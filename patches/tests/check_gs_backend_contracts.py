#!/usr/bin/env python3
"""Run full production GS transfer and framebuffer clear checks locally, without CI."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--cxx", default=os.environ.get("CXX", "c++"))
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    source, build = args.source.resolve(), args.build.resolve()
    if build == source or source in build.parents:
        parser.error("--build must be outside the source tree")
    tests = Path(__file__).resolve().parent
    units = [tests / "gs_backend_contracts_probe.cpp"] + [source / name for name in (
        "ps2xRuntime/src/lib/gs/gs_cpu_backend.cpp", "ps2xRuntime/src/lib/gs/ps2_gs_memory.cpp")]
    try:
        hashes = {str(p.relative_to(source)): hashlib.sha256(p.read_bytes()).hexdigest() for p in units[1:]}
        build.mkdir(parents=True, exist_ok=True)
        binary = build / "gs_backend_contracts_tests"
        command = [args.cxx, "-std=c++20", "-O2", "-g1", "-pthread", "-Wall", "-Wextra",
                   "-fno-fast-math", "-ffp-contract=off", "-fno-omit-frame-pointer"]
        if args.sanitize:
            command += ["-fsanitize=address,undefined,float-cast-overflow", "-fno-sanitize-recover=all"]
        command += ["-I", str(source / "ps2xRuntime/include")]
        command += [str(p) for p in units] + ["-o", str(binary)]
        print("SCOPE: complete production CPU GS backend and real VRAM/cache; no game or frontend", flush=True)
        print(json.dumps(hashes, indent=2, sort_keys=True), flush=True)
        print(shlex.join(command), flush=True)
        # Never run an older executable if a misconfigured compiler emits none.
        binary.unlink(missing_ok=True)
        subprocess.run(command, check=True, timeout=180)
        env = os.environ.copy()
        if args.sanitize:
            leaks = "1" if sys.platform.startswith("linux") else "0"
            env["ASAN_OPTIONS"] = f"detect_leaks={leaks}:halt_on_error=1"
            env["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
        subprocess.run([str(binary)], check=True, env=env, timeout=60)
        return 0
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"check_gs_backend_contracts: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
