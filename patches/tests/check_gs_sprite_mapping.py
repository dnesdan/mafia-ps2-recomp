#!/usr/bin/env python3
"""Locally test the production DrawSprite body at the sampler/writer boundary.

Uses the upstream GS types, coordinate helpers and texture combiner. Only the
SampleTexture/WritePixel calls are recorders: this is not a full renderer,
filtering, VRAM, frontend or original-game test. No network or CI is used.
"""
from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys


def definition(source: str, signature: str, *, struct: bool = False) -> str:
    """Extract one brace-delimited definition without interpreting comment braces."""
    if source.count(signature) != 1:
        raise ValueError(f"expected exactly one production definition: {signature}")
    start = source.index(signature)
    tokens = re.finditer(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]', source[start:], re.S)
    depth = 0
    opened = False
    for token in tokens:
        if token.group() == "{":
            depth += 1
            opened = True
        elif token.group() == "}":
            depth -= 1
            if opened and depth == 0:
                end = start + token.end()
                if struct:
                    if source[end:end + 1] != ";":
                        raise ValueError(f"missing struct terminator: {signature}")
                    end += 1
                return source[start:end] + "\n"
    raise ValueError(f"unterminated production definition: {signature}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True, help="prepared PS2Recomp source tree")
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--cxx", default=os.environ.get("CXX", "c++"))
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    source, build = args.source.resolve(), args.build.resolve()
    if build == source or source in build.parents:
        parser.error("--build must be outside the source tree")
    try:
        backend = (source / "ps2xRuntime/src/lib/gs/gs_cpu_backend.cpp").read_text(encoding="utf-8")
        draw = definition(backend, "void GSCpuBackend::DrawSprite(")
        helpers = definition(backend, "float fabsQ(")
        helpers += definition(backend, "struct TextureCombineResult", struct=True)
        helpers += definition(backend, "TextureCombineResult combineTexture(")
        build.mkdir(parents=True, exist_ok=True)
        (build / "sprite_draw.inc").write_text(draw, encoding="utf-8")
        (build / "sprite_helpers.inc").write_text(helpers, encoding="utf-8")
        print("SCOPE: production DrawSprite; sampler/writer recorders, NOT full GS integration.", flush=True)
        print("DrawSprite SHA256:", hashlib.sha256(draw.encode()).hexdigest(), flush=True)
        binary = build / "gs_sprite_mapping_tests"
        command = [args.cxx, "-std=c++20", "-O2", "-g1", "-Wall", "-Wextra",
                   "-fno-fast-math", "-ffp-contract=off", "-fno-omit-frame-pointer"]
        if args.sanitize:
            command += ["-fsanitize=address,undefined,float-cast-overflow", "-fno-sanitize-recover=all"]
        command += ["-I", str(source / "ps2xRuntime/include"), "-I", str(build),
                    str(Path(__file__).with_name("gs_sprite_mapping_probe.cpp")), "-o", str(binary)]
        print(shlex.join(command), flush=True)
        subprocess.run(command, check=True, timeout=120)
        env = os.environ.copy()
        if args.sanitize:
            # LeakSanitizer is not supported by Apple's host ASan.
            leaks = "1" if sys.platform.startswith("linux") else "0"
            env["ASAN_OPTIONS"] = f"detect_leaks={leaks}:halt_on_error=1"
            env["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
        subprocess.run([str(binary)], check=True, env=env, timeout=60)
        return 0
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"check_gs_sprite_mapping: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
