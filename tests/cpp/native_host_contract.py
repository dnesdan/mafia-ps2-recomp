#!/usr/bin/env python3
"""Verify the actual native host and Apple bundle with authored synthetic input.

This test does not contain, generate, load, or establish any Mafia game content.
"""

import argparse
import hashlib
import json
from pathlib import Path
import plistlib
import struct
import subprocess
import sys


def fixture_bytes() -> bytes:
    image = bytearray(0x110)
    image[:16] = b"\x7fELF\x01\x01\x01" + bytes(9)
    struct.pack_into("<HHIIIIIHHHHHH", image, 16,
                     2, 8, 1, 0x100000, 52, 0, 0x20924001,
                     52, 32, 1, 0, 0, 0)
    struct.pack_into("<IIIIIIII", image, 52,
                     1, 0x100, 0x100000, 0x100000, 16, 16, 5, 0x100)
    struct.pack_into("<IIII", image, 0x100,
                     0x3C021234, 0x34425678, 0x03E00008, 0)
    return bytes(image)


def check(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def verify_host(executable: Path, fixture: Path, work: Path) -> None:
    executable, fixture, work = executable.resolve(), fixture.resolve(), work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    original = fixture_bytes()
    check(fixture.read_bytes() == original, "Synthetic ELF bytes drifted")
    if sys.platform == "darwin":
        arch = subprocess.check_output(["lipo", "-archs", str(executable)], text=True).strip()
        check(arch == "arm64", f"Expected a native ARM64 executable, got {arch}")
        metadata = plistlib.loads((executable.parent.parent / "Info.plist").read_bytes())
        check(metadata["CFBundleExecutable"] == "MafiaHostContract", "Incorrect bundle executable")
        check(metadata["CFBundlePackageType"] == "APPL", "Synthetic host is not an application bundle")

    def run(name: str, elf: Path, *extra: str) -> tuple[int, dict]:
        report = work / f"{name}.json"
        report.unlink(missing_ok=True)
        result = subprocess.run([
            str(executable), "--elf", str(elf), "--data-root", str(fixture.parent),
            "--save-root", str(work / "memory-cards"), "--headless", "--timeout-ms", "1000",
            "--report", str(report), *extra,
        ], capture_output=True, text=True, timeout=8)
        (work / f"{name}.stdout.log").write_text(result.stdout)
        (work / f"{name}.stderr.log").write_text(result.stderr)
        check(report.is_file(), f"{name} did not produce a report: {result.stderr}")
        data = json.loads(report.read_text())
        check(data["profile_id"] == "synthetic-native-host-contract", "Fixture profile was not selected")
        check(data["elf_sha256"] == hashlib.sha256(original).hexdigest(), "Compiled fixture SHA mismatch")
        check(data["gameplay_verified"] is False and data["game_frame_verified"] is False,
              "A synthetic host test must never claim game verification")
        return result.returncode, data

    code, validation = run("validated-only", fixture, "--validate-only")
    check(code == 0 and validation["status"] == "validated-only", "ELF-only validation failed")
    check(validation["elf_verified"] is True and validation["elf_loaded"] is False
          and validation["entry_executed"] is False, "Validation was confused with execution")

    code, execution = run("strict-return", fixture)
    check(code == 1 and execution["status"] == "missing-dispatch", "Root zero return did not fail closed")
    check(execution["elf_verified"] is True and execution["elf_loaded"] is True
          and execution["entry_executed"] is True, "Original synthetic entry was not dispatched")
    check(execution["last_guest_pc"] == 0 and execution["last_guest_branch_pc"] == 0x100008,
          "Failed branch source/target diagnostics were lost")
    check(execution["presentations"] == 0, "Headless synthetic execution unexpectedly presented an image")

    changed = bytearray(original)
    changed[-1] ^= 1
    wrong_elf = work / "wrong-hash.elf"
    wrong_elf.write_bytes(changed)
    code, rejected = run("wrong-hash", wrong_elf)
    check(code == 1 and rejected["stage"] == "elf-verification", "Wrong ELF did not fail verification")
    check(rejected["elf_verified"] is False and rejected["elf_loaded"] is False
          and rejected["entry_executed"] is False, "Unverified bytes reached the runtime")
    checks = ["SHA gate", "validation", "synthetic entry", "strict return"]
    if sys.platform == "darwin":
        checks.insert(0, "ARM64 application bundle")
    print("Native host contract passed: " + ", ".join(checks) + "; no game execution.")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--write-fixture", type=Path)
    parser.add_argument("--executable", type=Path)
    parser.add_argument("--fixture", type=Path)
    parser.add_argument("--work-dir", type=Path)
    args = parser.parse_args()
    if args.write_fixture:
        args.write_fixture.write_bytes(fixture_bytes())
    elif args.executable and args.fixture and args.work_dir:
        verify_host(args.executable, args.fixture, args.work_dir)
    else:
        parser.error("Specify --write-fixture, or --executable, --fixture and --work-dir")


if __name__ == "__main__":
    main()
