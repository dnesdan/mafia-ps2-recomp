#!/usr/bin/env python3
"""Prepare local game data, build pinned tools and generate the native corpus."""

import argparse
import json
import os
from pathlib import Path
import platform
import subprocess
import sys

from mafia.common import FormatError
from mafia.upstream import prepare_dependencies
from mafia.toolchain import stamp_built_tools

ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso", type=Path, default=os.environ.get("MAFIA_PS2_ISO"))
    parser.add_argument("--work", type=Path, default=ROOT / ".work")
    parser.add_argument("--deps", type=Path, default=ROOT / ".deps")
    parser.add_argument("--tools-build", type=Path, default=ROOT / "build/upstream")
    parser.add_argument("--tools-only", action="store_true")
    parser.add_argument("--extract", choices=("all", "boot"), default="all")
    parser.add_argument("--diagnostic", action="store_true")
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--cmake", default=os.environ.get("CMAKE", "cmake"))
    parser.add_argument("--config", default="Release", choices=("Debug", "Release", "RelWithDebInfo", "MinSizeRel"))
    args = parser.parse_args()
    if not args.tools_only and args.iso is None:
        parser.error("set MAFIA_PS2_ISO or pass --iso; nothing is downloaded on your behalf")
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    try:
        if not args.tools_only:
            subprocess.run([sys.executable, str(ROOT / "tools/prepare_iso.py"), str(args.iso),
                            "--output", str(args.work), "--extract", args.extract], check=True)
        source, cache = prepare_dependencies(ROOT, args.deps.resolve())
        configure = [args.cmake, "-S", str(source), "-B", str(args.tools_build), "-C", str(cache),
                     f"-DCMAKE_BUILD_TYPE={args.config}", "-DPS2X_BUILD_RUNTIME=OFF", "-DPS2X_BUILD_TEST=OFF",
                     "-DPS2X_BUILD_STUDIO=OFF", "-DPS2X_BUILD_RECOMP=ON", "-DPS2X_BUILD_ANALYZER=ON",
                     "-DBUILD_TESTING=OFF"]
        if platform.system() == "Darwin":
            if platform.machine() != "arm64":
                raise FormatError("run from a native Apple Silicon terminal; Rosetta is not supported")
            configure += ["-DCMAKE_OSX_ARCHITECTURES=arm64", "-DCMAKE_C_COMPILER=clang", "-DCMAKE_CXX_COMPILER=clang++"]
        subprocess.run(configure, check=True)
        subprocess.run([args.cmake, "--build", str(args.tools_build), "--parallel", str(args.jobs),
                        "--config", args.config, "--target", "ps2_recomp", "ps2_analyzer"], check=True)
        lock = json.loads((ROOT / "dependencies.lock.json").read_text())
        stamp_built_tools(source, args.tools_build.resolve(), lock["ps2recomp"]["commit"],
                          sorted((ROOT / "patches").glob("*.patch")), args.config)
        if not args.tools_only:
            command = [sys.executable, str(ROOT / "tools/recompile.py"), "--work", str(args.work),
                       "--upstream", str(source), "--tools-build", str(args.tools_build)]
            if args.diagnostic:
                command.append("--diagnostic")
            subprocess.run(command, check=True)
        return 0
    except (OSError, ValueError, subprocess.CalledProcessError) as exc:
        print(f"bootstrap: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
