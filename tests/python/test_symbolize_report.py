"""Failure evidence keeps branch sources separate from targets; synthetic ELF only."""

from __future__ import annotations

import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))

from mafia.common import FormatError
from mafia.elf import ElfImage
import symbolize_report
from fixtures import write_elf


class SymbolizeReportTests(unittest.TestCase):
    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.elf_path = self.root / "fixture.elf"
        write_elf(self.elf_path, code=bytes(64), symbols=[
            ("source_function", 0x100000, 16, 2, 2),
            ("target_function", 0x100010, 16, 2, 2),
            ("boot_marker", 0x100020, 0, 0, 2),
        ])
        self.elf = ElfImage(self.elf_path)
        self.profile = {
            "profile_id": "synthetic-report-fixture", "elf_sha256": self.elf.sha256,
            "elf_bytes": len(self.elf.data), "entry_point": self.elf.entry_point,
            "policy": {"missing_function": "stop", "unknown_syscall": "stop",
                       "unsupported_instruction": "trap", "automatic_instruction_patches": False,
                       "allow_interpreter_fallback": False, "allow_jit_fallback": False},
            "verified_untyped_code_ranges": [{"name": "boot_marker", "start": 0x100020, "end": 0x100030}],
        }
        (self.root / "profiles").mkdir()
        (self.root / "profiles/fixture.json").write_text(json.dumps(self.profile))
        self.report = {
            "elf_sha256": self.elf.sha256, "stage": "guest-execution", "status": "missing-dispatch",
            "last_guest_pc": 0, "last_guest_branch_pc": 0x100008,
            "symbol": None, "entry_executed": True, "gameplay_verified": False,
        }

    def resolve(self, **changes) -> dict:
        return symbolize_report.symbolize({**self.report, **changes}, self.elf, self.profile)

    def cli(self, *arguments: str) -> tuple[int, str, str]:
        stdout, stderr = io.StringIO(), io.StringIO()
        with mock.patch.object(symbolize_report, "ROOT", self.root), \
                mock.patch.object(sys, "argv", ["symbolize_report.py", *arguments]), \
                contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
            result = symbolize_report.main()
        return result, stdout.getvalue(), stderr.getvalue()

    def test_zero_target_keeps_its_null_symbol_while_resolving_branch_source(self) -> None:
        before = dict(self.report)
        result = symbolize_report.symbolize(self.report, self.elf, self.profile)
        self.assertIsNone(result["symbol"])
        self.assertEqual(result["branch_source_symbol"], [
            {"name": "source_function", "start": 0x100000, "end": 0x100010}])
        self.assertEqual(result["last_guest_pc"], 0)
        self.assertFalse(result["gameplay_verified"])
        self.assertEqual(self.report, before)

    def test_nonzero_target_and_branch_source_resolve_independently(self) -> None:
        result = self.resolve(last_guest_pc=0x100014)
        self.assertEqual([item["name"] for item in result["symbol"]], ["target_function"])
        self.assertEqual([item["name"] for item in result["branch_source_symbol"]], ["source_function"])

    def test_branch_source_can_use_a_verified_untyped_boot_range(self) -> None:
        result = self.resolve(last_guest_branch_pc=0x10002C)
        self.assertEqual(result["branch_source_symbol"], [
            {"name": "boot_marker", "start": 0x100020, "end": 0x100030}])
        self.assertIsNone(self.resolve(last_guest_branch_pc=0x100030)["branch_source_symbol"])

    def test_legacy_report_without_branch_pc_does_not_invent_source_evidence(self) -> None:
        report = dict(self.report)
        del report["last_guest_branch_pc"]
        result = symbolize_report.symbolize(report, self.elf, self.profile)
        self.assertNotIn("last_guest_branch_pc", result)
        self.assertIsNone(result["branch_source_symbol"])

    def test_unmapped_addresses_replace_stale_symbol_names_with_null(self) -> None:
        result = self.resolve(last_guest_pc=0xFFFFFFFC, last_guest_branch_pc=0,
                              symbol=[{"name": "stale target"}], branch_source_symbol=[{"name": "stale source"}])
        self.assertIsNone(result["symbol"])
        self.assertIsNone(result["branch_source_symbol"])

    def test_malformed_addresses_and_missing_guest_pc_are_rejected(self) -> None:
        for field in ("last_guest_pc", "last_guest_branch_pc"):
            for value in (None, True, False, -1, 0x100000000, "0x100008", 1048584.0):
                with self.subTest(field=field, value=value), self.assertRaises(FormatError):
                    self.resolve(**{field: value})
        report = dict(self.report)
        del report["last_guest_pc"]
        with self.assertRaises(FormatError):
            symbolize_report.symbolize(report, self.elf, self.profile)

    def test_wrong_elf_identity_or_non_object_report_is_rejected(self) -> None:
        with self.assertRaisesRegex(FormatError, "different builds"):
            self.resolve(elf_sha256="0" * 64)
        with self.assertRaisesRegex(FormatError, "JSON object"):
            symbolize_report.symbolize([], self.elf, self.profile)

    def test_cli_labels_target_and_source_without_mutating_input_report(self) -> None:
        report_path = self.root / "runtime.json"
        report_path.write_text(json.dumps(self.report))
        original = report_path.read_bytes()
        result, stdout, stderr = self.cli(str(report_path), "--elf", str(self.elf_path))
        self.assertEqual(result, 0, stderr)
        self.assertIn("target 0x00000000: no symbol covers this address", stdout)
        self.assertIn("last branch source 0x00100008: source_function", stdout)
        written = json.loads((self.root / "runtime.symbolized.json").read_text())
        self.assertIsNone(written["symbol"])
        self.assertEqual(written["branch_source_symbol"][0]["name"], "source_function")
        self.assertEqual(report_path.read_bytes(), original)

    def test_cli_rejects_output_that_would_replace_original_elf(self) -> None:
        report_path = self.root / "runtime.json"
        report_path.write_text(json.dumps(self.report))
        original = self.elf_path.read_bytes()
        result, _, stderr = self.cli(str(report_path), "--elf", str(self.elf_path),
                                    "--output", str(self.elf_path))
        self.assertEqual(result, 1)
        self.assertIn("must not replace the ELF", stderr)
        self.assertEqual(self.elf_path.read_bytes(), original)


if __name__ == "__main__":
    unittest.main()
