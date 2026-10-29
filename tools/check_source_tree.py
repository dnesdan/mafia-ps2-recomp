#!/usr/bin/env python3
"""Reject game inputs/generated corpora; permit PNG documentation screenshots."""

from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
FORBIDDEN_PARTS = {".deps", ".work", ".local", "data", "extracted", "generated", "reports", "build", "dist"}
FORBIDDEN_SUFFIXES = {".iso", ".elf", ".irx", ".rar", ".7z", ".bin", ".dta", ".fql", ".pss", ".dat", ".ps2"}
MAGIC = (b"\x7fELF", b"Rar!", b"7z\xbc\xaf\x27\x1c", b"ISD0")
RASTER_SUFFIXES = {".png", ".jpg", ".jpeg", ".gif", ".webp", ".bmp", ".tga", ".dds", ".ppm", ".pgm", ".pbm", ".tif", ".tiff"}
SCREENSHOT_ROOT = Path("docs/screenshots")
PNG_MAGIC = b"\x89PNG\r\n\x1a\n"


def main() -> int:
    tracked = subprocess.check_output(["git", "-C", str(ROOT), "ls-files", "-z"])
    errors = []
    for raw in tracked.split(b"\0"):
        if not raw:
            continue
        name = raw.decode("utf-8")
        relative = Path(name)
        path = ROOT / relative
        if any(part in FORBIDDEN_PARTS for part in relative.parts) or relative.suffix.lower() in FORBIDDEN_SUFFIXES:
            errors.append(f"forbidden source path: {name}")
        is_documentation_png = relative.is_relative_to(SCREENSHOT_ROOT) and relative.suffix.lower() == ".png"
        if relative.suffix.lower() in RASTER_SUFFIXES and not is_documentation_png:
            errors.append(f"raster files are allowed only as PNG documentation screenshots: {name}")
        if path.is_symlink():
            errors.append(f"tracked symlink requires explicit review: {name}")
        elif path.is_file():
            with path.open("rb") as stream:
                prefix = stream.read(16)
            if any(prefix.startswith(value) for value in MAGIC):
                errors.append(f"binary game/archive signature: {name}")
            if is_documentation_png and not prefix.startswith(PNG_MAGIC):
                errors.append(f"documentation screenshot has an invalid PNG signature: {name}")
            if path.stat().st_size > 4 * 1024 * 1024:
                errors.append(f"unexpected large source file: {name}")
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 1
    print("Tracked tree excludes game inputs and generated corpora; PNG documentation screenshots are permitted.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
