"""Tool provenance and CMake output-layout tests without building compilers."""

from __future__ import annotations

import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))

from mafia.common import FormatError
from mafia.toolchain import executable_path, stamp_built_tools, verified_tools


class ToolchainTests(unittest.TestCase):
    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.source = self.root / "source"
        self.source.mkdir()
        self.build = self.root / "build"
        self.build.mkdir()
        self.commit = "1" * 40
        self.patch = self.root / "0001-test.patch"
        self.patch.write_text("synthetic patch fixture\n")
        self.digests = {self.patch.name: "a" * 64}
        self.verifier = mock.patch("mafia.toolchain.verify", return_value=self.digests)
        self.verify = self.verifier.start()
        self.addCleanup(self.verifier.stop)
        self.cache()
        self.binaries()

    def cache(self, *, multi: bool = False, configuration: str = "Release") -> None:
        text = f"CMAKE_HOME_DIRECTORY:INTERNAL={self.source}\n"
        if multi:
            text += "CMAKE_GENERATOR:INTERNAL=Xcode\nCMAKE_CONFIGURATION_TYPES:STRING=Debug;Release;RelWithDebInfo\n"
        else:
            text += f"CMAKE_GENERATOR:INTERNAL=Ninja\nCMAKE_BUILD_TYPE:STRING={configuration}\n"
        (self.build / "CMakeCache.txt").write_text(text)

    def binaries(self, *, configuration: str | None = None) -> tuple[Path, Path]:
        paths = []
        for component, name in (("ps2xAnalyzer", "ps2_analyzer"), ("ps2xRecomp", "ps2_recomp")):
            directory = self.build / component
            if configuration:
                directory /= configuration
            directory.mkdir(exist_ok=True, parents=True)
            target = directory / name
            target.write_text(f"#!/bin/sh\n# {name} {configuration or 'single'} synthetic executable\nexit 0\n")
            target.chmod(0o755)
            paths.append(target)
        return paths[0], paths[1]

    def stamp(self, configuration: str = "Release") -> dict:
        return stamp_built_tools(self.source, self.build, self.commit, [self.patch], configuration)

    def checked(self, commit: str | None = None) -> tuple[Path, Path]:
        return verified_tools(self.source, self.build, commit or self.commit, [self.patch])

    def alter_stamp(self, change) -> None:
        path = self.build / "mafia-toolchain.json"
        data = json.loads(path.read_text())
        change(data)
        path.write_text(json.dumps(data))

    def test_single_configuration_ignores_stale_multi_configuration_outputs(self) -> None:
        self.binaries(configuration="Release")
        expected = self.build / "ps2xRecomp/ps2_recomp"
        self.assertEqual(executable_path(self.build, "ps2xRecomp", "ps2_recomp", "Release"), expected)
        self.stamp()
        self.assertEqual(self.checked()[1], expected)

    def test_multi_configuration_ignores_stale_single_configuration_outputs(self) -> None:
        self.cache(multi=True)
        expected = self.binaries(configuration="Release")
        self.binaries(configuration="Debug")
        self.assertEqual(executable_path(self.build, "ps2xRecomp", "ps2_recomp", "Release"), expected[1])
        self.stamp()
        self.assertEqual(self.checked(), expected)

    def test_missing_selected_configuration_cannot_fall_back_to_other_output(self) -> None:
        self.cache(multi=True)
        self.binaries(configuration="Debug")
        with self.assertRaises(FormatError):
            executable_path(self.build, "ps2xRecomp", "ps2_recomp", "Release")

    def test_missing_cache_or_selected_executable_is_rejected(self) -> None:
        cache = self.build / "CMakeCache.txt"
        cache.unlink()
        with self.assertRaises(FormatError):
            executable_path(self.build, "ps2xAnalyzer", "ps2_analyzer", "Release")
        self.cache()
        (self.build / "ps2xAnalyzer/ps2_analyzer").unlink()
        with self.assertRaises(FormatError):
            self.stamp()
        self.assertFalse((self.build / "mafia-toolchain.json").exists())

    def test_successful_stamp_round_trips_binary_source_and_cache_identity(self) -> None:
        first = self.stamp()
        self.assertEqual(first["upstream_commit"], self.commit)
        self.assertEqual(first["patches"], self.digests)
        self.assertEqual(first["configuration"], "Release")
        expected = (self.build / "ps2xAnalyzer/ps2_analyzer", self.build / "ps2xRecomp/ps2_recomp")
        self.assertEqual(self.checked(), expected)
        self.assertEqual(self.stamp(), first)

    def test_missing_stamp_rejects_even_present_executables(self) -> None:
        with self.assertRaises(FormatError):
            self.checked()

    def test_changed_analyzer_or_recompiler_is_rejected(self) -> None:
        for component, name in (("ps2xAnalyzer", "ps2_analyzer"), ("ps2xRecomp", "ps2_recomp")):
            with self.subTest(component=component):
                self.binaries()
                self.stamp()
                with (self.build / component / name).open("a") as file:
                    file.write("# changed after build\n")
                with self.assertRaises(FormatError):
                    self.checked()

    def test_changed_revision_or_patch_set_requires_rebuild(self) -> None:
        self.stamp()
        with self.assertRaises(FormatError):
            self.checked("2" * 40)
        for changed in ({self.patch.name: "b" * 64}, {}, {"0002-different.patch": "a" * 64}):
            with self.subTest(patches=changed):
                self.verify.return_value = changed
                with self.assertRaises(FormatError):
                    self.checked()

    def test_source_verification_failure_is_not_hidden_by_an_existing_stamp(self) -> None:
        self.stamp()
        self.verify.side_effect = FormatError("source differs from reviewed tree")
        with self.assertRaisesRegex(FormatError, "source differs"):
            self.checked()

    def test_failed_source_verification_cannot_create_a_build_stamp(self) -> None:
        self.verify.side_effect = FormatError("source differs from reviewed tree")
        with self.assertRaises(FormatError):
            self.stamp()
        self.assertFalse((self.build / "mafia-toolchain.json").exists())

    def test_modified_cmake_configuration_invalidates_stamp(self) -> None:
        self.stamp()
        with (self.build / "CMakeCache.txt").open("a") as file:
            file.write("CMAKE_CXX_FLAGS:STRING=-ffast-math\n")
        with self.assertRaises(FormatError):
            self.checked()

    def test_single_configuration_cannot_be_stamped_under_a_different_label(self) -> None:
        self.cache(configuration="Debug")
        with self.assertRaises(FormatError):
            self.stamp("Release")

    def test_recorded_configuration_and_executable_layout_cannot_disagree(self) -> None:
        for multi in (False, True):
            with self.subTest(multi=multi):
                self.cache(multi=multi)
                if multi:
                    self.binaries(configuration="Release")
                    self.binaries(configuration="Debug")
                self.stamp()
                self.alter_stamp(lambda stamp: stamp.update(configuration="Debug"))
                with self.assertRaises(FormatError):
                    self.checked()

    def test_unsafe_stamped_paths_are_rejected(self) -> None:
        for path in (str(self.source / "outside"), "../outside", ""):
            with self.subTest(path=path):
                self.stamp()
                self.alter_stamp(lambda stamp: stamp["executables"]["analyzer"].update(path=path))
                with self.assertRaises(FormatError):
                    self.checked()

    def test_executable_symlink_is_rejected_even_when_bytes_match(self) -> None:
        self.stamp()
        target = self.build / "ps2xAnalyzer/ps2_analyzer"
        external = self.root / "same-analyzer"
        external.write_bytes(target.read_bytes())
        target.unlink()
        target.symlink_to(external)
        with self.assertRaises(FormatError):
            self.checked()


if __name__ == "__main__":
    unittest.main()
