"""ISO behavior and hostile-input regressions using constructed public data."""

from __future__ import annotations

import builtins
import hashlib
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))

from mafia.common import FormatError, safe_destination
from mafia.iso import IsoImage, lookup_key, parse_system_cnf
from fixtures import BLOCK, dual16, dual32, mutate, pack_records, record, write_iso


class IsoTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.iso_path = self.directory / "constructed.iso"

    def iso(self, entries: list[bytes], payloads: dict[int, bytes] | None = None,
            **kwargs: object) -> Path:
        return write_iso(self.iso_path, entries, payloads, **kwargs)

    def test_boot2_is_read_from_disc_and_not_guessed_from_filename(self) -> None:
        config = b"# a fixture\r\n boot2 = cdrom0:\\BOOT\\SLES_123.45;1\r\nVER = 2.50\r\nVMODE = PAL\r\n"
        inner = pack_records([record(b"\0", 25, BLOCK, 2),
                              record(b"\1", 20, BLOCK, 2),
                              record("SLES_123.45;1", 32, 4)])
        self.iso([record("SYSTEM.CNF;1", 30, len(config)), record("BOOT", 25, BLOCK, 2)],
                 {25: inner, 30: config, 32: b"TEST"})
        with IsoImage(self.iso_path) as disc:
            metadata = parse_system_cnf(disc.read_file("system.cnf"))
            self.assertEqual(metadata["serial"], "SLES-12345")
            self.assertEqual(metadata["region_from_serial"], "Europe")
            self.assertEqual(metadata["fields"]["VMODE"], "PAL")
            self.assertEqual(disc.read_file(metadata["boot2"]), b"TEST")

    def test_unknown_boot_filename_is_accepted_without_invented_serial(self) -> None:
        metadata = parse_system_cnf(b"BOOT2=cdrom0:/UNUSUAL.ELF;1\n")
        self.assertIsNone(metadata["serial"])
        self.assertEqual(metadata["region_from_serial"], "unknown")

    def test_case_slashes_and_versions_lookup_original_bytes(self) -> None:
        payload = b"case independent contents\x00\xff"
        self.iso([record("MiXeD.DAT;1", 31, len(payload))], {31: payload})
        with IsoImage(self.iso_path) as disc:
            for name in ("MIXED.DAT", "mixed.dat;1", r"cdrom0:\mixed.dat;27", "/MiXeD.DaT"):
                with self.subTest(name=name):
                    self.assertEqual(disc.read_file(name), payload)
            self.assertEqual(disc.lookup("mixed.dat").path, "MiXeD.DAT")
            self.assertEqual(disc.lookup("mixed.dat").iso_name, "MiXeD.DAT;1")

    def test_raw_sectors_preserve_padding_and_do_not_read_past_eof(self) -> None:
        block = bytes(range(256)) * 8
        self.iso([], {40: block})
        with IsoImage(self.iso_path) as disc:
            self.assertEqual(disc.read_sectors(40, 1), block)
            self.assertEqual(disc.read_sectors(64, 0), b"")
            for lba, count in ((-1, 1), (0, -1), (64, 1), (63, 2), (2**63, 1)):
                with self.subTest(lba=lba, count=count), self.assertRaises(FormatError):
                    disc.read_sectors(lba, count)

    def test_directory_sector_padding_is_skipped(self) -> None:
        first = record("FIRST.TXT;1", 31, 5)
        second = record("SECOND.TXT;1", 32, 6)
        root = first.ljust(BLOCK, b"\0") + second.ljust(BLOCK, b"\0")
        self.iso([], {31: b"first", 32: b"second"}, root_data=root)
        with IsoImage(self.iso_path) as disc:
            self.assertEqual([e.path for e in disc.entries], ["FIRST.TXT", "SECOND.TXT"])
            self.assertEqual(disc.read_file("second.txt"), b"second")

    def test_multi_extent_file_concatenates_discontiguous_content(self) -> None:
        first = bytes(range(256)) * 8
        last = b"last fragment"
        self.iso([record("LONG.BIN;1", 30, len(first), 0x80),
                  record("LONG.BIN;1", 42, len(last))], {30: first, 42: last})
        with IsoImage(self.iso_path) as disc:
            entry = disc.lookup("long.bin")
            self.assertEqual(entry.size, BLOCK + len(last))
            self.assertEqual(disc.read_file("long.bin"), first + last)
            digest = disc.extract(entry, self.directory / "out")
            self.assertEqual(digest, hashlib.sha256(first + last).hexdigest())

    def test_extended_attribute_sector_is_not_file_data(self) -> None:
        self.iso([record("ATTR.TXT;1", 30, 7, attributes=1)],
                 {30: b"attribute marker", 31: b"content"})
        with IsoImage(self.iso_path) as disc:
            self.assertEqual(disc.read_file("attr.txt"), b"content")

    def test_read_limit_is_enforced_before_allocating_file(self) -> None:
        self.iso([record("DATA.BIN;1", 30, 8)], {30: b"contents"})
        with IsoImage(self.iso_path) as disc, self.assertRaises(FormatError):
            disc.read_file("data.bin", limit=7)

    def test_empty_file_and_missing_file_are_distinct(self) -> None:
        self.iso([record("EMPTY.TXT;1", 30, 0)])
        with IsoImage(self.iso_path) as disc:
            self.assertEqual(disc.read_file("empty.txt"), b"")
            with self.assertRaises(FileNotFoundError):
                disc.read_file("missing.txt")

    def test_non_sector_aligned_or_truncated_image_rejected(self) -> None:
        for data in (b"", bytes(17 * BLOCK), bytes(64 * BLOCK - 1)):
            with self.subTest(size=len(data)):
                self.iso_path.write_bytes(data)
                with self.assertRaises(FormatError):
                    IsoImage(self.iso_path)

    def test_malformed_descriptors_are_rejected(self) -> None:
        mutations = [
            (16 * BLOCK + 1, b"BAD!!"),
            (17 * BLOCK, b"\x02"),
            (16 * BLOCK + 128, dual16(4096)),
            (16 * BLOCK + 80, dual32(65)),
            (16 * BLOCK + 84, (63).to_bytes(4, "big")),
            (16 * BLOCK + 130, (1024).to_bytes(2, "big")),
            (16 * BLOCK + 156 + 25, b"\x00"),
        ]
        for offset, change in mutations:
            with self.subTest(offset=offset, change=change):
                self.iso([])
                mutate(self.iso_path, offset, change)
                with self.assertRaises(FormatError):
                    IsoImage(self.iso_path)

    def test_bad_file_endianness_extent_and_interleaving_are_rejected(self) -> None:
        base = bytearray(record("BAD.BIN;1", 30, 8))
        changes = [(6, (31).to_bytes(4, "big")),
                   (14, (9).to_bytes(4, "big")),
                   (2, dual32(64)), (10, dual32(64 * BLOCK)),
                   (26, b"\x01"), (27, b"\x01"), (28, dual16(2))]
        for offset, value in changes:
            with self.subTest(offset=offset, value=value):
                item = base.copy()
                item[offset:offset + len(value)] = value
                self.iso([bytes(item)])
                with self.assertRaises(FormatError):
                    IsoImage(self.iso_path)

    def test_record_cannot_cross_directory_sector_boundary(self) -> None:
        # Leave 34 bytes in the sector, fewer than the final record requires.
        prefix = b"".join(record(f"F{i:03}", 30, 0) for i in range(53))
        self.assertEqual(len(prefix), 2014)
        malformed = (prefix + record("CROSSING;1", 30, 1)).ljust(2 * BLOCK, b"\0")
        self.iso([], root_data=malformed)
        with self.assertRaises(FormatError):
            IsoImage(self.iso_path)

    def test_cycle_and_directory_alias_are_rejected(self) -> None:
        for records in ([record("LOOP", 20, BLOCK, 2)],
                        [record("A", 25, BLOCK, 2), record("B", 25, BLOCK, 2)]):
            with self.subTest(records=records):
                self.iso(records, {25: pack_records([])})
                with self.assertRaises(FormatError):
                    IsoImage(self.iso_path)

    def test_duplicate_case_or_version_paths_are_rejected(self) -> None:
        for alternate in ("sample.TXT;1", "SAMPLE.TXT;2"):
            with self.subTest(alternate=alternate):
                self.iso([record("SAMPLE.TXT;1", 30, 0), record(alternate, 31, 0)])
                with self.assertRaises(FormatError):
                    IsoImage(self.iso_path)

    def test_broken_multi_extent_chains_are_rejected(self) -> None:
        for entries in ([record("LONG;1", 30, BLOCK, 0x80)],
                        [record("LONG;1", 30, BLOCK, 0x80), record("OTHER;1", 32, 5)],
                        [record("DIRECTORY", 30, BLOCK, 0x82)]):
            with self.subTest(entries=entries):
                self.iso(entries)
                with self.assertRaises(FormatError):
                    IsoImage(self.iso_path)

    def test_malformed_and_traversing_identifiers_are_rejected(self) -> None:
        for name in ("../OUT", "..", "BAD/NAME", "BAD\\NAME", "BAD:NAME", b"\xffNAME"):
            with self.subTest(name=name):
                self.iso([record(name, 30, 1)])
                with self.assertRaises(FormatError):
                    IsoImage(self.iso_path)

    def test_extract_preserves_identical_existing_file_and_rejects_difference(self) -> None:
        payload = b"fixture contents"
        self.iso([record("KEEP.TXT;1", 30, len(payload))], {30: payload})
        out = self.directory / "out"
        with IsoImage(self.iso_path) as disc:
            entry = disc.lookup("keep.txt")
            digest = disc.extract(entry, out)
            inode = (out / "KEEP.TXT").stat().st_ino
            self.assertEqual(disc.extract(entry, out), digest)
            self.assertEqual((out / "KEEP.TXT").stat().st_ino, inode)
            (out / "KEEP.TXT").write_bytes(b"user changes")
            with self.assertRaises(FileExistsError):
                disc.extract(entry, out)
            self.assertEqual((out / "KEEP.TXT").read_bytes(), b"user changes")

    def test_extract_does_not_delete_a_file_created_by_another_writer(self) -> None:
        self.iso([record("RACE.TXT;1", 30, 4)], {30: b"ours"})
        out = self.directory / "out"
        target = out / "RACE.TXT"
        original_open = Path.open

        def another_writer_wins(path: Path, mode: str = "r", *args: object, **kwargs: object):
            if path == target and mode == "xb":
                with builtins.open(target, "wb") as other:
                    other.write(b"other writer")
                raise FileExistsError("another process created the destination")
            return original_open(path, mode, *args, **kwargs)

        with IsoImage(self.iso_path) as disc:
            with mock.patch.object(Path, "open", another_writer_wins):
                with self.assertRaises(FileExistsError):
                    disc.extract(disc.lookup("race.txt"), out)
        self.assertTrue(target.exists(), "no-clobber must not unlink another writer's file")
        self.assertEqual(target.read_bytes(), b"other writer")

    def test_extract_rejects_file_and_parent_symlinks(self) -> None:
        payload = b"ours"
        inner = pack_records([record("DATA.TXT;1", 32, len(payload))])
        self.iso([record("DATA.TXT;1", 30, len(payload)), record("SUBDIR", 25, BLOCK, 2)],
                 {25: inner, 30: payload, 32: payload})
        out = self.directory / "out"
        out.mkdir()
        outside = self.directory / "outside"
        outside.mkdir()
        (outside / "DATA.TXT").write_bytes(b"private")
        (out / "DATA.TXT").symlink_to(outside / "DATA.TXT")
        (out / "SUBDIR").symlink_to(outside, target_is_directory=True)
        with IsoImage(self.iso_path) as disc:
            for name in ("DATA.TXT", "SUBDIR/DATA.TXT"):
                with self.subTest(name=name), self.assertRaises(FormatError):
                    disc.extract(disc.lookup(name), out)
        self.assertEqual((outside / "DATA.TXT").read_bytes(), b"private")

    def test_extract_rejects_dangling_symlink(self) -> None:
        self.iso([record("DATA.TXT;1", 30, 1)], {30: b"x"})
        out = self.directory / "out"
        out.mkdir()
        outside = self.directory / "absent"
        (out / "DATA.TXT").symlink_to(outside)
        with IsoImage(self.iso_path) as disc, self.assertRaises(FormatError):
            disc.extract(disc.lookup("data.txt"), out)
        self.assertFalse(outside.exists())


class PathAndConfigTests(unittest.TestCase):
    def test_lookup_rejects_traversal_even_before_version_separator(self) -> None:
        for path in ("../FILE", "..;1/FILE", ".;1/FILE", ";1", r"cdrom0:\..;1\FILE"):
            with self.subTest(path=path), self.assertRaises(FormatError):
                lookup_key(path)

    def test_extraction_destination_rejects_escape_and_absolute_names(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            for path in ("/ABS", "../OUT", "A/../../OUT", r"C:\OUT", "A//B", "A\x00B"):
                with self.subTest(path=path), self.assertRaises(FormatError):
                    safe_destination(Path(temporary), path)

    def test_invalid_or_duplicate_boot_configuration_fails(self) -> None:
        for data in (b"", b"BOOT=cdrom0:\\FILE;1", b"BOOT2=host:FILE", b"BOOT2=/FILE", b"BOOT2=cdrom0:/../FILE", b"BOOT2=cdrom0:/FILE\nboot2=cdrom0:/OTHER", b"BOOT2=\xff"):
            with self.subTest(data=data), self.assertRaises(FormatError):
                parse_system_cnf(data)


if __name__ == "__main__":
    unittest.main()
