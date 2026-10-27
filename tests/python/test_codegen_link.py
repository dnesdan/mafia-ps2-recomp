"""Exercise optional libdwarf linkage without host compression assumptions."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import tempfile
import unittest


HARNESS = Path(__file__).resolve().parents[2] / "patches/tests/check_integer_codegen.py"
SPEC = importlib.util.spec_from_file_location("integer_codegen_harness", HARNESS)
assert SPEC is not None and SPEC.loader is not None
harness = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(harness)


class CompressionLinkTests(unittest.TestCase):
    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory(prefix="mafia-link-fixture-")
        self.addCleanup(temporary.cleanup)
        self.build = Path(temporary.name)
        self.config = self.build / "_deps/libdwarf-build/config.h"
        self.config.parent.mkdir(parents=True)
        self.cache = self.build / "CMakeCache.txt"
        self.library_directory = self.build / "SDK with spaces"
        self.library_directory.mkdir()

    def library(self, name: str) -> Path:
        path = self.library_directory / name
        path.touch()
        return path

    def test_disabled_compression_needs_no_optional_libraries(self) -> None:
        self.config.write_text("/* #undef HAVE_ZLIB */\n/* #undef HAVE_ZSTD */\n")
        self.cache.write_text("ZSTD_LIBRARIES:FILEPATH=ZSTD_LIBRARIES-NOTFOUND\n")
        self.assertEqual(harness.libdwarf_compression_libraries(self.build), [])

    def test_enabled_zlib_and_zstd_use_exact_configured_paths(self) -> None:
        zlib, zstd = self.library("libz.so"), self.library("libzstd.so")
        self.config.write_text("#define HAVE_ZLIB 1\n#define HAVE_ZSTD 1\n")
        self.cache.write_text(f"CMAKE_BUILD_TYPE:STRING=Release\n"
                              f"ZLIB_LIBRARY_RELEASE:FILEPATH={zlib}\n"
                              f"ZSTD_LIBRARIES:FILEPATH={zstd}\n")
        self.assertEqual(harness.libdwarf_compression_libraries(self.build), [zlib, zstd])

    def test_debug_uses_its_configured_zlib(self) -> None:
        release, debug = self.library("libz.a"), self.library("libzd.a")
        self.config.write_text("#define HAVE_ZLIB 1\n")
        self.cache.write_text(f"CMAKE_BUILD_TYPE:STRING=Debug\n"
                              f"ZLIB_LIBRARY_RELEASE:FILEPATH={release}\n"
                              f"ZLIB_LIBRARY_DEBUG:FILEPATH={debug}\n")
        self.assertEqual(harness.libdwarf_compression_libraries(self.build), [debug])

    def test_debug_falls_back_to_release_when_cmake_found_only_release(self) -> None:
        release = self.library("libz.dylib")
        self.config.write_text("#define HAVE_ZLIB 1\n#define HAVE_ZSTD 0\n")
        self.cache.write_text(f"CMAKE_BUILD_TYPE:STRING=Debug\n"
                              f"ZLIB_LIBRARY_RELEASE:FILEPATH={release}\n"
                              "ZLIB_LIBRARY_DEBUG:FILEPATH=ZLIB_LIBRARY_DEBUG-NOTFOUND\n")
        self.assertEqual(harness.libdwarf_compression_libraries(self.build), [release])

    def test_enabled_library_missing_from_cache_fails_explicitly(self) -> None:
        self.config.write_text("#define HAVE_ZSTD 1\n")
        self.cache.write_text("ZSTD_LIBRARIES:FILEPATH=ZSTD_LIBRARIES-NOTFOUND\n")
        with self.assertRaisesRegex(ValueError, "HAVE_ZSTD.*configured library is missing"):
            harness.libdwarf_compression_libraries(self.build)

    def test_stale_library_path_fails_explicitly(self) -> None:
        self.config.write_text("#define HAVE_ZSTD 1\n")
        self.cache.write_text(f"ZSTD_LIBRARIES:FILEPATH={self.build / 'removed/libzstd.a'}\n")
        with self.assertRaisesRegex(ValueError, "HAVE_ZSTD"):
            harness.libdwarf_compression_libraries(self.build)


if __name__ == "__main__":
    unittest.main()
