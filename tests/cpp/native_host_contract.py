#!/usr/bin/env python3
"""Verify the actual native host and Apple bundle with authored synthetic input.

This test does not contain, generate, load, or establish any Mafia game content.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import plistlib
import struct
import subprocess
import sys
import tempfile


def fixture_bytes() -> bytes:
    image = bytearray(0x200)
    image[:16] = b"\x7fELF\x01\x01\x01" + bytes(9)
    struct.pack_into("<HHIIIIIHHHHHH", image, 16,
                     2, 8, 1, 0x100000, 52, 0x120, 0x20924001,
                     52, 32, 1, 40, 4, 0)
    struct.pack_into("<IIIIIIII", image, 52,
                     1, 0x100, 0x100000, 0x100000, 16, 16, 5, 0x100)
    struct.pack_into("<IIII", image, 0x100,
                     0x3C021234, 0x34425678, 0x03E00008, 0)
    # Authored symbols exercise report resolution from the same verified bytes.
    names = b"\0synthetic_entry\0"
    image[0x1C0:0x1C0 + len(names)] = names
    struct.pack_into("<10I", image, 0x148, 0, 1, 6, 0x100000, 0x100, 16, 0, 0, 4, 0)
    struct.pack_into("<10I", image, 0x170, 0, 3, 0, 0, 0x1C0, len(names), 0, 0, 1, 0)
    struct.pack_into("<10I", image, 0x198, 0, 2, 0, 0, 0x1E0, 32, 2, 1, 4, 16)
    struct.pack_into("<IIIBBH", image, 0x1F0, 1, 0x100000, 16, 0x12, 0, 1)
    return bytes(image)


def check(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def verify_default_launcher(repo: Path, work: Path) -> None:
    """Exercise default-path resolution without opening a window or any game."""
    platform = "macos" if sys.platform == "darwin" else "linux"
    with tempfile.TemporaryDirectory(prefix="launcher-contract-", dir=work) as temporary:
        root = Path(temporary)
        build, data, extracted = root / "build", root / "work", root / "actual-extracted"
        build.mkdir()
        data.mkdir()
        extracted.mkdir()
        (data / "extracted").symlink_to(extracted, target_is_directory=True)
        boot = extracted / "synthetic.elf"
        boot.write_bytes(fixture_bytes())
        disc = data / "disc.json"
        disc.write_text(json.dumps({"boot_elf": boot.name}))
        (build / "CMakeCache.txt").write_text("CMAKE_BUILD_TYPE:STRING=Debug\n")
        binary = build / ("MafiaNative.app/Contents/MacOS/MafiaNative" if platform == "macos"
                          else "MafiaNative")
        binary.parent.mkdir(parents=True, exist_ok=True)
        binary.write_text("#!/usr/bin/env python3\nimport json,sys\nprint(json.dumps(sys.argv[1:]))\n")
        binary.chmod(0o755)
        environment = os.environ.copy()
        environment.update(MAFIA_BUILD_DIR=str(build), MAFIA_BUILD_CONFIG="Debug",
                           MAFIA_WORK_DIR=str(data), PYTHON=sys.executable)
        environment.pop("MAFIA_PS2_ISO", None)
        launcher = repo / "tools" / f"run_{platform}.sh"
        result = subprocess.run(["bash", str(launcher)], env=environment, capture_output=True,
                                text=True, timeout=5)
        check(result.returncode == 0, f"Launcher rejected a symlinked data root: {result.stderr}")
        arguments = json.loads(result.stdout)
        check(Path(arguments[arguments.index("--elf") + 1]) == boot,
              "Launcher did not use the canonical boot ELF inside its selected data root")
        outside = root / "outside.elf"
        outside.write_bytes(fixture_bytes())
        (extracted / "escape.elf").symlink_to(outside)
        disc.write_text(json.dumps({"boot_elf": "escape.elf"}))
        rejected = subprocess.run(["bash", str(launcher)], env=environment, capture_output=True,
                                  text=True, timeout=5)
        check(rejected.returncode != 0 and "Invalid boot path" in rejected.stderr,
              "Launcher permitted an individual boot-file symlink outside the selected data root")


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
        check(data["latest_sce_open_request"] is None and data["sce_open_request_snapshot_error"] == "",
              "Synthetic or rejected input acquired a game-specific sceOpen request")
        check(data["frontend_snapshot"] is None and data["frontend_snapshot_error"] == "",
              "Synthetic or rejected input acquired game-specific frontend globals")
        return result.returncode, data

    code, validation = run("validated-only", fixture, "--validate-only")
    check(code == 0 and validation["status"] == "validated-only", "ELF-only validation failed")
    check(validation["elf_verified"] is True and validation["elf_loaded"] is False
          and validation["entry_executed"] is False, "Validation was confused with execution")
    check(validation["iop"] is None, "ELF-only validation invented an IOP execution snapshot")
    check(validation["ee"] is None and validation["io_registers"] is None,
          "ELF-only validation invented EE or device execution state")
    check(validation["ee_final_context"] is None, "ELF-only validation invented a guest context")
    check(validation["controller_input"]["host_samples"] == 0,
          "ELF-only validation sampled a controller device")
    check(validation["symbol"] == "synthetic_entry" and validation["symbol_address"] == 0x100000
          and validation["symbol_offset"] == 0 and validation["symbol_source"] == "verified-elf"
          and validation["symbol_lookup_error"] == "", "Verified entry symbol was not resolved")

    code, execution = run("strict-return", fixture)
    check(code == 1 and execution["status"] == "missing-dispatch", "Root zero return did not fail closed")
    check(execution["elf_verified"] is True and execution["elf_loaded"] is True
          and execution["entry_executed"] is True, "Original synthetic entry was not dispatched")
    check(execution["last_guest_pc"] == 0 and execution["last_guest_branch_pc"] == 0x100008,
          "Failed branch source/target diagnostics were lost")
    check(execution["symbol"] is None and execution["branch_symbol"] == "synthetic_entry"
          and execution["branch_symbol_address"] == 0x100000 and execution["branch_symbol_offset"] == 8,
          "Branch source symbol was lost or an invalid target acquired a guessed symbol")
    check(execution["presentations"] == 0, "Headless synthetic execution unexpectedly presented an image")
    check(execution["ee_snapshot_error"] == "" and execution["ee"]["sequence"] > 0
          and any(thread["entry"] == 0x100000 for thread in execution["ee"]["threads"]),
          "Native execution lost its EE scheduler snapshot")
    context = execution["ee_final_context"]
    check(execution["ee_context_snapshot_error"] == "" and context["gpr_low32"]["sp"] == 0x1FFFFF0
          and context["stack_status"] == "captured" and context["stack"]["size"] == 16
          and context["stack"]["physical_address"] == 0x1FFFFF0
          and len(context["stack"]["bytes_hex"]) == 32,
          "Final context lost the actual guest stack pointer or read past the end of RAM")
    check(execution["io_snapshot_error"] == "" and execution["io_registers"]["vif1_stat"] == 0
          and execution["io_registers"]["vif1_dma_chcr"] == 0,
          "EE-only fixture invented VIF or DMA state")
    check(execution["iop_snapshot_error"] == "" and execution["iop"]["instructions"] == 0
          and execution["iop"]["loaded_guest_modules"] == 0
          and execution["iop"]["guest_rpc_servers"] == 0,
          "EE-only fixture incorrectly claimed IOP guest module execution")
    check(execution["controller_input"]["route"] == "iop-sio2"
          and execution["controller_input"]["host_samples"] >= 1
          and execution["controller_input"]["events"] == [],
          "Headless execution lost its neutral serial-controller source")
    sio2 = next(service for service in execution["iop"]["services"]
                if service["name"] == "sio2_device")
    check(sio2["metrics"]["pio_transfers"] == 0 and sio2["metrics"]["controller_packets"] == 0,
          "Host sampling was falsely counted as original IOP controller traffic")

    with tempfile.TemporaryDirectory(prefix="serial-card-contract-", dir=work) as temporary:
        card_root = Path(temporary)
        card = card_root / "slot1.ps2"
        code, card_validation = run("card-validate-only", fixture, "--validate-only",
                                    "--memory-card", str(card))
        check(code == 0 and not card.exists() and not card_validation["memory_card"]["mounted"],
              "Validation-only created or mounted persistent card storage")
        code, attached = run("card-created", fixture, "--memory-card", str(card))
        check(code == 1 and attached["status"] == "missing-dispatch"
              and attached["memory_card"]["mounted"] and card.stat().st_size == 8_650_752,
              "Native card attachment hid the guest result or created incorrect geometry")
        original_card_digest = hashlib.sha256(card.read_bytes()).hexdigest()
        code, reopened = run("card-reopened", fixture, "--memory-card", str(card))
        check(code == 1 and reopened["memory_card"]["mounted"]
              and hashlib.sha256(card.read_bytes()).hexdigest() == original_card_digest,
              "Reopening a card without guest writes changed its persistent bytes")
        damaged = card_root / "existing-data.ps2"
        damaged.write_bytes(b"existing user data must survive")
        code, bad_card = run("card-invalid-image", fixture, "--memory-card", str(damaged))
        check(code == 1 and not bad_card["entry_executed"]
              and not bad_card["memory_card"]["mounted"]
              and damaged.read_bytes() == b"existing user data must survive",
              "Invalid existing card storage was overwritten or reached guest execution")
        absent_root = card_root / "absent"
        code, absent = run("card-disabled", fixture, "--no-memory-card", "--save-root", str(absent_root))
        check(code == 1 and absent["status"] == "missing-dispatch"
              and not absent["memory_card"]["mounted"] and not (absent_root / "slot1.ps2").exists(),
              "The absent-card option attached or created a physical card")
    code, pulse = run("scheduled-controller", fixture, "--pad-event", "0:start:10",
                      "--pad-event", "5:cross:20")
    check(code == 1 and pulse["status"] == "missing-dispatch"
          and pulse["controller_input"]["events"] == [
              {"at_ms": 0, "duration_ms": 10, "button_mask": 8},
              {"at_ms": 5, "duration_ms": 20, "button_mask": 16384}],
          "Scheduled controller input concealed the guest result or lost its reproducible timeline")

    capture = work / "no-surface.ppm"
    capture.unlink(missing_ok=True)
    code, no_surface = run("capture-no-surface", fixture, "--capture-frame", str(capture))
    check(code == 1 and no_surface["status"] == "missing-dispatch", "Capture concealed the guest failure")
    check(no_surface["frame_capture"]["status"] == "unavailable" and not capture.exists(),
          "Capture fabricated an image without a configured GS surface")
    check(no_surface["frame_capture"]["sha256"] == "", "A missing image acquired a content digest")

    changed = bytearray(original)
    changed[-1] ^= 1
    wrong_elf = work / "wrong-hash.elf"
    wrong_elf.write_bytes(changed)
    code, rejected = run("wrong-hash", wrong_elf, "--capture-frame", str(capture))
    check(code == 1 and rejected["stage"] == "elf-verification", "Wrong ELF did not fail verification")
    check(rejected["elf_verified"] is False and rejected["elf_loaded"] is False
          and rejected["entry_executed"] is False, "Unverified bytes reached the runtime")
    check(rejected["iop"] is None, "Rejected input acquired an IOP execution snapshot")
    check(rejected["ee"] is None and rejected["io_registers"] is None,
          "Rejected input acquired EE or device execution state")
    check(rejected["ee_final_context"] is None, "Rejected input acquired a guest context")
    check(rejected["symbol"] is None and rejected["branch_symbol"] is None
          and rejected["symbol_source"] is None, "Unverified input acquired symbol evidence")
    check(rejected["frame_capture"]["status"] == "not-executed" and not capture.exists(),
          "Rejected input produced framebuffer evidence")
    verify_default_launcher(Path(__file__).resolve().parents[2], work)
    checks = ["SHA gate", "validation", "verified ELF symbols", "synthetic entry", "strict return", "absent-frame refusal",
              "launcher symlink containment", "persistent serial-card mount and input preservation"]
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
