#!/usr/bin/env python3
"""Fail if the runtime source differs from its exact revision and patch set."""

import argparse
import json
from pathlib import Path
import subprocess
import sys

from mafia.upstream import verify

ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    args = parser.parse_args()
    try:
        lock = json.loads((ROOT / "dependencies.lock.json").read_text())
        patches = verify(args.source, lock["ps2recomp"]["commit"], sorted((ROOT / "patches").glob("*.patch")))
        print(json.dumps({"commit": lock["ps2recomp"]["commit"], "patches": patches}, sort_keys=True))
        return 0
    except (ValueError, OSError, subprocess.CalledProcessError) as exc:
        print(f"verify_upstream: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
