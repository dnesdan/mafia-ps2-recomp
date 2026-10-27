"""Small shared helpers. No game downloads or proprietary fixtures."""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import tempfile


class FormatError(ValueError):
    """An input is malformed or uses an explicitly unsupported format."""


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def write_json(path: Path, value: object) -> None:
    """Write stable JSON atomically; no wall-clock timestamps in analysis."""
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, name = tempfile.mkstemp(prefix=".tmp-", dir=path.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as stream:
            json.dump(value, stream, indent=2, sort_keys=True, ensure_ascii=True)
            stream.write("\n")
        os.replace(name, path)
    finally:
        Path(name).unlink(missing_ok=True)


def checked_slice(data: bytes, offset: int, size: int, context: str) -> bytes:
    if offset < 0 or size < 0 or offset > len(data) or size > len(data) - offset:
        raise FormatError(f"{context}: range {offset}+{size} outside {len(data)} bytes")
    return data[offset:offset + size]


def safe_destination(root: Path, relative: str) -> Path:
    """Reject traversal and existing symlinks before writing extracted data."""
    parts = relative.replace("\\", "/").split("/")
    if not parts or any(part in ("", ".", "..") or ":" in part or "\x00" in part for part in parts):
        raise FormatError(f"unsafe disc path: {relative!r}")
    root = root.resolve()
    target = root.joinpath(*parts)
    for candidate in [target, *target.parents]:
        if candidate == root:
            break
        if candidate.is_symlink():
            raise FormatError(f"symlink in extraction destination: {candidate}")
    if not target.resolve().is_relative_to(root):
        raise FormatError(f"path escapes extraction root: {relative!r}")
    return target
