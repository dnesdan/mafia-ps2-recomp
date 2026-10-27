"""Exercise dependency provenance with disposable, entirely synthetic Git trees."""

from __future__ import annotations

import difflib
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))

from mafia.common import FormatError
from mafia.upstream import apply_reviewed_patches, checkout, verify


class UpstreamVerificationTests(unittest.TestCase):
    relative = "ps2xRuntime/src/lib/Kernel/Core.cpp"
    baseline = "int fixture_value = 1;\nint fixture_other = 2;\n"

    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory(prefix="mafia-git-fixture-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.source = self.root / "source"
        self.source.mkdir()
        self.git("init", "--quiet")
        self.file = self.source / self.relative
        self.file.parent.mkdir(parents=True)
        self.file.write_text(self.baseline)
        (self.source / ".gitignore").write_text("build/\n.audit*/\n")
        self.git("add", ".")
        self.git("-c", "user.name=Synthetic Fixture", "-c", "user.email=fixture@invalid",
                 "commit", "--quiet", "-m", "Constructed test source")
        self.commit = self.git("rev-parse", "HEAD").stdout.strip()

    def git(self, *args: str, check: bool = True) -> subprocess.CompletedProcess:
        return subprocess.run(["git", "-C", str(self.source), *args], check=check,
                              text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    def patch(self, name: str, before: str, after: str) -> Path:
        path = self.root / name
        path.write_text("".join(difflib.unified_diff(
            before.splitlines(keepends=True), after.splitlines(keepends=True),
            fromfile="a/" + self.relative, tofile="b/" + self.relative)))
        return path

    def test_pristine_tree_and_non_input_build_outputs_are_accepted(self) -> None:
        output = self.source / ".audit-build" / "output.log"
        output.parent.mkdir()
        output.write_text("synthetic build output")
        self.assertEqual(verify(self.source, self.commit, []), {})

    def test_sequential_patches_apply_once_and_leave_the_real_index_unchanged(self) -> None:
        first = self.baseline.replace("value = 1", "value = 3")
        second = first.replace("other = 2", "other = 4")
        patches = [self.patch("0001.patch", self.baseline, first),
                   self.patch("0002.patch", first, second)]
        original_index = (self.source / ".git/index").read_bytes()
        apply_reviewed_patches(self.source, self.commit, patches)
        self.assertEqual(self.file.read_text(), second)
        expected = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in patches}
        self.assertEqual(verify(self.source, self.commit, patches), expected)
        apply_reviewed_patches(self.source, self.commit, patches)
        self.assertEqual(self.file.read_text(), second)
        self.assertEqual((self.source / ".git/index").read_bytes(), original_index)

    def test_wrong_revision_is_rejected_without_modifying_files(self) -> None:
        with self.assertRaises(FormatError):
            verify(self.source, "0" * 40, [])
        with self.assertRaises(FormatError):
            checkout(self.source, "unused-local-fixture-url", "0" * 40)
        self.assertEqual(self.file.read_text(), self.baseline)
        self.assertEqual(self.git("rev-parse", "HEAD").stdout.strip(), self.commit)

    def test_local_tracked_edits_are_rejected_and_preserved(self) -> None:
        patch = self.patch("change.patch", self.baseline, self.baseline.replace("1", "3"))
        edited = "user-owned fixture changes\n"
        self.file.write_text(edited)
        with self.assertRaises(FormatError):
            apply_reviewed_patches(self.source, self.commit, [patch])
        self.assertEqual(self.file.read_text(), edited)

    def test_staged_edits_and_real_index_are_preserved_on_rejection(self) -> None:
        self.file.write_text("staged fixture changes\n")
        self.git("add", self.relative)
        staged_index = (self.source / ".git/index").read_bytes()
        with self.assertRaises(FormatError):
            verify(self.source, self.commit, [])
        self.assertEqual((self.source / ".git/index").read_bytes(), staged_index)
        self.assertEqual(self.git("show", ":" + self.relative).stdout, "staged fixture changes\n")

    def test_partly_applied_patch_stack_is_not_silently_repaired(self) -> None:
        first = self.baseline.replace("value = 1", "value = 3")
        second = first.replace("other = 2", "other = 4")
        patches = [self.patch("first.patch", self.baseline, first),
                   self.patch("second.patch", first, second)]
        self.file.write_text(first)
        with self.assertRaises(FormatError):
            apply_reviewed_patches(self.source, self.commit, patches)
        self.assertEqual(self.file.read_text(), first)

    def test_untracked_compilable_source_is_rejected(self) -> None:
        (self.file.parent / "Additional.cpp").write_text("void additional() {}\n")
        with self.assertRaises(FormatError):
            verify(self.source, self.commit, [])

    def test_gitignored_source_is_still_rejected_when_recursive_cmake_glob_can_compile_it(self) -> None:
        extra = self.file.parent / "build" / "Additional.cpp"
        extra.parent.mkdir()
        extra.write_text("void additional() {}\n")
        self.assertEqual(self.git("check-ignore", str(extra)).returncode, 0)
        with self.assertRaises(FormatError):
            verify(self.source, self.commit, [])

    def test_checkout_fetches_exact_local_commit_and_is_detached(self) -> None:
        target = self.root / "dependency"
        checkout(target, str(self.source), self.commit)
        actual = subprocess.check_output(["git", "-C", str(target), "rev-parse", "HEAD"], text=True).strip()
        self.assertEqual(actual, self.commit)
        symbolic = subprocess.run(["git", "-C", str(target), "symbolic-ref", "-q", "HEAD"],
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.assertNotEqual(symbolic.returncode, 0)


if __name__ == "__main__":
    unittest.main()
