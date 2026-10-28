# Verified progress

Updated **2026-10-28**. **Main-menu entry, movie playback and gameplay remain unverified.** The last published original-game execution uses patches 0001–0039. Newer constructed-input tests do not change that historical run's result.

The [patch-0044 checkpoint](https://github.com/dnesdan/mafia-ps2-recomp/blob/4af8b23c05a78e40dec0e0c46321c0b28520fbab/docs/PROGRESS.md) retains cancellation tests and earlier execution identities. The [patch-0043 report](https://github.com/dnesdan/mafia-ps2-recomp/blob/59a0d214599524a6229235f9fa1e1880bc21269f/docs/PROGRESS.md) and [earlier detailed history](https://github.com/dnesdan/mafia-ps2-recomp/blob/8c4b294c26ab24daadac57f8f7f677a9e90d051d/docs/PROGRESS.md) preserve their complete test and screenshot provenance.

## Latest source checkpoint: patch 0045

[Commit b945332](https://github.com/dnesdan/mafia-ps2-recomp/commit/b94533276b589fd217f95fc5f33c467461cd384e) implements pending `sceCdSync` (`cdvdman:11`) modes **0 and 5** for the existing command model. A managed IOP caller waits on the real shared completion event without losing its import PC, argument registers, GP, return address or stack. It retries the command condition after waking: a stream callback may have submitted another read before that thread runs again.

External startup/RPC invocations use a different path. They keep their own CPU and native call stack while bounded IOP scheduling advances device completion, alarms and ready threads. Authored MIPS tests exercise this through an actual registered IOMAN Devctl callback with independent driver and caller stacks. Idle cycles count against that invocation's budget, so a permanently masked completion cannot hang the host indefinitely. This budget is a diagnostic execution bound, not an implementation of the guest's timed Sync modes.

The importer gives compatible supplied `sceCdSync` exports precedence and restricts the modeled fallback to the compatible major version. Read errors remain read errors after synchronization. Deleted events, callback faults and retired device lifetimes cannot become successful completion; full reset discards sleeping callers.

**Scope remains limited.** Pending timed/stream-wide waits, waits in IRQ or CPU-interrupt-disabled context, and blocking synchronous host invocations nested inside a running managed thread remain explicit unsupported boundaries. In particular, an external driver callback is not evidence that every nested managed-thread call is now resumable. Stream0 and raw CDVD IRQ programming also remain outside the implemented service model.

The source reference is pinned PS2SDK [CDVDMAN `sceCdSync`](https://github.com/ps2dev/ps2sdk/blob/329e2bb43287e16af16c4dde1c614e2662c24468/iop/cdvd/cdvdman/src/cdvdman.c) and the [CDVDSTM stop/start flow](https://github.com/ps2dev/ps2sdk/blob/329e2bb43287e16af16c4dde1c614e2662c24468/iop/cdvd/cdvdstm/src/cdvdstm.c). This does not claim a PS2 firmware boot, cycle-exact drive timing or a verified complete movie-stop sequence.

### Executed checks

| Check | Observed result |
| --- | --- |
| Complete IOP suite, GCC 14.2 Debug | 20/20 programs passed. |
| Complete IOP suite, Clang 17, `-O2`, assertions and strict floating-point flags | 20/20 programs passed. |
| Complete IOP suite, GCC ASan/UBSan with leak detection | 20/20 programs passed; no sanitizer findings. |
| NCMD/read/abort/sync suite | 51/51 groups passed. |
| Identical final tests on unchanged patch-0044 runtime | 37/51 passed: fourteen new regression groups fail; the unsupported-mode control and all thirty-six preceding groups pass. |
| Negative-control source check | Only the test runner and added fixture differ from the 93-file patch-0044 baseline; runtime source is unchanged. |
| Fresh patch application | All 94 IOP files byte-identical to the tested source. |
| Full Linux and ARM64 macOS CI, patch 0044 | All four jobs passed in [run 20](https://github.com/dnesdan/mafia-ps2-recomp/actions/runs/40757496945). |
| Full CI, patch 0045 | [Run 22](https://github.com/dnesdan/mafia-ps2-recomp/actions/runs/40762352805); `.recomp.json` records the latest confirmed checkpoint without assuming a queued job passed. |
| New original-game execution, BIOS comparison or screenshot | Not performed. |

Tests cover read/Nop/abort waits, low-priority peer progress, masked IRQs, successor commands, one-cycle host slices, event deletion, reset, external driver stacks, supplied exports, callback faults and unsuccessful media reads. Review passes covered continuation ownership, wakeup races, failure lifetime and platform/regression coverage. These are self-review and executed tests, not new independent reviewers or an original-game run.

| Source identity | SHA-256 |
| --- | --- |
| Patch 0045 | `b47c83411b5f7743a7198c4cb2ce4ee7ca8980524eb65e3b280e567c02889b30` |
| Baseline patch-0044 CI source archive | `e1ba87010a970a434f5aaff408be77c342da4f8f1630005919e4ab75c97b4062` |

The baseline archive records upstream `e5636981a1c9de368f6b71e819c3dc188dfedbf4` plus patches 0001–0044. CI archives contain verified public IOP sources and test logs, not commercial inputs, generated game code, game executables or Git metadata. They expire after seven days.

### Preceding changes

Patch 0042 fixed Apple standard-library test portability while retaining concurrent controller sampling/reset checks. Patch 0043 added owned asynchronous stream1 sector reads. Patch 0044 added cancellation and corrected premature user callbacks when a stream callback submits a successor. All four Linux/macOS jobs passed for patches 0043 and 0044. Their detailed controls remain in the linked checkpoints.

## Original-game execution status

The original-data workspace is still unreachable. Retried user-supplied shares and BIOS retrieval did not recover bytes in this environment; outbound downloads fail and the original machine's tunnel is inactive. The current workspace contains public IOP sources, not the original ISO, PC archive, extracted game files, BIOS or generated game corpus. No alternative commercial download was substituted. Private share links and passwords are not stored in this repository or its CI artifacts.

| Milestone | Historical evidence and remaining limit |
| --- | --- |
| Original ELF and entry | Exact hash/headers verified; entry `0x00100008` and original main execute. |
| Original IOP services | Supplied LOADFILE, CDVDFSV and FILEIO execute their entry functions and register services; later game modules also execute. Kernel/reset behavior is modeled, not a PS2 BIOS boot. |
| Original frontend graphics | Gathering splash, autosave caution and no-card dialog captured from the actual GS surface on Linux x86-64. |
| Original input | Up/Cross consumed through original SIO2MAN/PADMAN in full-game Xvfb/software-OpenGL runs. Separate original-driver tests verify Start/Cross press and release. Physical gamepads remain untested. |
| Memory-card query | Original MCSERV reply completes and the no-card dialog appears. No present card or working save system is claimed. |
| Movie initialization | Original CDVDSTM starts; stream initialization and layer-zero search advance; correct trailer metadata reaches EE memory. |
| Movie playback, main menu, missions and save/load | Not verified. |
| Complete original game on macOS ARM64 or Linux ARM64 | Not verified. macOS CI uses constructed input, not the game corpus. |

### Last recorded original run: patch 0039

The Linux window run lasted 90.861 seconds and recorded 644 presentations, 674 SIO2 transfers, 10,300,542 IOP instructions and 12 loaded modules. Original `FileInfo` at EE address `0x1FFF920` contains trailer LSN **1,269,101** and size **68,435,972 bytes**, matching the independently checked original ISO.

Its strict stop was `cdvdman:46`, version `0x0101`, IOP PC `0x9D2B8`, return address `0x9C188`: original CDVDSTM's `sceCdNop` after selecting stream1. The final GS image was black with no active frontend menu. Later patches implement this service and related operations, but no newer full-game trace is available here to establish the next actual stop.

| Run identity | SHA-256 |
| --- | --- |
| Original executable | `457d2f9d80f5ad66c270a284b77e662490559d5f01bb080ffeba25167407bf92` |
| Original runtime report | `f54d53d8c13703917d4bb62020013cb0c939d078010fcb1831205716015b5d19` |

## Latest published screenshot

![Original no-card dialog with Yes selected through the original controller drivers](screenshots/mafia-linux-no-card.png)

This is the actual **patch-0035** capture, not a patch-0045 image. Cross dismissed the caution, Up selected Yes, and capture occurred after keys were released and the EE worker joined. The exact-ELF snapshot identified the original no-card class and selection, both controller records in mode `0x79`, and zero grace/settling counters. The surface is **640×448**, VBlank 664, with 221,360 nonblack pixels. PNG encoding preserves its RGB bytes.

| Capture identity | SHA-256 |
| --- | --- |
| Raw PPM | `fd333e32dc14633e7713db95446c7964d267bc2b2eba47c67166990d02e0eb9b` |
| Published PNG | `9c629be34f8ef00e90c52f6e8e8fb8caf8620751c89ccdaed36958c33239cdc3` |
| Decoded RGB | `40187ea2cde72ecc6cd3a20a1ce05db18af41bca8db302fa759711be6e14f1bd` |
| Runtime report | `30c90fe3b4215959f89eabcce2682b7fb9b01a3423ba5c08399c4a9cbc3edebe` |

Earlier [autosave caution](screenshots/mafia-linux-autosave.png) and [Gathering splash](screenshots/mafia-linux-gathering.png) captures remain in the repository with provenance in the historical report. None establishes main-menu entry or gameplay.

## Original input and translation

The historical complete disc is MAFIA, **SLUS-20671 / VER 1.01 / NTSC**, with 281 extracted files totaling 4,126,918,314 bytes. It is not assumed present in the current workspace.

| Identity | Verified historical value |
| --- | --- |
| ISO bytes / sectors | 4,128,538,624 / 2,015,888 × 2,048 bytes |
| ISO SHA-256 | `2cabb2b619bb7d43da2ded3fcdaf496b6f237385e05a9922d3bcf1cc82d4a5c0` |
| ELF bytes / entry | 5,613,500 / `0x00100008` |
| ELF SHA-256 | `adf4944c5cdac23fcc4e9af9ef3ec1ba6234497e57cec2af2ff1a7a15c62a46f` |
| Original functions translated | 7,791, including reviewed `_start`, `_exit` and `_root` ranges |
| Explicit stubs / skipped functions / instruction patches | 0 / 0 / 0 |
| Generated manifest SHA-256 | `ae9f147aedbf649ab7f0dc4b862269a9d3085dd960724c3ed6938dbaaa8056a9` |

The original functions previously compiled into 245 Linux game objects with assertions and strict floating-point flags. Runtime-only updates through patch 0039 reran the actual generator and verified byte-identical generated sources. Later patches have not been relinked with that corpus here. The 2,506 indirect-control-flow warnings still require runtime/differential validation.

## Reproduce and continue

Full original-game commands are in [README](../README.md). A new diagnostic run needs the matching ISO/extracted files, verified current dependencies and genuinely regenerated metadata. Retain a new report, log, executable hash and actual GS capture for each run.

To exercise IOP without commercial input:

```sh
python3 tools/bootstrap.py --tools-only --jobs 2
python3 tools/verify_upstream.py .deps/PS2Recomp
cmake -S .deps/PS2Recomp/ps2xIOP -B build/iop-only \
  -DCMAKE_BUILD_TYPE=Debug -DPS2X_IOP_BUILD_TESTS=ON
cmake --build build/iop-only --parallel 2
ctest --test-dir build/iop-only --output-on-failure --no-tests=error
```

The observed Linux GCC sanitizer build uses `-fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie` in C++ flags and `-fsanitize=address,undefined -no-pie` in executable linker flags, with `ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1`. The non-PIE flags are Linux test configuration, not macOS build requirements.

Next integration work is to recover the complete original-data workspace, rebuild the current patch set and repeat the real Up/Cross frontend flow through stream reads, cancellation and synchronization. Trace any remaining unsupported call before extending its contract. Only mark main-menu entry after the original menu is visible and navigable. Broader timed/nested synchronization, PCSX2 comparisons, saves, missions, audio and performance remain separate milestones.
