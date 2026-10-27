#!/usr/bin/env python3
"""Resolve a runtime guest PC and last branch source against the exact input ELF."""

import argparse
import json
from pathlib import Path
import sys

from mafia.common import FormatError, write_json
from mafia.elf import ElfImage
from mafia.profile import select_profile

ROOT = Path(__file__).resolve().parents[1]


def symbolize(report: dict, elf: ElfImage, profile: dict) -> dict:
    if not isinstance(report, dict):
        raise FormatError("runtime report must be a JSON object")
    if report.get("elf_sha256") != elf.sha256:
        raise FormatError("runtime report and ELF belong to different builds")
    for field in ("last_guest_pc", "last_guest_branch_pc"):
        if field == "last_guest_branch_pc" and field not in report:
            continue  # Older reports did not capture the branch source.
        pc = report.get(field)
        if type(pc) is not int or not 0 <= pc <= 0xFFFFFFFF:
            raise FormatError(f"runtime report has no valid {field}")

    ranges = [{"name": symbol.name, "start": symbol.address, "end": symbol.address + symbol.size}
              for symbol in elf.functions() if symbol.size]
    ranges.extend({"name": symbol["name"], "start": symbol["start"], "end": symbol["end"]}
                  for symbol in profile.get("verified_untyped_code_ranges", []))

    def matches(pc: int) -> list[dict] | None:
        return sorted((symbol for symbol in ranges if symbol["start"] <= pc < symbol["end"]),
                      key=lambda symbol: (symbol["start"], symbol["name"])) or None

    result = dict(report)
    # Keep the existing field tied to the guest PC, even when the failed target
    # is zero. A source symbol must never be presented as the target's symbol.
    result["symbol"] = matches(report["last_guest_pc"])
    result["branch_source_symbol"] = (matches(report["last_guest_branch_pc"])
                                      if "last_guest_branch_pc" in report else None)
    result["symbolization_source"] = "exact ELF retained symbols and verified profile boot ranges"
    return result


def symbol_names(matches: list[dict] | None) -> str:
    return ", ".join(match["name"] for match in matches or []) or "no symbol covers this address"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        elf = ElfImage(args.elf)
        profile = select_profile(elf, ROOT / "profiles")
        report = symbolize(json.loads(args.report.read_text()), elf, profile)
        output = args.output or args.report.with_name(args.report.stem + ".symbolized.json")
        if output.resolve() == args.elf.resolve():
            raise FormatError("symbolized report must not replace the ELF")
        write_json(output, report)
        label = "target" if report.get("status") == "missing-dispatch" else "guest PC"
        locations = [f"{label} 0x{report['last_guest_pc']:08x}: {symbol_names(report['symbol'])}"]
        if "last_guest_branch_pc" in report:
            locations.append(f"last branch source 0x{report['last_guest_branch_pc']:08x}: "
                             f"{symbol_names(report['branch_source_symbol'])}")
        print("; ".join([*locations, f"stage={report.get('stage')}", report.get("error", "")]))
        return 0
    except (OSError, ValueError) as exc:
        print(f"symbolize_report: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
