#!/usr/bin/env python3
"""Produce local deterministic ELF/symbol/ISA reports without copying game code."""

import argparse
from dataclasses import asdict
from pathlib import Path
import shutil
import subprocess
import sys

from mafia.common import FormatError, write_json
from mafia.elf import ElfImage


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    parser.add_argument("--output", type=Path, default=Path(".work/analysis"))
    parser.add_argument("--external-tools", action="store_true", help="also save llvm-readelf/llvm-nm (GNU fallback) output")
    args = parser.parse_args()
    try:
        elf = ElfImage(args.elf)
        report = elf.report()
        write_json(args.output / "elf.json", report)
        write_json(args.output / "symbols.json", [asdict(s) for s in elf.symbols])
        write_json(args.output / "functions.json", [asdict(s) for s in elf.functions()])
        external = []
        if args.external_tools:
            for candidates, flags, name in [(("llvm-readelf", "readelf"), ["-h", "-l", "-S", "-W"], "readelf.txt"),
                                            (("llvm-nm", "nm"), ["-n", "-S"], "nm.txt")]:
                tool = next((shutil.which(c) for c in candidates if shutil.which(c)), None)
                if not tool:
                    external.append({"tool": candidates[0], "status": "not-installed"})
                    continue
                with (args.output / name).open("w", encoding="utf-8") as stream:
                    proc = subprocess.run([tool, *flags, str(args.elf.resolve())], stdout=stream, stderr=subprocess.PIPE, text=True)
                external.append({"tool": Path(tool).name, "status": "passed" if proc.returncode == 0 else "failed",
                                 "returncode": proc.returncode, "stderr": proc.stderr})
            write_json(args.output / "external-tools.json", external)
        print(f"SHA256 {elf.sha256}; entry 0x{elf.entry_point:08x}; "
              f"{report['defined_function_symbols']} function symbols; debug sections: {report['debug_sections']}")
        if any(item["status"] == "failed" for item in external):
            return 1
        return 0
    except (OSError, FormatError) as exc:
        print(f"analyze_elf: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
