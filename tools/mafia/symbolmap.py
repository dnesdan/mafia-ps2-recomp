"""Export retained symbol boundaries in PS2Recomp's CSV interchange format.

This is a symbol map, not Ghidra output. Untyped boot code requires explicit,
hash-scoped ranges reviewed against the actual instructions in the profile.
"""

from __future__ import annotations

from pathlib import Path
import re

from .common import FormatError
from .elf import ElfImage


def write_symbol_map(elf: ElfImage, profile: dict, destination: Path) -> dict:
    functions: dict[int, tuple[str, int]] = {}

    def add(name: str, start: int, end: int) -> None:
        if not name:
            name = f"symbol_{start:08x}"
        # Upstream reads CSV with getline(',') rather than a quoted CSV parser.
        # Metrowerks template symbols contain commas. Preserve their exact
        # spelling in symbols.json, using an address-unique safe label here.
        if re.search(r"[^A-Za-z0-9_]", name):
            name = re.sub(r"[^A-Za-z0-9_]", "_", name) + f"_at_{start:08x}"
        if start % 4 or end % 4 or end <= start:
            raise FormatError(f"invalid function extent: {name}")
        if not any(s.flags & 4 and s.address <= start < end <= s.address + s.size for s in elf.sections):
            raise FormatError(f"function is outside executable section: {name}")
        elf.bytes_at(start, end - start)
        if start in functions and functions[start][1] != end:
            raise FormatError(f"conflicting symbol extents at 0x{start:08x}")
        if start not in functions or name < functions[start][0]:
            functions[start] = (name, end)

    for symbol in elf.functions():
        if symbol.section >= len(elf.sections):
            raise FormatError(f"function has an invalid section: {symbol.name}")
        section = elf.sections[symbol.section]
        if not section.flags & 4 or not section.address <= symbol.address < symbol.address + symbol.size <= section.address + section.size:
            raise FormatError(f"symbol lacks a bounded executable extent: {symbol.name}; use a reviewed external map")
        add(symbol.name, symbol.address, symbol.address + symbol.size)
    for region in profile.get("verified_untyped_code_ranges", []):
        if not region.get("evidence") or not any(s.name == region["name"] and s.address == region["start"] for s in elf.symbols):
            raise FormatError("profile boot-code range lacks matching symbol/evidence")
        add(region["name"], region["start"], region["end"])
    if elf.entry_point not in functions:
        raise FormatError("entry point has no verified function boundary; add a hash-scoped boot range or a reviewed external map")
    ordered = sorted(functions.items())
    last_end = 0
    for address, (name, end) in ordered:
        if address < last_end:
            raise FormatError(f"overlapping function boundaries at {name}; inspect aliases in an external map")
        last_end = end
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text("Name,Start,End,Size\n" + "".join(
        f"{name},0x{start:08X},0x{end:08X},{end-start}\n" for start, (name, end) in ordered), encoding="utf-8")
    return {"functions": len(functions), "source": "retained ELF function symbols plus verified hash-scoped boot ranges",
            "ghidra_executed": False}
