"""Exact executable identity and strict recompiler configuration contracts."""

from __future__ import annotations

import copy
import json
from pathlib import Path
import sys
import tempfile
import tomllib
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))

from mafia.common import FormatError
from mafia.elf import ElfImage
from mafia.profile import recompiler_config, select_profile
from fixtures import write_elf


class ProfileTests(unittest.TestCase):
    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.elf_path = self.root / "arbitrary-name.bin"
        write_elf(self.elf_path)
        self.elf = ElfImage(self.elf_path)
        self.profiles = self.root / "profiles"
        self.profiles.mkdir()
        self.profile = {
            "profile_id": "public-test-build", "elf_sha256": self.elf.sha256,
            "elf_bytes": len(self.elf.data), "entry_point": self.elf.entry_point,
            "entry_points": ["ENTRY_MARKER@0x00100000"],
            "stubs": [], "instruction_patches": [],
            "policy": {"missing_function": "stop", "unknown_syscall": "stop",
                       "unsupported_instruction": "trap", "automatic_instruction_patches": False,
                       "allow_interpreter_fallback": False, "allow_jit_fallback": False},
        }

    def save(self, profile: dict | None = None, name: str = "test.json") -> None:
        (self.profiles / name).write_text(json.dumps(self.profile if profile is None else profile))

    def test_selects_by_exact_bytes_despite_arbitrary_elf_filename(self) -> None:
        self.save()
        self.assertEqual(select_profile(self.elf, self.profiles), self.profile)

    def test_wrong_hash_and_ambiguous_hash_matches_are_rejected(self) -> None:
        with self.assertRaises(FormatError):
            select_profile(self.elf, self.profiles)
        wrong = {**self.profile, "elf_sha256": "0" * 64}
        self.save(wrong)
        with self.assertRaises(FormatError):
            select_profile(self.elf, self.profiles)
        self.save()
        self.save(name="duplicate.json")
        with self.assertRaises(FormatError):
            select_profile(self.elf, self.profiles)

    def test_matching_hash_does_not_override_wrong_length_or_entry(self) -> None:
        for key in ("elf_bytes", "entry_point"):
            with self.subTest(key=key):
                profile = {**self.profile, key: self.profile[key] + 4}
                self.save(profile)
                with self.assertRaises(FormatError):
                    select_profile(self.elf, self.profiles)

    def test_every_fail_closed_policy_is_mandatory(self) -> None:
        for key in self.profile["policy"]:
            for remove in (False, True):
                with self.subTest(key=key, missing=remove):
                    profile = copy.deepcopy(self.profile)
                    if remove:
                        del profile["policy"][key]
                    else:
                        profile["policy"][key] = "continue" if isinstance(profile["policy"][key], str) else True
                    self.save(profile)
                    with self.assertRaises(FormatError):
                        select_profile(self.elf, self.profiles)

    def test_function_substitution_and_instruction_patches_are_rejected(self) -> None:
        for key, value in (("stubs", ["ret0@0x00100000"]),
                           ("instruction_patches", [{"address": 0x100000, "value": 0}])):
            with self.subTest(key=key):
                self.save({**self.profile, key: value})
                with self.assertRaises(FormatError):
                    select_profile(self.elf, self.profiles)

    def test_profile_identifier_cannot_be_a_path(self) -> None:
        for name in ("../escape", "C:\\escape", "bad\nname", ""):
            with self.subTest(name=name):
                self.save({**self.profile, "profile_id": name})
                with self.assertRaises(FormatError):
                    select_profile(self.elf, self.profiles)

    def test_recompiler_config_disables_patching_and_automatic_native_stubs(self) -> None:
        parsed = tomllib.loads(recompiler_config(self.elf_path, self.root / "generated", self.profile))
        general = parsed["general"]
        for flag in ("patch_syscalls", "patch_cop0", "patch_cache", "auto_bind_runtime_calls"):
            self.assertIs(general[flag], False)
        self.assertEqual(general["stubs"], [])
        self.assertEqual(general["skip"], [])
        self.assertEqual(parsed["patches"]["instructions"], [])
        self.assertEqual(general["entry_points"], self.profile["entry_points"])

    def test_toml_paths_round_trip_quotes_backslashes_and_non_bmp_unicode(self) -> None:
        path = self.root / 'Panda 🐼 "test"\\folder' / "input.elf"
        output = self.root / "výstup 🦊"
        functions = self.root / "map 🐼.csv"
        parsed = tomllib.loads(recompiler_config(path, output, self.profile, functions))["general"]
        self.assertEqual(parsed["input"], str(path.resolve()))
        self.assertEqual(parsed["output"], str(output.resolve()))
        self.assertEqual(parsed["ghidra_output"], str(functions.resolve()))


if __name__ == "__main__":
    unittest.main()
