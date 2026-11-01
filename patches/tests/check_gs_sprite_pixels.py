#!/usr/bin/env python3
"""Check sprite framebuffer pixels locally; no network or CI.

Default: compile the complete production CPU backend and its real VRAM/cache.
--source-slices: explicitly narrower recovery mode: extract four production
routines and use a CT32 storage adapter. This is NOT full-backend validation.
Both modes execute the identical pixel assertions, never sampler/writer mocks.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys

from check_gs_sprite_mapping import definition


def prepare_slices(source: Path, build: Path) -> dict[str, str]:
    backend = (source / "ps2xRuntime/src/lib/gs/gs_cpu_backend.cpp").read_text(encoding="utf-8")
    helpers = [
        "float fabsQ(", "u16 Rgba8888ToRgba5551(", "u32 Rgba5551ToRgba8888(",
        "u32 pack32(", "uint32_t applyTexa(", "int wrapTextureCoordinate(",
        "bool passesAlphaTest(", "struct PixelWriteMask", "PixelWriteMask classifyAlphaTest(",
        "bool passesDestinationAlphaTest(", "struct TextureCombineResult", "TextureCombineResult combineTexture(",
        "bool isFourBitIndexedPsm(", "bool isEightBitIndexedPsm(", "uint8_t lerpChannel(",
    ]
    methods = ["uint32_t GSCpuBackend::LookupCLUT(", "uint32_t GSCpuBackend::SampleTexture(",
               "void GSCpuBackend::WritePixel(", "void GSCpuBackend::DrawSprite("]
    definitions = {sig: definition(backend, sig, struct=sig.startswith("struct "))
                   for sig in helpers + methods}
    for name, signatures in (("sprite_pixel_helpers.inc", helpers), ("sprite_pixel_methods.inc", methods)):
        (build / name).write_text("\n".join(definitions[sig] for sig in signatures), encoding="utf-8")
    return {sig: hashlib.sha256(text.encode()).hexdigest() for sig, text in definitions.items()}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--cxx", default=os.environ.get("CXX", "c++"))
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--source-slices", action="store_true", help="explicit CT32 recovery adapter; NOT full backend")
    args = parser.parse_args()
    source, build = args.source.resolve(), args.build.resolve()
    tests = Path(__file__).resolve().parent
    if build == source or source in build.parents:
        parser.error("--build must be outside the source tree")
    try:
        build.mkdir(parents=True, exist_ok=True)
        units = [tests / "gs_sprite_pixels_probe.cpp"]
        command = [args.cxx, "-std=c++20", "-O2", "-g1", "-pthread", "-Wall", "-Wextra",
                   "-fno-fast-math", "-ffp-contract=off", "-fno-omit-frame-pointer"]
        if args.sanitize:
            command += ["-fsanitize=address,undefined,float-cast-overflow", "-fno-sanitize-recover=all"]
        if args.source_slices:
            scope = "SOURCE SLICES: production pixel routines + CT32 adapter; NO frontend/cache/full backend"
            hashes = prepare_slices(source, build)
            command += ["-DMAFIA_SPRITE_SOURCE_SLICES=1"]
        else:
            scope = "COMPLETE CPU BACKEND: production Submit, rasterizer, sampler, VRAM and texture cache; NO game/frontend"
            units += [source / "ps2xRuntime/src/lib/gs/gs_cpu_backend.cpp",
                      source / "ps2xRuntime/src/lib/gs/ps2_gs_memory.cpp"]
            hashes = {str(unit.relative_to(source)): hashlib.sha256(unit.read_bytes()).hexdigest()
                      for unit in units[1:]}
        print("SCOPE:", scope, flush=True)
        print(json.dumps(hashes, indent=2, sort_keys=True), flush=True)
        for include in (source / "ps2xRuntime/include", source / "ps2xRuntime/src/lib", tests, build):
            command += ["-I", str(include)]
        binary = build / "gs_sprite_pixels_tests"
        command += [str(unit) for unit in units] + ["-o", str(binary)]
        print(shlex.join(command), flush=True)
        subprocess.run(command, check=True, timeout=180)
        env = os.environ.copy()
        if args.sanitize:
            leaks = "1" if sys.platform.startswith("linux") else "0"
            env["ASAN_OPTIONS"] = f"detect_leaks={leaks}:halt_on_error=1"
            env["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
        subprocess.run([str(binary)], check=True, env=env, timeout=60)
        return 0
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"check_gs_sprite_pixels: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
