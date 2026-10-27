"""Bind built analyzer/recompiler executables to the reviewed upstream tree."""

from __future__ import annotations

import json
from pathlib import Path

from .common import FormatError, sha256_file, write_json
from .upstream import verify


def build_configuration(build: Path, configuration: str) -> bool:
    """Validate the label against CMake's actual cache and return multi-config."""
    if not isinstance(configuration, str) or configuration not in ("Debug", "Release", "RelWithDebInfo", "MinSizeRel"):
        raise FormatError("unsupported toolchain configuration label")
    cache = build / "CMakeCache.txt"
    if not cache.is_file():
        raise FormatError("toolchain build has no CMake cache")
    values = {}
    for line in cache.read_text(errors="replace").splitlines():
        if line.startswith(("#", "//")) or "=" not in line or ":" not in line.split("=", 1)[0]:
            continue
        key, value = line.split("=", 1)
        values[key.split(":", 1)[0]] = value
    if "CMAKE_CONFIGURATION_TYPES" in values:
        if configuration not in values["CMAKE_CONFIGURATION_TYPES"].split(";"):
            raise FormatError("requested configuration is unavailable in the multi-config build")
        return True
    if values.get("CMAKE_BUILD_TYPE") != configuration:
        raise FormatError("requested configuration differs from the single-config CMake build type")
    return False


def executable_path(build: Path, component: str, name: str, configuration: str) -> Path:
    multi = build / component / configuration / name
    single = build / component / name
    is_multi = build_configuration(build, configuration)
    target = multi if is_multi else single
    if not target.is_file():
        raise FormatError(f"built tool is missing: {target}")
    return target


def stamp_built_tools(source: Path, build: Path, commit: str, patches: list[Path], configuration: str) -> dict:
    """Call only after CMake successfully builds both targets from verified source."""
    patch_digests = verify(source, commit, patches)
    executables = {}
    for label, component, name in (("analyzer", "ps2xAnalyzer", "ps2_analyzer"),
                                   ("recompiler", "ps2xRecomp", "ps2_recomp")):
        path = executable_path(build, component, name, configuration)
        executables[label] = {"path": path.relative_to(build).as_posix(), "sha256": sha256_file(path)}
    metadata = {"schema_version": 1, "upstream_commit": commit, "patches": patch_digests,
                "configuration": configuration, "executables": executables,
                "cmake_cache_sha256": sha256_file(build / "CMakeCache.txt")}
    write_json(build / "mafia-toolchain.json", metadata)
    return metadata


def verified_tools(source: Path, build: Path, commit: str, patches: list[Path]) -> tuple[Path, Path]:
    stamp = build / "mafia-toolchain.json"
    if not stamp.is_file():
        raise FormatError("upstream binaries have no verified build stamp; rebuild with tools/bootstrap.py --tools-only")
    metadata = json.loads(stamp.read_text())
    if not isinstance(metadata, dict) or metadata.get("schema_version") != 1:
        raise FormatError("unsupported toolchain build stamp schema")
    configuration = metadata.get("configuration")
    build_configuration(build, configuration)
    if metadata.get("upstream_commit") != commit or metadata.get("patches") != verify(source, commit, patches):
        raise FormatError("upstream binaries were built with a different revision/patch set; rebuild the toolchain")
    if metadata.get("cmake_cache_sha256") != sha256_file(build / "CMakeCache.txt"):
        raise FormatError("toolchain build configuration changed since the verified build; rebuild the toolchain")
    found = []
    for label, component, name in (("analyzer", "ps2xAnalyzer", "ps2_analyzer"),
                                   ("recompiler", "ps2xRecomp", "ps2_recomp")):
        item = metadata.get("executables", {}).get(label, {})
        relative = Path(item.get("path", ""))
        if relative.is_absolute() or not relative.parts or ".." in relative.parts:
            raise FormatError("unsafe tool path in build stamp")
        path = build / relative
        if path != executable_path(build, component, name, configuration):
            raise FormatError("tool path disagrees with its recorded CMake configuration")
        if path.is_symlink() or not path.resolve().is_relative_to(build.resolve()) or not path.is_file():
            raise FormatError("tool executable is missing or outside the build directory")
        if sha256_file(path) != item.get("sha256"):
            raise FormatError(f"{label} binary changed since the verified build")
        found.append(path)
    return found[0], found[1]
