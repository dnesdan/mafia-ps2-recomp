#!/usr/bin/env python3
"""Resolve a runtime failure PC against the exact original ELF's retained symbols."""

import argparse
import json
from pathlib import Path
import sys

from mafia.common import FormatError, write_json
from mafia.elf import ElfImage
from mafia.profile import select_profile

ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        elf = ElfImage(args.elf)
        profile = select_profile(elf, ROOT / "profiles")
        report = json.loads(args.report.read_text())
        if report.get("elf_sha256") != elf.sha256:
            raise FormatError("runtime report and ELF belong to different builds")
        pc = report.get("last_guest_pc")
        if not isinstance(pc, int) or not 0 <= pc <= 0xFFFFFFFF:
            raise FormatError("runtime report has no valid guest PC")
        matches = [{"name": s.name, "start": s.address, "end": s.address + s.size}
                   for s in elf.functions() if s.size and s.address <= pc < s.address + s.size]
        matches.extend({"name": s["name"], "start": s["start"], "end": s["end"]}
                       for s in profile.get("verified_untyped_code_ranges", []) if s["start"] <= pc < s["end"])
        report["symbol"] = sorted(matches, key=lambda match: (match["start"], match["name"])) or None
        report["symbolization_source"] = "exact ELF retained symbols and verified profile boot ranges"
        output = args.output or args.report.with_name(args.report.stem + ".symbolized.json")
        if output.resolve() == args.elf.resolve():
            raise FormatError("symbolized report must not replace the ELF")
        write_json(output, report)
        names = ", ".join(match["name"] for match in matches) or "no symbol covers this address"
        print(f"0x{pc:08x}: {names}; stage={report.get('stage')}; {report.get('error', '')}")
        return 0
    except (OSError, ValueError) as exc:
        print(f"symbolize_report: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
