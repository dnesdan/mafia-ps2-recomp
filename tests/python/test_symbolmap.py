"""Verify function extents with constructed ELF symbols, never game bytes."""

from __future__ import annotations

import csv
from pathlib import Path
import re
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))

from mafia.common import FormatError
from mafia.elf import ElfImage
from mafia.symbolmap import write_symbol_map
from fixtures import write_elf


class SymbolMapTests(unittest.TestCase):
    base = 0x100000

    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory(prefix="mafia-symbol-fixture-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.input = self.root / "fixture.elf"
        self.output = self.root / "functions.csv"

    def elf(self, symbols: list[tuple[str, int, int, int, int]]) -> ElfImage:
        write_elf(self.input, code=bytes(64), symbols=symbols)
        return ElfImage(self.input)

    def profile(self, elf: ElfImage, ranges: list[dict] | None = None) -> dict:
        return {"elf_sha256": elf.sha256, "elf_bytes": len(elf.data), "entry_point": elf.entry_point,
                "verified_untyped_code_ranges": ranges or []}

    def test_symbol_extents_are_deterministic_and_csv_safe(self) -> None:
        elf = self.elf([("template<A,B>", self.base, 16, 2, 2),
                        ("ordinary", self.base + 16, 16, 2, 2)])
        report = write_symbol_map(elf, self.profile(elf), self.output)
        original = self.output.read_bytes()
        self.assertEqual(report["functions"], 2)
        self.assertFalse(report["ghidra_executed"])
        rows = list(csv.reader(self.output.read_text().splitlines()))
        self.assertEqual(len(rows), 3)
        for row in rows[1:]:
            self.assertEqual(len(row), 4)
            self.assertIsNotNone(re.fullmatch(r"[A-Za-z0-9_]+", row[0]))
            self.assertEqual(int(row[2], 16) - int(row[1], 16), int(row[3]))
        self.assertEqual(int(rows[1][1], 16), self.base)
        write_symbol_map(elf, self.profile(elf), self.output)
        self.assertEqual(self.output.read_bytes(), original)

    def test_exact_aliases_deduplicate_but_conflicting_extents_fail(self) -> None:
        elf = self.elf([("z_alias", self.base, 16, 2, 2), ("a_alias", self.base, 16, 2, 2)])
        report = write_symbol_map(elf, self.profile(elf), self.output)
        self.assertEqual(report["functions"], 1)
        self.assertIn("a_alias,", self.output.read_text())
        elf = self.elf([("first", self.base, 16, 2, 2), ("alias", self.base, 20, 2, 2)])
        with self.assertRaises(FormatError):
            write_symbol_map(elf, self.profile(elf), self.output)

    def test_zero_sized_function_requires_review_and_does_not_overwrite_output(self) -> None:
        elf = self.elf([("unbounded", self.base, 0, 2, 2)])
        self.output.write_text("existing output\n")
        with self.assertRaises(FormatError):
            write_symbol_map(elf, self.profile(elf), self.output)
        self.assertEqual(self.output.read_text(), "existing output\n")

    def test_untyped_entry_requires_matching_symbol_and_explicit_evidence(self) -> None:
        elf = self.elf([("boot", self.base, 0, 0, 2),
                        ("ordinary", self.base + 8, 16, 2, 2)])
        region = {"name": "boot", "start": self.base, "end": self.base + 8,
                  "evidence": "Constructed fixture boundary before the next sized function"}
        with self.assertRaises(FormatError):
            write_symbol_map(elf, self.profile(elf), self.output)
        self.assertEqual(write_symbol_map(elf, self.profile(elf, [region]), self.output)["functions"], 2)
        for bad in ({**region, "evidence": ""}, {**region, "name": "absent"},
                    {**region, "start": self.base + 4}):
            with self.subTest(region=bad), self.assertRaises(FormatError):
                write_symbol_map(elf, self.profile(elf, [bad]), self.output)

    def test_overlapping_or_unaligned_function_extents_fail(self) -> None:
        for symbols in ([('first', self.base, 16, 2, 2), ('overlap', self.base + 8, 16, 2, 2)],
                        [('unaligned_end', self.base, 15, 2, 2)],
                        [('first', self.base, 8, 2, 2), ('unaligned_start', self.base + 9, 7, 2, 2)]):
            with self.subTest(symbols=symbols):
                elf = self.elf(symbols)
                with self.assertRaises(FormatError):
                    write_symbol_map(elf, self.profile(elf), self.output)

    def test_non_executable_and_out_of_section_symbols_fail(self) -> None:
        for symbol in (("data_function", self.base + 64, 4, 2, 5),
                       ("outside", self.base, 68, 2, 2)):
            with self.subTest(symbol=symbol):
                elf = self.elf([symbol])
                with self.assertRaises(FormatError):
                    write_symbol_map(elf, self.profile(elf), self.output)

    def test_section_extent_must_also_have_file_backed_load_bytes(self) -> None:
        meta = write_elf(self.input, code=bytes(64), symbols=[("fixture", self.base, 32, 2, 2)])
        data = bytearray(self.input.read_bytes())
        struct.pack_into("<I", data, meta["ph"] + 16, 16)
        self.input.write_bytes(data)
        elf = ElfImage(self.input)
        with self.assertRaises(FormatError):
            write_symbol_map(elf, self.profile(elf), self.output)


if __name__ == "__main__":
    unittest.main()
