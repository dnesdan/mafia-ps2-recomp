#!/usr/bin/env python3
"""Compile actual generated integer operations with UBSan (Linux/macOS Clang/GCC)."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import platform
import subprocess
import tempfile


def run(args: list[str], **kwargs: object) -> None:
    subprocess.run(args, check=True, **kwargs)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True, help="Patched PS2Recomp checkout")
    parser.add_argument("--build", type=Path, required=True, help="Built upstream CMake directory")
    parser.add_argument("--sse2neon", type=Path, help="Header directory on ARM64")
    args = parser.parse_args()
    source, build = args.source.resolve(), args.build.resolve()
    compiler = os.environ.get("CXX", "c++")
    flags = ["-std=c++20", "-O2"]
    if platform.machine().lower() in ("arm64", "aarch64"):
        if args.sse2neon is None:
            parser.error("ARM64 requires --sse2neon")
        flags += ["-DUSE_SSE2NEON", "-I" + str(args.sse2neon.resolve())]
    else:
        flags += ["-msse4.1"]
    includes = ["-I" + str(source / path) for path in (
        "ps2xRecomp/include", "ps2xRuntime/include", "ps2xIOP/include")]
    fmt_candidates = sorted((build / "_deps/fmt-build").glob("libfmt*.a"))
    if len(fmt_candidates) != 1:
        parser.error("Expected one static fmt library in the upstream build")
    libraries = [
        build / "ps2xRecomp/libps2_recomp_lib.a", fmt_candidates[0],
        build / "_deps/libdwarf-build/src/lib/libdwarf/libdwarf.a",
        build / "ps2xRecomp/librabbitizer.a",
    ]
    with tempfile.TemporaryDirectory(prefix="mafia-integer-") as tmp:
        work = Path(tmp)
        generator = work / "generator"
        run([compiler, *flags, *includes, str(Path(__file__).with_name("integer_codegen.cpp")),
             *(str(p) for p in libraries), "-lz", "-o", str(generator)])
        generated = work / "generated.cpp"
        with generated.open("w") as out:
            run([str(generator)], stdout=out)
        executable = work / "integer-tests"
        run([compiler, *flags, *includes, "-fsanitize=undefined", "-fno-sanitize-recover=all",
             str(generated), "-o", str(executable)])
        run([str(executable)])


if __name__ == "__main__":
    main()
