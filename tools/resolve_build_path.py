#!/usr/bin/env python3
"""Resolve the app executable using the actual CMake generator/configuration."""

import argparse
from pathlib import Path
import sys

from mafia.toolchain import executable_path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path)
    parser.add_argument("configuration", choices=("Debug", "Release", "RelWithDebInfo", "MinSizeRel"))
    args = parser.parse_args()
    try:
        print(executable_path(args.build.resolve(), "", "MafiaNative.app/Contents/MacOS/MafiaNative", args.configuration))
        return 0
    except (OSError, ValueError) as exc:
        print(f"resolve_build_path: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
