"""Select an exact build by executable identity, never by a guessed filename."""

from __future__ import annotations

import json
from pathlib import Path
import re

from .common import FormatError
from .elf import ElfImage


def select_profile(elf: ElfImage, profiles: Path) -> dict:
    matches = []
    for file in sorted(profiles.glob("*.json")):
        profile = json.loads(file.read_text(encoding="utf-8"))
        if profile.get("elf_sha256") == elf.sha256:
            matches.append(profile)
    if len(matches) != 1:
        raise FormatError(f"expected one hash-matched profile, found {len(matches)} for {elf.sha256}; "
                          "analyze this build and add a separate reviewed profile")
    profile = matches[0]
    if profile.get("elf_bytes") != len(elf.data) or profile.get("entry_point") != elf.entry_point:
        raise FormatError("profile length/entry does not match the exact ELF")
    if not re.fullmatch(r"[A-Za-z0-9_.-]+", profile.get("profile_id", "")):
        raise FormatError("invalid profile identifier")
    policy = profile.get("policy", {})
    if (policy.get("missing_function") != "stop" or policy.get("unknown_syscall") != "stop"
            or policy.get("unsupported_instruction") != "trap" or policy.get("automatic_instruction_patches") is not False):
        raise FormatError("profile must fail closed on missing or unsupported behavior")
    if policy.get("allow_interpreter_fallback") is not False or policy.get("allow_jit_fallback") is not False:
        raise FormatError("profiles must explicitly disable EE interpreter/JIT fallback")
    if profile.get("instruction_patches") or profile.get("stubs"):
        raise FormatError("this bring-up pipeline does not accept instruction patches or function replacement stubs")
    return profile


def recompiler_config(elf_path: Path, output: Path, profile: dict, function_map: Path | None = None) -> str:
    # JSON basic strings are compatible with TOML basic strings for these paths.
    quote = lambda value: json.dumps(str(value), ensure_ascii=False)
    return "\n".join([
        "# Generated locally. Original instructions are not silently patched.",
        "[general]", f"input = {quote(elf_path.resolve())}", f"output = {quote(output.resolve())}",
        f"ghidra_output = {quote(function_map.resolve() if function_map else '')}",
        "single_file_output = false", "low_memory_mode = true", "output_worker_threads = 1",
        "patch_syscalls = false", "patch_cop0 = false", "patch_cache = false",
        "auto_bind_runtime_calls = false",
        "stubs = []", "skip = []", f"entry_points = {json.dumps(profile.get('entry_points', []))}",
        "", "[patches]", "instructions = []", ""
    ])
