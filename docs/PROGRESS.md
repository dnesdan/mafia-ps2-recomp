# Verified progress

Updated **2026-10-28**. **Main-menu entry, movie playback and gameplay remain unverified.** The last published original-game execution uses patches 0001–0039. Newer source changes and their tests are reported separately below; they do not retroactively change that run's result.

The earlier detailed execution history, intermediate executable hashes and per-patch negative controls remain in the [patch-0041 checkpoint report](https://github.com/dnesdan/mafia-ps2-recomp/blob/8c4b294c26ab24daadac57f8f7f677a9e90d051d/docs/PROGRESS.md). This report keeps the current state and the evidence needed to continue.

## Latest source checkpoint: patches 0042–0043

Patch 0042 fixes a real ARM64 macOS CI compilation failure: the supported Apple standard library did not provide `std::jthread` in the SIO2 concurrency test. The fixture already stopped and joined its writer explicitly on both normal and exceptional exits, so it now uses `std::thread`. Runtime behavior and concurrency assertions are unchanged. All four Linux and macOS jobs passed in [run 16](https://github.com/dnesdan/mafia-ps2-recomp/actions/runs/40730058723) and again with the artifact changes in [run 17](https://github.com/dnesdan/mafia-ps2-recomp/actions/runs/40730476659).

CI now retains a verified public-source IOP archive and test logs for seven days. The archive contains `ps2xIOP`, the license, dependency lock and patch provenance, not commercial inputs, generated game code, executable game builds or Git metadata. The current local IOP workspace was recovered from the run-17 artifact. The previous original-game workspace is unreachable; the current container does not have its ISO, extracted files or generated game corpus.

Patch 0043 implements the four-argument `sceCdRE` import (`cdvdman:114`) for stream1 and 2048-byte sectors. An accepted request records its location, count and destination ownership without reading media or writing payload bytes immediately. A later modeled device deadline reads the selected medium, publishes the complete payload, and then delivers the live stream callback and ordinary read callback in interrupt context with their saved GP. The original streaming driver continues to own its ring buffer and next request.

The implementation distinguishes hardware completion from masked driver completion, refuses replacement commands while busy, and keeps failed or short reads from partially overwriting the destination. Buffer allocation identities are checked before the read and after host I/O. Reset, allocation reuse, callback replacement, nested checkpoints and successor requests have regression coverage. A configured ISO cannot silently fall back to unrelated extracted-file sectors.

The behavior was checked against the pinned primary PS2SDK [CDVD implementation](https://github.com/ps2dev/ps2sdk/blob/329e2bb43287e16af16c4dde1c614e2662c24468/iop/cdvd/cdvdman/src/cdvdman.c), especially `sceCdRE`, `cdvdman_read` and `cdvdman_intr_cb`, and the [stream driver's callback](https://github.com/ps2dev/ps2sdk/blob/329e2bb43287e16af16c4dde1c614e2662c24468/iop/cdvd/cdvdstm/src/cdvdstm.c). The 128-cycle command delay is a bounded service model, not physical-drive timing. Stream0, raw-sector formats, Read0 chunk callbacks, incomplete request shapes and unsupported blocking waits remain explicit unsupported boundaries.

### Executed checks for patch 0043

| Check | Observed result |
| --- | --- |
| Complete IOP CTest suite, GCC 14.2 Debug | 20/20 programs passed. |
| Complete IOP CTest suite, Clang 17 with `-O2`, assertions and strict floating-point flags | 20/20 programs passed. |
| Complete IOP CTest suite, GCC with ASan, UBSan and leak detection | 20/20 programs passed; no sanitizer findings. |
| NCMD/stream cases, corrected implementation | 24/24 passed. |
| Identical NCMD/stream tests on the exact preceding source | 15/24 passed: all nine new behavior groups failed, while the unsupported-shape control and fourteen existing groups passed. |
| Patch application against the verified run-17 source snapshot | Passed; no source changes outside the additive patch. |
| Full source/host/runtime Linux and ARM64 macOS CI through patch 0042 | All four jobs passed in runs 16 and 17. |
| Patch-0043 full CI | [Run 18](https://github.com/dnesdan/mafia-ps2-recomp/actions/runs/40733776289) was still running when this checkpoint was written; no result is assumed. |
| New original-game execution, movie playback or main menu | Not performed in the recovered source-only workspace. |

Review covered ABI and supplied-export precedence, memory ownership and reentrancy, and negative-control/test scope. It found and corrected reset-during-host-I/O handling and a wrong-major test that initially failed before reaching the intended import. These are self-review and executed-test results, not a claim of new independent reviewers.

The patch is in [commit 210616f](https://github.com/dnesdan/mafia-ps2-recomp/commit/210616f6b2ffb5392979cb574e85a9414381b4eb). Its SHA-256 is `6acc0a4450083a60a18efe3838b8b9088b61008f627e8df703f75a73e74475c2`. The downloaded source archive's SHA-256 is `2be404b0045d8f42a01f28665819aa55fa8d7b68f58397e2e651dc9b3abfbcb7` and its provenance records upstream `e5636981a1c9de368f6b71e819c3dc188dfedbf4` plus patches 0001–0042.

## Original-game execution status

These observations come from the previously recorded original-program runs, not the constructed-input tests above.

| Milestone | Evidence and limit |
| --- | --- |
| Verify and load the original ELF | Exact SHA-256, headers and original entry `0x00100008` verified. Original main executes. |
| Initialize original IOP services | Supplied LOADFILE, CDVDFSV and FILEIO execute their entry functions and register services. Later game-supplied modules also execute. Kernel/reset behavior is modeled; no PS2 BIOS boot is claimed. |
| Original frontend graphics | Gathering splash, autosave caution and no-card dialog captured from the actual GS surface on Linux x86-64. |
| Original input | Up and Cross consumed in full-game Xvfb/software-OpenGL window runs through the original SIO2MAN/PADMAN route. Separate original-driver tests verify Start/Cross press and release. Physical gamepads remain untested. |
| Memory-card query | Original MCSERV reply completes and the original no-card dialog appears. No working save system or present memory card is claimed. |
| Movie initialization | Original CDVDSTM starts, stream initialization and layer-zero search advance, and correct trailer metadata reaches EE memory. |
| Video playback, main menu, missions and save/load | Not verified. |
| Original game on Apple Silicon or Linux ARM64 | Not verified. ARM64 macOS CI uses synthetic input, not the commercial game corpus. |

### Last recorded native run: patch 0039

The Linux window run lasted 90.861 seconds and recorded 644 presentations, 674 SIO2 transactions, 10,300,542 IOP instructions and 12 loaded modules. The original `FileInfo` at EE address `0x1FFF920` contains trailer LSN **1,269,101** and size **68,435,972 bytes**, matching the independently checked original ISO. The full earlier search buffer had already been reused, so its complete metadata hash was not claimed as a native snapshot.

The run stopped at `cdvdman:46`, version `0x0101`, IOP PC `0x9D2B8`, return address `0x9C188`: original CDVDSTM's `sceCdNop` call after selecting stream1. The ending GS image was black and there was no active frontend menu. Patch 0041 subsequently implemented Nop completion, but no newer full-game trace is available here to establish the next actual stop.

| Run identity | SHA-256 |
| --- | --- |
| Native executable | `457d2f9d80f5ad66c270a284b77e662490559d5f01bb080ffeba25167407bf92` |
| Runtime report | `f54d53d8c13703917d4bb62020013cb0c939d078010fcb1831205716015b5d19` |

Patches 0040–0043 add kernel alarm delivery, asynchronous Nop completion, test portability and asynchronous stream reads. Their tests are evidence of those service contracts, not proof that the complete game has passed the old stop or entered the menu. Original streaming shutdown, video decoding, audio output and the title/profile flow still need integration verification.

## Latest published screenshot

![Original no-card dialog with Yes selected through the original controller drivers](screenshots/mafia-linux-no-card.png)

This is the actual patch-0035 GS capture, not a new patch-0043 image. Cross dismissed the caution and Up selected Yes. After all keys were released, the host closed at 92.913 seconds and captured the surface after joining the EE worker. The exact-ELF snapshot identified the no-card class `0x004D8E80` and focus component 2, both controller records in mode `0x79`, released buttons and zero grace/settling counters. The surface is **640×448**, VBlank 664, with 221,360 nonblack pixels. PNG encoding preserves the original RGB bytes.

| Screenshot identity | SHA-256 |
| --- | --- |
| Original run executable | `780f1950478a7ad7dc7ee98d22369f9571ebfcec284393be6f715eb5d25049fa` |
| Raw PPM | `fd333e32dc14633e7713db95446c7964d267bc2b2eba47c67166990d02e0eb9b` |
| Published PNG | `9c629be34f8ef00e90c52f6e8e8fb8caf8620751c89ccdaed36958c33239cdc3` |
| Decoded RGB | `40187ea2cde72ecc6cd3a20a1ce05db18af41bca8db302fa759711be6e14f1bd` |
| Runtime report | `30c90fe3b4215959f89eabcce2682b7fb9b01a3423ba5c08399c4a9cbc3edebe` |

The earlier [autosave caution](screenshots/mafia-linux-autosave.png) and [Gathering splash](screenshots/mafia-linux-gathering.png) remain in the repository; their full capture provenance is retained in the linked historical report. No screenshot is evidence of main-menu entry or gameplay.

## Original input and static translation

The earlier original-data workspace verified a complete disc and 281 extracted files. Its availability is **not** assumed in the current source-only workspace.

| Item | Verified historical value |
| --- | --- |
| Disc/build | MAFIA; SLUS-20671; SYSTEM.CNF version 1.01; NTSC |
| ISO bytes / sectors | 4,128,538,624 / 2,015,888 × 2,048 bytes |
| ISO SHA-256 | `2cabb2b619bb7d43da2ded3fcdaf496b6f237385e05a9922d3bcf1cc82d4a5c0` |
| ELF bytes / entry | 5,613,500 / `0x00100008` |
| ELF SHA-256 | `adf4944c5cdac23fcc4e9af9ef3ec1ba6234497e57cec2af2ff1a7a15c62a46f` |
| Extracted files / bytes | 281 / 4,126,918,314 |
| ELF symbols | 15,236; 7,788 unique sized functions; no DWARF |
| Translated original functions | 7,791, including reviewed `_start`, `_exit` and `_root` ranges |
| Explicit stubs / skipped functions / instruction patches | 0 / 0 / 0 |
| Decoder failures / unhandled instructions / translator errors | 0 / 0 / 0 |
| Indirect-control-flow warnings | 2,506; runtime/differential validation still required |
| Generated files / source bytes | 7,794 / 238,938,733 |
| Generated manifest SHA-256 | `ae9f147aedbf649ab7f0dc4b862269a9d3085dd960724c3ed6938dbaaa8056a9` |

All functions previously compiled into 245 Linux game objects at `-O1 -g0`, with assertions and strict floating-point flags. Runtime-only updates through patch 0039 reran the real generator, checked byte-identical generated sources and rebuilt through ordinary CMake. No original EE/IRX instruction changes or native game-function substitutions were introduced. New patches have not been relinked with that corpus in this workspace.

## Reproduce and continue

The complete original-game build/run commands are in [README](../README.md). A bounded diagnostic launch needs the matching original ISO and extracted files, a freshly verified patched toolchain and regenerated build metadata. Use a new report and capture path for every run.

The IOP suite can be exercised separately after preparing the pinned sources:

```sh
python3 tools/bootstrap.py --tools-only --jobs 2
python3 tools/verify_upstream.py .deps/PS2Recomp
cmake -S .deps/PS2Recomp/ps2xIOP -B build/iop-only \
  -DCMAKE_BUILD_TYPE=Debug -DPS2X_IOP_BUILD_TESTS=ON
cmake --build build/iop-only --parallel 2
ctest --test-dir build/iop-only --output-on-failure --no-tests=error
```

For sanitizer checks, use a separate build directory with `-fsanitize=address,undefined -fno-omit-frame-pointer` in the C++ flags and `-fsanitize=address,undefined` in executable linker flags, then run CTest with `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1`. Sanitizer availability depends on the host toolchain; the observed result above is Linux GCC 14.2.

The next original-game verification is to rebuild the complete current patch set, repeat the real Up/Cross frontend flow and observe stream reads, cancellation, media output and title/profile transitions. Retain the exact executable hash, report, log and actual GS capture. Only mark menu entry after the original menu is visible and navigable. PCSX2 differential comparisons, all missions, saves, audio and performance remain separate milestones.
