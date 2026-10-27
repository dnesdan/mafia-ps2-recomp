"""Fetch exact revisions and verify local patches without resetting user changes."""

from __future__ import annotations

from contextlib import contextmanager
import json
import os
from pathlib import Path
import subprocess
import tempfile

from .common import FormatError, sha256_file


def git(source: Path, *args: str, env: dict | None = None, check: bool = True,
        input_text: str | None = None) -> subprocess.CompletedProcess:
    return subprocess.run(["git", "-C", str(source), *args], env=env, check=check,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, input=input_text)


def checkout(destination: Path, url: str, commit: str) -> None:
    if destination.exists():
        if git(destination, "rev-parse", "HEAD").stdout.strip() != commit:
            raise FormatError(f"existing checkout {destination} has a different revision; use a separate dependency directory")
        return
    destination.mkdir(parents=True)
    git(destination, "init")
    git(destination, "remote", "add", "origin", url)
    git(destination, "fetch", "--depth", "1", "origin", commit)
    git(destination, "checkout", "--detach", "FETCH_HEAD")
    if git(destination, "rev-parse", "HEAD").stdout.strip() != commit:
        raise FormatError(f"checkout did not resolve the pinned revision: {destination}")


@contextmanager
def expected_index(source: Path, patches: list[Path]):
    with tempfile.TemporaryDirectory(prefix="mafia-index-") as temporary:
        env = {**os.environ, "GIT_INDEX_FILE": str(Path(temporary) / "index")}
        git(source, "read-tree", "HEAD", env=env)
        for patch in patches:
            git(source, "apply", "--cached", "--whitespace=nowarn", str(patch.resolve()), env=env)
        yield env


def verify(source: Path, commit: str, patches: list[Path]) -> dict[str, str]:
    if git(source, "rev-parse", "HEAD").stdout.strip() != commit:
        raise FormatError("upstream HEAD differs from dependency lock")
    with expected_index(source, patches) as env:
        difference = git(source, "diff", "--exit-code", "--no-ext-diff", env=env, check=False)
        if difference.returncode:
            raise FormatError("upstream source differs from the pinned revision plus reviewed patches; "
                              "refusing to reset or overwrite local changes")
        # CMake globs these source directories; an extra source must not bypass
        # the commit/patch gate. Build and diagnostic folders are not inputs.
        for directory in ("ps2xRuntime", "ps2xIOP", "ps2xRecomp", "ps2xAnalyzer", "ps2xTest"):
            # Git ignore rules do not constrain CMake's recursive source globs.
            # Even an ignored nested build/extra.cpp can become compiler input.
            extra = git(source, "ls-files", "--others", "--", directory, env=env).stdout
            if extra.strip():
                raise FormatError(f"untracked upstream inputs: {extra.strip()}")
    return {patch.name: sha256_file(patch) for patch in patches}


def apply_reviewed_patches(source: Path, commit: str, patches: list[Path]) -> None:
    try:
        verify(source, commit, patches)
        return
    except FormatError:
        # Only patch a pristine checkout; never partially repair an unknown tree.
        verify(source, commit, [])
    if patches:
        # Later patches can depend on earlier changes to the same file. A
        # multi-patch worktree --check does not apply that stack sequentially.
        # Construct the complete result in the private index, then apply one
        # combined change atomically, without touching the caller's real index.
        with expected_index(source, patches) as env:
            combined = git(source, "diff", "--cached", "--binary", "--full-index", "HEAD", env=env).stdout
        git(source, "apply", "--check", "-", input_text=combined)
        git(source, "apply", "--whitespace=nowarn", "-", input_text=combined)
    verify(source, commit, patches)


def prepare_dependencies(root: Path, destination: Path) -> tuple[Path, Path]:
    lock = json.loads((root / "dependencies.lock.json").read_text())
    source = destination / "PS2Recomp"
    checkout(source, lock["ps2recomp"]["url"], lock["ps2recomp"]["commit"])
    patches = sorted((root / "patches").glob("*.patch"))
    apply_reviewed_patches(source, lock["ps2recomp"]["commit"], patches)
    cache_lines = []
    for name, spec in sorted(lock["fetchcontent"].items()):
        path = destination / name
        checkout(path, spec["url"], spec["commit"])
        verify(path, spec["commit"], [])
        # CMake bracket strings preserve Unicode, spaces, semicolons and '$'.
        value = path.resolve().as_posix()
        if "]==]" in value:
            raise FormatError("dependency path contains a reserved CMake bracket delimiter")
        cache_lines.append(f"set(FETCHCONTENT_SOURCE_DIR_{name.upper()} [==[{value}]==] CACHE PATH \"Pinned source\" FORCE)")
    cache = destination / "fetchcontent.cmake"
    cache.write_text("\n".join(cache_lines) + "\n", encoding="utf-8")
    return source, cache
