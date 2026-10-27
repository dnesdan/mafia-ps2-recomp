"""End-to-end preparation and generated-source gates with synthetic inputs."""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

TOOLS = Path(__file__).resolve().parents[2] / "tools"
sys.path.insert(0, str(TOOLS))

from mafia.common import FormatError
from mafia.generation import inspect_generated
from prepare_iso import prepare
from fixtures import record, write_elf, write_iso


class PrepareTests(unittest.TestCase):
    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.iso = self.root / "input.iso"
        self.output = self.root / "work"
        self.elf = self.root / "fixture.elf"
        write_elf(self.elf)
        cnf = b"BOOT2=cdrom0:\\BOOT.ELF;1\nVMODE=NTSC\n"
        write_iso(self.iso,
                  [record("SYSTEM.CNF;1", 30, len(cnf)),
                   record("BOOT.ELF;1", 32, self.elf.stat().st_size),
                   record("EXTRA.DAT;1", 35, 5)],
                  {30: cnf, 32: self.elf.read_bytes(), 35: b"extra"})

    def test_all_extraction_reports_correct_hashes_and_is_reproducible(self) -> None:
        report = prepare(self.iso, self.output, "all")
        self.assertTrue(report["complete_extraction"])
        self.assertEqual(report["retained_files"], 3)
        self.assertEqual(report["iso_sha256"], hashlib.sha256(self.iso.read_bytes()).hexdigest())
        self.assertEqual(report["elf_sha256"], hashlib.sha256(self.elf.read_bytes()).hexdigest())
        before = (self.output / "disc.json").read_bytes()
        self.assertEqual(prepare(self.iso, self.output, "all"), report)
        self.assertEqual((self.output / "disc.json").read_bytes(), before)

    def test_boot_only_extraction_does_not_claim_complete_game_data(self) -> None:
        report = prepare(self.iso, self.output, "boot")
        self.assertFalse(report["complete_extraction"])
        self.assertEqual(report["file_count"], 3)
        self.assertEqual(report["retained_files"], 2)
        extra = next(item for item in report["files"] if item["path"] == "EXTRA.DAT")
        self.assertFalse(extra["retained"])
        self.assertIsNone(extra["sha256"])
        self.assertFalse((self.output / "extracted/EXTRA.DAT").exists())

    def test_report_path_cannot_overwrite_original_iso(self) -> None:
        self.output.mkdir()
        source = self.output / "disc.json"
        original = self.iso.read_bytes()
        source.write_bytes(original)
        with self.assertRaises(FormatError):
            prepare(source, self.output, "all")
        self.assertEqual(source.read_bytes(), original)

    def test_input_inside_extraction_tree_is_rejected_without_mutation(self) -> None:
        source = self.output / "extracted/source.iso"
        source.parent.mkdir(parents=True)
        source.write_bytes(self.iso.read_bytes())
        with self.assertRaises(FormatError):
            prepare(source, self.output, "boot")
        self.assertEqual(source.read_bytes(), self.iso.read_bytes())

    def test_resume_does_not_require_space_for_already_existing_identical_files(self) -> None:
        first = prepare(self.iso, self.output, "all")
        with mock.patch("prepare_iso.shutil.disk_usage", return_value=SimpleNamespace(free=16 * 1024 * 1024 + 1)):
            self.assertEqual(prepare(self.iso, self.output, "all"), first)

    def test_insufficient_space_fails_before_extracting_payloads(self) -> None:
        with mock.patch("prepare_iso.shutil.disk_usage", return_value=SimpleNamespace(free=1)):
            with self.assertRaises(FormatError):
                prepare(self.iso, self.output, "all")
        self.assertFalse((self.output / "extracted/BOOT.ELF").exists())

    def test_repeated_prepare_does_not_overwrite_locally_modified_files(self) -> None:
        prepare(self.iso, self.output, "all")
        target = self.output / "extracted/EXTRA.DAT"
        target.write_bytes(b"user edit")
        with self.assertRaises(FileExistsError):
            prepare(self.iso, self.output, "all")
        self.assertEqual(target.read_bytes(), b"user edit")

    def test_cli_accepts_iso_from_environment(self) -> None:
        process = subprocess.run([sys.executable, str(TOOLS / "prepare_iso.py"),
                                  "--extract", "boot", "--output", str(self.output)],
                                 env={**os.environ, "MAFIA_PS2_ISO": str(self.iso)},
                                 text=True, capture_output=True)
        self.assertEqual(process.returncode, 0, process.stderr)
        self.assertFalse(json.loads((self.output / "disc.json").read_text())["complete_extraction"])


class GeneratedAuditTests(unittest.TestCase):
    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.generated = self.root / "generated"
        self.generated.mkdir()
        for name in ("ps2_recompiled_functions.h", "ps2_recompiled_stubs.h"):
            (self.generated / name).write_text("#pragma once\n")
        (self.generated / "register_functions.cpp").write_text("void register_functions() {}\n")
        self.unit = self.generated / "fixture.cpp"
        self.unit.write_text("void fixture() {}\n")

    def audit(self, body: str) -> dict:
        self.unit.write_text(body)
        return inspect_generated(self.generated)

    def test_manifest_covers_every_source_and_header_exactly(self) -> None:
        (self.generated / "helper.hpp").write_text("#pragma once\n// synthetic helper\n")
        report = inspect_generated(self.generated)
        manifest = (self.generated / "sources.sha256").read_bytes()
        self.assertEqual(report["sources_manifest_sha256"], hashlib.sha256(manifest).hexdigest())
        entries = [line.split("  ", 1) for line in manifest.decode().splitlines()]
        names = [name for _, name in entries]
        self.assertEqual(names, sorted(names))
        self.assertEqual(len(entries), 5)
        for digest, name in entries:
            self.assertEqual(digest, hashlib.sha256((self.generated / name).read_bytes()).hexdigest())
        self.assertEqual(inspect_generated(self.generated), report)
        self.unit.write_text("void fixture() { return; }\n")
        changed = inspect_generated(self.generated)
        self.assertNotEqual(changed["source_tree_sha256"], report["source_tree_sha256"])
        self.assertNotEqual(changed["sources_manifest_sha256"], report["sources_manifest_sha256"])

    def test_missing_registration_and_empty_function_output_are_rejected(self) -> None:
        (self.generated / "register_functions.cpp").unlink()
        with self.assertRaises(FormatError):
            inspect_generated(self.generated)
        (self.generated / "register_functions.cpp").write_text("void register_functions() {}\n")
        self.unit.unlink()
        with self.assertRaises(FormatError):
            inspect_generated(self.generated)

    def test_native_substitutions_are_rejected_including_multiline_references(self) -> None:
        for body in ("ps2_stubs::malloc(rdram, ctx, runtime);", "ps2_stubs::\n sceCdRead(rdram, ctx, runtime);",
                     "ps2_syscalls::FlushCache(rdram, ctx, runtime);",
                     "PS2_REGISTER_GAME_OVERRIDE(thing)", "registerDescriptor\n(descriptor);",
                     "ret0(ctx);", "ret1(ctx);", "reta0(ctx);", "auto handler = &ps2_stubs::scePadRead;"):
            with self.subTest(body=body):
                report = self.audit(body)
                self.assertGreater(report["prohibited_substitutions"], 0)

    def test_comments_and_string_literals_do_not_invent_native_calls(self) -> None:
        report = self.audit('// ps2_stubs::malloc(ctx);\n/* registerDescriptor(x); */\n'
                            'const char* sample = "ps2_stubs::scePadRead(ctx);";\n'
                            '// Unsupported instructions are trapped by the emitter.\n')
        self.assertEqual(report["prohibited_substitutions"], 0)
        self.assertEqual(report["unresolved_lines"], 0)

    def test_unhandled_throw_is_an_explicit_trap(self) -> None:
        report = self.audit('throw std::runtime_error("Unhandled opcode at fixture PC");\n')
        self.assertEqual(report["trap_lines"], 1)
        self.assertEqual(report["silent_unsupported_lines"], 0)
        self.assertEqual(report["unresolved_lines"], 1)

    def test_runtime_bounds_guard_is_a_trap_without_an_unresolved_instruction(self) -> None:
        report = self.audit('if (index >= size) throw std::runtime_error("jump table bounds");\n')
        self.assertEqual(report["trap_lines"], 1)
        self.assertEqual(report["unresolved_lines"], 0)
        self.assertEqual(report["silent_unsupported_lines"], 0)

    def test_multiline_throw_and_immediately_preceding_marker_are_not_silent(self) -> None:
        report = self.audit('// Unimplemented COP0 register 7\nthrow std::runtime_error(\n'
                            '  "Unhandled COP0 register 7");\n')
        self.assertEqual(report["trap_lines"], 1)
        self.assertEqual(report["silent_unsupported_lines"], 0)

    def test_zero_and_comment_only_placeholders_are_silent_unsupported(self) -> None:
        for body in ("SET_GPR_U32(ctx, 9, 0); // Unimplemented FCR10\n",
                     "// Unimplemented MTC0 to COP0 7\nSET_GPR_U32(ctx, 1, 2);\n",
                     "// Unknown instruction: fixture opcode\n",
                     "// Unimplemented CFC2 VU control register: 29\n"):
            with self.subTest(body=body):
                report = self.audit(body)
                self.assertEqual(report["silent_unsupported_lines"], 1)
                self.assertEqual(report["trap_lines"], 0)

    def test_unrelated_later_throw_cannot_hide_an_earlier_zero_placeholder(self) -> None:
        report = self.audit('SET_GPR_U32(ctx, 9, 0); // Unimplemented FCR10\n'
                            'throw std::runtime_error("Unhandled next instruction");\n')
        self.assertEqual(report["silent_unsupported_lines"], 1)
        self.assertEqual(report["trap_lines"], 1)

    def test_conditional_throw_cannot_hide_a_silent_placeholder_on_the_same_line(self) -> None:
        report = self.audit('if (false) throw std::runtime_error("guard"); '
                            'SET_GPR_U32(ctx, 9, 0); // Unimplemented FCR7\n')
        self.assertEqual(report["silent_unsupported_lines"], 1)

    def test_taking_todo_handler_address_is_not_an_executed_trap(self) -> None:
        report = self.audit('auto callback = &ps2_stubs::TODO; // Unimplemented instruction\n')
        self.assertEqual(report["trap_lines"], 0)
        self.assertGreater(report["prohibited_substitutions"], 0)
        self.assertEqual(report["silent_unsupported_lines"], 1)

    def test_strict_todo_handler_is_tracked_as_trap_and_other_stubs_stay_forbidden(self) -> None:
        for body in ('ps2_stubs::TODO_NAMED("Unimplemented fixture", rdram, ctx, runtime);\n',
                     'ps2_stubs::TODO_NAMED(\n"Unimplemented fixture", rdram, ctx, runtime);\n'):
            with self.subTest(body=body):
                report = self.audit(body)
                self.assertEqual(report["prohibited_substitutions"], 0)
                self.assertEqual(report["trap_lines"], 1)
                self.assertEqual(report["silent_unsupported_lines"], 0)

    def test_manifest_names_match_the_native_cmake_contract(self) -> None:
        (self.generated / "ambiguous name.hpp").write_text("#pragma once\n")
        with self.assertRaises(FormatError):
            inspect_generated(self.generated)

    def test_symlink_sources_are_rejected(self) -> None:
        external = self.root / "external.cpp"
        external.write_text("void external() {}\n")
        self.unit.unlink()
        self.unit.symlink_to(external)
        with self.assertRaises(FormatError):
            inspect_generated(self.generated)


if __name__ == "__main__":
    unittest.main()
