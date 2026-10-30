#!/usr/bin/env python3
"""Compile the actual GS frontend/backend upload tests under ASan and UBSan."""

import argparse
import os
from pathlib import Path
import platform
import shlex
import subprocess
import sys


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--cxx", default=os.environ.get("CXX", "c++"))
    parser.add_argument("--sse2neon", type=Path)
    args = parser.parse_args()
    source, build = args.source.resolve(), args.build.resolve()
    architecture = platform.machine().lower()
    arch_flags = []
    if architecture in ("x86_64", "amd64"):
        arch_flags.append("-msse4.1")
    elif architecture in ("arm64", "aarch64"):
        if args.sse2neon is None or not (args.sse2neon / "sse2neon.h").is_file():
            parser.error("ARM64 requires --sse2neon pointing to the pinned header directory")
        arch_flags += ["-I", str(args.sse2neon.resolve())]
    else:
        parser.error(f"unsupported sanitizer host architecture: {architecture}")
    if build == source or source in build.parents:
        parser.error("--build must be outside the verified upstream source tree")
    units = [source / name for name in (
        "ps2xRuntime/src/lib/gs/gs_cpu_backend.cpp",
        "ps2xRuntime/src/lib/gs/gs_frontend.cpp",
        "ps2xRuntime/src/lib/gs/ps2_gs_memory.cpp",
        "ps2xTest/src/ps2_gs_upload_tests.cpp",
    )]
    if any(not unit.is_file() for unit in units):
        parser.error("prepare the pinned upstream with the GS upload patch first")
    try:
        build.mkdir(parents=True, exist_ok=True)
        runner = build / "gs_upload_main.cpp"
        runner.write_text(
            '#include "MiniTest.h"\n'
            'void register_ps2_gs_upload_tests();\n'
            'int main() { register_ps2_gs_upload_tests(); return MiniTest::Run(); }\n',
            encoding="utf-8",
        )
        binary = build / "gs_upload_tests"
        command = [args.cxx, "-std=c++20", "-O1", "-g1", "-pthread",
                   "-fno-fast-math", "-ffp-contract=off", "-fno-omit-frame-pointer",
                   "-fsanitize=address,undefined", *arch_flags]
        for include in ("ps2xRuntime/include", "ps2xRuntime/src/lib", "ps2xIOP/include", "ps2xTest/include"):
            command += ["-I", str(source / include)]
        command += [str(unit) for unit in units] + [str(runner), "-o", str(binary)]
        print(shlex.join(command), flush=True)
        subprocess.run(command, check=True, timeout=300)
        env = os.environ.copy()
        # LeakSanitizer is available on the Linux CI host, not Apple platforms.
        leaks = "1" if platform.system() == "Linux" else "0"
        env["ASAN_OPTIONS"] = f"detect_leaks={leaks}:halt_on_error=1"
        env["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
        subprocess.run([str(binary)], check=True, env=env, timeout=60)
        return 0
    except (OSError, subprocess.SubprocessError) as error:
        print(f"check_gs_uploads: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
