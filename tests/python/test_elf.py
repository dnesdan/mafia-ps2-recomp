"""ELF validation and instruction-survey tests built from public synthetic bytes."""

from __future__ import annotations

import hashlib
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))

from mafia.common import FormatError
from mafia.elf import ElfImage, physical_address
from fixtures import mutate, write_elf


class ElfTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.path = Path(self.temporary.name) / "constructed.elf"

    def test_retained_function_symbols_are_distinct_from_debug_sections(self) -> None:
        write_elf(self.path)
        elf = ElfImage(self.path)
        report = elf.report()
        self.assertEqual(report["entry_point"], 0x100000)
        self.assertEqual(report["sha256"], hashlib.sha256(self.path.read_bytes()).hexdigest())
        self.assertEqual(report["defined_function_symbols"], 1)
        self.assertEqual(report["debug_sections"], [])
        self.assertEqual({s["name"] for s in report["entry_symbols"]},
                         {"ENTRY_MARKER", "fixture_function"})
        self.assertEqual([s.name for s in elf.functions()], ["fixture_function"])

    def test_real_debug_section_is_reported_separately(self) -> None:
        write_elf(self.path, debug=True)
        self.assertEqual(ElfImage(self.path).report()["debug_sections"], [".debug_info"])

    def test_stripped_elf_with_no_section_table_remains_loadable(self) -> None:
        write_elf(self.path, sectioned=False)
        elf = ElfImage(self.path)
        self.assertEqual(elf.entry_point, 0x100000)
        self.assertEqual(elf.functions(), [])
        self.assertEqual(elf.instruction_survey()["counts"], {})

    def test_bss_is_not_read_as_file_backed_guest_code(self) -> None:
        write_elf(self.path)
        elf = ElfImage(self.path)
        self.assertEqual(len(elf.bytes_at(0x100000, 16)), 16)
        with self.assertRaises(FormatError):
            elf.bytes_at(0x100010, 4)

    def test_cached_and_uncached_ee_aliases_map_to_identical_bytes(self) -> None:
        code = struct.pack("<II", 0x2402002A, 0)
        write_elf(self.path, code=code, base=0x80100000)
        elf = ElfImage(self.path)
        for address in (0x00100000, 0x80100000, 0xA0100000):
            self.assertEqual(elf.bytes_at(address, 8), code)
        self.assertEqual(physical_address(0x70000000), 0x70000000)

    def test_non_function_data_is_excluded_from_instruction_survey(self) -> None:
        # The data word has the SYSCALL encoding, but lies outside the function.
        code = struct.pack("<IIII", 0, 0x03E00008, 0, 0x0000000C)
        write_elf(self.path, code=code, symbols=[("code_only", 0x100000, 12, 2, 2),
                                               ("data_word", 0x10000C, 4, 1, 2)])
        counts = ElfImage(self.path).instruction_survey()["counts"]
        self.assertEqual(counts["words"], 3)
        self.assertNotIn("syscall", counts)

    def test_known_control_flow_encodings_and_aliases_are_counted_once(self) -> None:
        words = [0x0C040008, 0, 0x0100F809, 0, 0x03E00008, 0,
                 0x01200008, 0, 0x0000000C, 0x0000000D,
                 0x3C081000, 0x50000001, 0, 0x70000000]
        code = struct.pack("<" + "I" * len(words), *words)
        write_elf(self.path, code=code, symbols=[("first_name", 0x100000, len(code), 2, 2),
                                               ("alias_name", 0x100000, len(code), 2, 2)])
        survey = ElfImage(self.path).instruction_survey()
        self.assertEqual(survey["counts"], {"words": 14, "jal": 1, "jalr": 1,
                                             "jr": 2, "syscall": 1, "break": 1,
                                             "branch_likely": 1, "mmi": 1})
        self.assertEqual(survey["direct_targets"], [{"address": 0x100020, "references": 1}])
        self.assertEqual([s["pc"] for s in survey["indirect_sites"]], [0x100008, 0x100018])
        candidate = survey["hardware_address_candidates"][0]
        self.assertEqual(candidate["pc"], 0x100028)
        self.assertIn("not proof", candidate["evidence"])

    def test_invalid_function_extents_are_reported_without_scanning_them(self) -> None:
        symbols = [("zero", 0x100000, 0, 2, 2),
                   ("unaligned", 0x100001, 4, 2, 2),
                   ("odd_size", 0x100000, 3, 2, 2),
                   ("unmapped", 0x180000, 4, 2, 2),
                   ("bss_function", 0x100010, 4, 2, 5)]
        write_elf(self.path, symbols=symbols)
        survey = ElfImage(self.path).instruction_survey()
        self.assertEqual(len(survey["invalid_function_extents"]), 5)
        self.assertEqual(survey["counts"], {})

    def test_symbol_extent_must_fit_its_referenced_executable_section(self) -> None:
        meta = write_elf(self.path, symbols=[("outside_text", 0x100008, 8, 2, 2)])
        # PT_LOAD is 16 bytes, but the declared executable section is only 8.
        mutate(self.path, meta["sections"] + 2 * 40 + 20, struct.pack("<I", 8))
        survey = ElfImage(self.path).instruction_survey()
        self.assertEqual(survey["counts"], {})
        self.assertEqual(len(survey["invalid_function_extents"]), 1)

    def test_symbol_and_string_table_corruption_is_rejected(self) -> None:
        for corruption in ("bad_link", "wrong_link_type", "wrong_entry_size", "bad_section", "name_past_end", "unterminated_name"):
            with self.subTest(corruption=corruption):
                meta = write_elf(self.path)
                if corruption == "bad_link":
                    mutate(self.path, meta["sections"] + 4 * 40 + 24, struct.pack("<I", 999))
                elif corruption == "wrong_link_type":
                    mutate(self.path, meta["sections"] + 4 * 40 + 24, struct.pack("<I", 2))
                elif corruption == "wrong_entry_size":
                    mutate(self.path, meta["sections"] + 4 * 40 + 36, struct.pack("<I", 8))
                elif corruption == "bad_section":
                    mutate(self.path, meta["symtab"] + 16 + 14, struct.pack("<H", 8))
                elif corruption == "name_past_end":
                    mutate(self.path, meta["symtab"] + 16, struct.pack("<I", 0xFFFFFFFF))
                else:
                    # Shrink .strtab to include ENTRY_MARKER but omit its NUL.
                    mutate(self.path, meta["sections"] + 3 * 40 + 20, struct.pack("<I", 13))
                with self.assertRaises(FormatError):
                    ElfImage(self.path)

    def test_wrong_elf_class_endianness_machine_or_kind_is_rejected(self) -> None:
        changes = [(0, b"NOPE"), (4, b"\x02"), (5, b"\x02"),
                   (16, struct.pack("<H", 1)), (18, struct.pack("<H", 62)),
                   (20, struct.pack("<I", 2)), (40, struct.pack("<H", 64))]
        for offset, data in changes:
            with self.subTest(offset=offset, data=data):
                write_elf(self.path)
                mutate(self.path, offset, data)
                with self.assertRaises(FormatError):
                    ElfImage(self.path)

    def test_entry_requires_a_complete_file_backed_instruction(self) -> None:
        for length in (1, 2, 3):
            with self.subTest(length=length):
                write_elf(self.path, code=bytes(length))
                with self.assertRaises(FormatError):
                    ElfImage(self.path)

    def test_entry_cannot_be_unaligned_non_executable_unmapped_or_bss(self) -> None:
        for entry, flags in ((0x100001, 7), (0x100000, 6), (0x100010, 7), (0x180000, 7)):
            with self.subTest(entry=entry, flags=flags):
                write_elf(self.path, entry=entry)
                mutate(self.path, 52 + 24, struct.pack("<I", flags))
                with self.assertRaises(FormatError):
                    ElfImage(self.path)

    def test_load_bounds_and_alignment_are_checked(self) -> None:
        changes = [(52 + 4, 0xFFFFFFFF), (52 + 8, 0x02000000),
                   (52 + 16, 0x10000000), (52 + 20, 1),
                   (52 + 20, 0xFFFFFFFF), (52 + 28, 3),
                   (52 + 4, 0x104)]
        for offset, value in changes:
            with self.subTest(offset=offset, value=value):
                write_elf(self.path)
                mutate(self.path, offset, struct.pack("<I", value))
                with self.assertRaises(FormatError):
                    ElfImage(self.path)

    def test_overlapping_physical_load_segments_rejected_even_with_different_alias(self) -> None:
        write_elf(self.path)
        mutate(self.path, 44, struct.pack("<H", 2))
        second = (1, 0x100, 0xA0100000, 0x100000, 16, 16, 6, 16)
        mutate(self.path, 84, struct.pack("<IIIIIIII", *second))
        with self.assertRaises(FormatError):
            ElfImage(self.path)

    def test_program_and_section_tables_cannot_escape_file(self) -> None:
        changes = [(28, struct.pack("<I", 0xFFFFFFF0)),
                   (32, struct.pack("<I", 0xFFFFFFF0)),
                   (44, struct.pack("<H", 0)),
                   (46, struct.pack("<H", 39)),
                   (50, struct.pack("<H", 99))]
        for offset, data in changes:
            with self.subTest(offset=offset):
                write_elf(self.path)
                mutate(self.path, offset, data)
                with self.assertRaises(FormatError):
                    ElfImage(self.path)

    def test_truncated_header_does_not_leak_struct_errors(self) -> None:
        for size in (0, 4, 16, 51):
            with self.subTest(size=size):
                self.path.write_bytes(bytes(size))
                with self.assertRaises(FormatError):
                    ElfImage(self.path)

    def test_report_is_deterministic(self) -> None:
        write_elf(self.path)
        self.assertEqual(ElfImage(self.path).report(), ElfImage(self.path).report())


if __name__ == "__main__":
    unittest.main()
