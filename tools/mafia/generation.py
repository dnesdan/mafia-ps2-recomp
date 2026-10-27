"""Audit pinned generated C++ and bind builds to exact source bytes.

This conservative source audit is not a C++ verifier. Explicit failure paths
are distinguished from translator zero-return/comment-only placeholders.
Native SDK substitutions are forbidden even in diagnostic builds.
"""

from __future__ import annotations

import hashlib
import os
from pathlib import Path
import re
import tempfile

from .common import FormatError

_TOKEN = re.compile(r'//[^\n]*|/\*.*?\*/|/\*.*$|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', re.S)
_MARKER = re.compile(r"\b(?:unsupported|unimplemented|unhandled|unknown\s+(?:instruction|opcode))\b", re.I)
# Known translator instruction placeholders; explanatory prose is not itself
# evidence that an instruction has been replaced with a no-op.
_COMMENT_MARKER = re.compile(
    r"(?://|\*)\s*(?:unimplemented|unhandled|unsupported|unknown)\s+"
    r"(?:FCR\d+|COP[012]|[CM][FT]C[012]|MMI\d*|REGIMM|SPECIAL|VU[01]|FPU|PM[FT]HL|"
    r"instruction|opcode|register|operation|syscall|function)\b", re.I)
_PROHIBITED = re.compile(r"\b(?:PS2_REGISTER_GAME_OVERRIDE|registerDescriptor|ret0|ret1|reta0)\s*\(")
_STUB_REFERENCE = re.compile(r"\b(ps2_stubs|ps2_syscalls)\s*::\s*([A-Za-z_]\w*)")
_TRAP = re.compile(r"\bthrow\b|\b(?:std\s*::\s*)?(?:abort|terminate)\s*\(|\b__builtin_trap\s*\(")
_TRAP_START = re.compile(r"^\s*(?:throw\b|(?:std\s*::\s*)?(?:abort|terminate)\s*\(|__builtin_trap\s*\(|ps2_stubs\s*::\s*TODO(?:_NAMED)?\b)")
_SOURCE_NAME = re.compile(r"[A-Za-z0-9_.+-]+\.(?:cpp|h|hpp)")


class _CodeView:
    """Mask comments and ordinary literals in the pinned generator's output."""

    def __init__(self) -> None:
        self.in_comment = False

    def line(self, text: str) -> str:
        prefix = ""
        if self.in_comment:
            end = text.find("*/")
            if end < 0:
                return " " * len(text)
            prefix = " " * (end + 2)
            text = text[end + 2:]
            self.in_comment = False

        def mask(match: re.Match[str]) -> str:
            token = match.group()
            if token.startswith("/*") and not token.endswith("*/"):
                self.in_comment = True
            return " " * len(token)

        return prefix + _TOKEN.sub(mask, text)


def _write_manifest(directory: Path, content: bytes) -> None:
    fd, name = tempfile.mkstemp(prefix=".sources-", dir=directory)
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(content)
        os.replace(name, directory / "sources.sha256")
    finally:
        Path(name).unlink(missing_ok=True)


def inspect_generated(directory: Path) -> dict:
    required = ("register_functions.cpp", "ps2_recompiled_functions.h", "ps2_recompiled_stubs.h")
    for name in required:
        if not (directory / name).is_file():
            raise FormatError(f"recompiler did not produce {name}")
    files = sorted(p for p in directory.iterdir() if p.suffix in (".cpp", ".h", ".hpp"))
    if not any(p.suffix == ".cpp" and p.name != "register_functions.cpp" for p in files):
        raise FormatError("recompiler generated no function translation units")

    findings: list[dict] = []
    finding_keys: set[tuple[str, int, str]] = set()
    unresolved_locations: set[tuple[str, int]] = set()
    code_hash = hashlib.sha256()
    inventory = []
    manifest_lines = []
    total_bytes = 0

    def finding(path: Path, number: int, kind: str, text: str, detail: str = "") -> None:
        if kind == "silent-unsupported":
            unresolved_locations.add((path.name, number))
        key = (path.name, number, kind)
        if key not in finding_keys:
            finding_keys.add(key)
            findings.append({"file": path.name, "line": number, "kind": kind,
                             "text": text.strip()[:400], **({"detail": detail} if detail else {})})

    for path in files:
        if not path.is_file() or path.is_symlink() or not _SOURCE_NAME.fullmatch(path.name):
            raise FormatError(f"generated source must be a regular file with a safe basename: {path.name!r}")
        digest = hashlib.sha256()
        size = 0
        view = _CodeView()
        carry = ""
        pending_comments: list[tuple[int, str]] = []
        throw_pending = False
        throw_origin = 0
        with path.open("rb") as source:
            for number, raw in enumerate(source, 1):
                digest.update(raw)
                size += len(raw)
                try:
                    line = raw.decode("utf-8")
                except UnicodeDecodeError as exc:
                    raise FormatError(f"generated source is not UTF-8: {path.name}:{number}") from exc
                code = view.line(line)
                stripped = code.strip()
                was_throw_pending = throw_pending
                trap_match = _TRAP.search(code)
                has_throw = trap_match is not None
                unconditional_trap = bool(_TRAP_START.match(code))

                if stripped and pending_comments:
                    # Only the next executable statement can implement a
                    # comment-only unsupported instruction as an explicit trap.
                    if not unconditional_trap:
                        for old_number, old_line in pending_comments:
                            finding(path, old_number, "silent-unsupported", old_line)
                    else:
                        unresolved_locations.add((path.name, number))
                    pending_comments.clear()

                combined = carry + code
                todo_trap = False
                for match in _PROHIBITED.finditer(combined):
                    if match.end() > len(carry):
                        finding(path, number, "prohibited-substitution", line, match.group().strip())
                for match in _STUB_REFERENCE.finditer(combined):
                    if match.end() <= len(carry):
                        continue
                    namespace, handler = match.group(1), match.group(2)
                    direct_call = combined[match.end():].lstrip().startswith("(")
                    if namespace == "ps2_stubs" and handler in ("TODO", "TODO_NAMED") and direct_call:
                        # The locked runtime throws for these handlers under
                        # MissingFunctionPolicy::Stop, enforced by the host.
                        finding(path, number, "explicit-trap", line, "strict-runtime TODO trap")
                        todo_trap = True
                        unresolved_locations.add((path.name, number))
                    else:
                        finding(path, number, "prohibited-substitution", line,
                                f"native SDK replacement/reference {namespace}::{handler}")
                carry = combined[-256:]

                if has_throw:
                    finding(path, number, "explicit-trap", line)
                marker = bool(_MARKER.search(line))
                if marker and not stripped.startswith("#"):
                    if was_throw_pending:
                        unresolved_locations.add((path.name, throw_origin))
                    elif unconditional_trap and (has_throw or todo_trap):
                        unresolved_locations.add((path.name, number))
                    elif stripped:
                        finding(path, number, "silent-unsupported", line)
                    elif _COMMENT_MARKER.search(line):
                        pending_comments.append((number, line))
                if trap_match and unconditional_trap:
                    throw_pending = ";" not in code[trap_match.start():]
                    throw_origin = number
                elif todo_trap and unconditional_trap:
                    throw_pending = ";" not in code
                    throw_origin = number
                elif throw_pending and ";" in code:
                    throw_pending = False

        for number, line in pending_comments:
            finding(path, number, "silent-unsupported", line)
        if view.in_comment:
            raise FormatError(f"unterminated generated C++ block comment: {path.name}")
        total_bytes += size
        hexdigest = digest.hexdigest()
        inventory.append({"name": path.name, "bytes": size, "sha256": hexdigest})
        code_hash.update(path.name.encode("utf-8") + b"\0" + digest.digest())
        manifest_lines.append(f"{hexdigest}  {path.name}\n")

    manifest = "".join(manifest_lines).encode("utf-8")
    _write_manifest(directory, manifest)
    traps = sum(f["kind"] == "explicit-trap" for f in findings)
    silent = sum(f["kind"] == "silent-unsupported" for f in findings)
    return {"schema_version": 2, "source_files": len(files),
            "translation_units": sum(p.suffix == ".cpp" for p in files),
            "source_bytes": total_bytes, "source_tree_sha256": code_hash.hexdigest(),
            "sources_manifest_sha256": hashlib.sha256(manifest).hexdigest(),
            "files": inventory, "findings": findings,
            "prohibited_substitutions": sum(f["kind"] == "prohibited-substitution" for f in findings),
            "unresolved_lines": len(unresolved_locations), "trap_lines": traps,
            "unsupported_trap_lines": sum(f["kind"] == "explicit-trap" and
                                           (f["file"], f["line"]) in unresolved_locations for f in findings),
            "silent_unsupported_lines": silent,
            "limitation": "conservative audit of pinned generated C++; not proof of instruction, delay-slot, FPU, VU or game correctness"}
