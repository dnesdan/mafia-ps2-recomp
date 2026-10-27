#!/usr/bin/env python3
"""Run the real pinned PS2Recomp analyzer/recompiler for a verified game build."""

import argparse
import json
from pathlib import Path
import subprocess
import sys

from mafia.common import FormatError, sha256_file, write_json
from mafia.elf import ElfImage
from mafia.generation import inspect_generated
from mafia.profile import recompiler_config, select_profile
from mafia.symbolmap import write_symbol_map
from mafia.upstream import verify
from mafia.toolchain import verified_tools

ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path)
    parser.add_argument("--work", type=Path, default=ROOT / ".work")
    parser.add_argument("--upstream", type=Path, default=ROOT / ".deps/PS2Recomp")
    parser.add_argument("--tools-build", type=Path, default=ROOT / "build/upstream")
    parser.add_argument("--function-map", type=Path, help="optional reviewed Ghidra CSV for this exact build")
    parser.add_argument("--diagnostic", action="store_true", help="permit generated unresolved traps, with explicit status")
    args = parser.parse_args()
    try:
        work = args.work.resolve()
        work.mkdir(parents=True, exist_ok=True)
        elf_path = args.elf
        if elf_path is None:
            disc = json.loads((work / "disc.json").read_text())
            elf_path = work / "extracted" / disc["boot_elf"]
        elf = ElfImage(elf_path)
        profile = select_profile(elf, ROOT / "profiles")
        lock = json.loads((ROOT / "dependencies.lock.json").read_text())
        actual = subprocess.check_output(["git", "-C", str(args.upstream), "rev-parse", "HEAD"], text=True).strip()
        if actual != lock["ps2recomp"]["commit"]:
            raise FormatError("PS2Recomp checkout does not match dependency lock")
        patches = verify(args.upstream, actual, sorted((ROOT / "patches").glob("*.patch")))
        tools_build = args.tools_build.resolve()
        analyzer, recompiler = verified_tools(args.upstream, tools_build, actual, sorted((ROOT / "patches").glob("*.patch")))
        # Freeze exactly the bytes checked above. Never race the external input.
        frozen = work / "input.elf"
        frozen.write_bytes(elf.data)
        function_map = args.function_map
        if function_map is None:
            function_map = work / "symbol-functions.csv"
            map_evidence = write_symbol_map(elf, profile, function_map)
        else:
            if not function_map.is_file():
                raise FormatError("function map does not exist")
            map_evidence = {"source": "user-supplied external function map", "ghidra_executed": False}
        with (work / "analyzer.log").open("w") as log:
            subprocess.run([str(analyzer), str(frozen), str(work / "analyzer-candidates.toml")],
                           stdout=log, stderr=subprocess.STDOUT, check=True)
        # Candidate analyzer patches/stubs are deliberately not imported.
        output = work / "generated"
        if output.exists() and any(output.iterdir()):
            raise FormatError(f"output already exists: {output}; select a fresh --work directory to prevent stale generated functions")
        output.mkdir(parents=True, exist_ok=True)
        config = work / "recompile.toml"
        config.write_text(recompiler_config(frozen, output, profile, function_map), encoding="utf-8")
        with (work / "recompiler.log").open("w") as log:
            subprocess.run([str(recompiler), str(config)], stdout=log, stderr=subprocess.STDOUT, check=True)
        audit = inspect_generated(output)
        write_json(work / "generation-audit.json", audit)
        if audit["prohibited_substitutions"]:
            raise FormatError("generated code contains prohibited substitutions; see generation-audit.json")
        if audit.get("silent_unsupported_lines", 0):
            raise FormatError("generated code silently substitutes unsupported behavior; diagnostic mode does not permit this")
        if audit["unresolved_lines"] and not args.diagnostic:
            raise FormatError("unresolved generated instructions; inspect generation-audit.json and use --diagnostic only for bring-up")
        metadata = {"schema_version": 1, "profile_id": profile["profile_id"], "elf_sha256": elf.sha256,
                    "entry_point": elf.entry_point, "ps2recomp_commit": actual,
                    "recompiler_binary_sha256": sha256_file(recompiler), "analyzer_binary_sha256": sha256_file(analyzer),
                    "toolchain_stamp_sha256": sha256_file(tools_build / "mafia-toolchain.json"),
                    "source_tree_sha256": audit["source_tree_sha256"], "diagnostic": args.diagnostic,
                    "sources_manifest_sha256": audit["sources_manifest_sha256"], "patches": patches,
                    "unresolved_lines": audit["unresolved_lines"], "gameplay_verified": False,
                    "function_map_sha256": sha256_file(function_map), "function_map_evidence": map_evidence}
        write_json(output / "generation.json", metadata)
        print(f"Generated {audit['translation_units']} C++ units, {audit['source_bytes']:,} bytes; "
              f"unresolved source lines: {audit['unresolved_lines']}; diagnostic={args.diagnostic}")
        print(f"Configure the host with -DMAFIA_GENERATED_DIR={output}")
        return 0
    except (OSError, ValueError, subprocess.CalledProcessError) as exc:
        print(f"recompile: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
