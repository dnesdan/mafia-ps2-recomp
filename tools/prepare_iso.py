#!/usr/bin/env python3
"""Inspect a user-owned local ISO and extract all files or only its boot files."""

import argparse
import os
from pathlib import Path
import shutil
import sys

from mafia.common import FormatError, safe_destination, sha256_file, write_json
from mafia.elf import ElfImage
from mafia.iso import IsoImage, parse_system_cnf


def prepare(iso: Path, output: Path, mode: str) -> dict:
    output = output.resolve()
    if iso.resolve() == (output / "disc.json").resolve():
        raise FormatError("input ISO collides with the generated disc.json report")
    if iso.resolve().is_relative_to(output / "extracted"):
        raise FormatError("input ISO must be outside the extraction directory")
    with IsoImage(iso) as disc:
        boot = parse_system_cnf(disc.read_file("SYSTEM.CNF", limit=65536))
        executable = disc.lookup(boot["boot2"])
        if executable.is_directory:
            raise FormatError("BOOT2 points to a directory")
        files = [entry for entry in disc.entries if not entry.is_directory]
        selected = files if mode == "all" else [disc.lookup("SYSTEM.CNF"), executable]
        required = sum(e.size for e in selected if not safe_destination(output / "extracted", e.path).exists())
        output.mkdir(parents=True, exist_ok=True)
        if shutil.disk_usage(output).free < required + 16 * 1024 * 1024:
            raise FormatError(f"need up to {required:,} bytes to extract; use --extract boot for analysis only")
        iso_hash = sha256_file(iso)
        retained = {}
        for entry in selected:
            retained[entry.path] = disc.extract(entry, output / "extracted")
        elf = ElfImage(output / "extracted" / executable.path)
        report = {"schema_version": 1, "iso_sha256": iso_hash, "iso_bytes": disc.size,
                  "sector_size": 2048, "volume_id": disc.volume_id, **boot,
                  "boot_elf": executable.path, "elf_sha256": elf.sha256, "entry_point": elf.entry_point,
                  "file_count": len(files), "directory_count": len(disc.entries) - len(files),
                  "extraction": mode, "complete_extraction": mode == "all", "retained_files": len(selected),
                  "files": [{**e.report(), "retained": e.path in retained, "sha256": retained.get(e.path)} for e in disc.entries]}
        write_json(output / "disc.json", report)
        return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("iso", nargs="?", default=os.environ.get("MAFIA_PS2_ISO"), type=Path)
    parser.add_argument("--output", type=Path, default=Path(".work"))
    parser.add_argument("--extract", choices=("all", "boot"), default="all")
    args = parser.parse_args()
    if args.iso is None:
        parser.error("provide a local ISO path or set MAFIA_PS2_ISO")
    try:
        report = prepare(args.iso, args.output, args.extract)
        print(f"{report['serial'] or 'unknown serial'}: {report['file_count']} files; "
              f"ELF entry 0x{report['entry_point']:08x}; extracted {report['retained_files']} files")
        print(f"ISO SHA256 {report['iso_sha256']}")
        print(f"ELF SHA256 {report['elf_sha256']}")
        return 0
    except (OSError, FormatError) as exc:
        print(f"prepare_iso: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
