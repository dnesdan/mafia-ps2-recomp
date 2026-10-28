# Verified progress

Updated **2026-10-28**. **Main-menu entry, movie playback and gameplay remain unverified.** The last published original-game execution uses patches 0001–0039. The newer service implementations and constructed-input tests below do not change that historical run's result.

The [patch-0043 checkpoint](https://github.com/dnesdan/mafia-ps2-recomp/blob/59a0d214599524a6229235f9fa1e1880bc21269f/docs/PROGRESS.md) retains its complete test, translation and screenshot provenance. Earlier per-patch execution traces remain in the [patch-0041 report](https://github.com/dnesdan/mafia-ps2-recomp/blob/8c4b294c26ab24daadac57f8f7f677a9e90d051d/docs/PROGRESS.md).

## Latest source checkpoint: patch 0044

[Commit 5ba829b](https://github.com/dnesdan/mafia-ps2-recomp/commit/5ba829bd5cb57bf6c824da3251a41b6d92fb3352) adds `sceCdBreak` (`cdvdman:39`) to the existing normal/stream1 command model. An accepted abort drops pending payload ownership before another checkpoint can touch media or a released/reused destination. Already-copied bytes remain copied. Completion is deferred, respects live interrupt masks and callback registration, and distinguishes the stream callback's argument 1 from the ordinary user's Break reason 8. Repeated aborts cannot postpone or duplicate completion.

Idle Break sets the aborted error and shared event without inventing a callback, except when the primary reported-timeout path requires abort recovery. Timeout retirement, reset, supplied-export precedence and major-version checks remain explicit. Blocking `sceCdSync` while a command is pending, stream0 and raw CDVD IRQ programming are **still unsupported**. This is not a claim that the whole original streaming-stop sequence now works.

Review also found a real defect in patch 0043: after the stream callback submitted another Read2 or Nop, the retired command could still deliver an early user completion. Patch 0044 suppresses that callback while the successor owns completion. The regression for this defect fails on the preceding implementation before it needs `sceCdBreak`.

Behavior was checked against pinned PS2SDK [CDVDMAN](https://github.com/ps2dev/ps2sdk/blob/329e2bb43287e16af16c4dde1c614e2662c24468/iop/cdvd/cdvdman/src/cdvdman.c), especially `sceCdBreak`, `cdvdman_intr_cb` and `sceCdSync`, and [CDVDSTM](https://github.com/ps2dev/ps2sdk/blob/329e2bb43287e16af16c4dde1c614e2662c24468/iop/cdvd/cdvdstm/src/cdvdstm.c), including its stop/timeout flow. The existing 128-cycle asynchronous delay remains a bounded service model, not a physical-drive timing claim. Reentrant cancellation inside host file I/O is rejected rather than pretending that the synchronous host stack is a guest DMA operation.

### Executed checks

| Check | Observed result |
| --- | --- |
| Complete IOP suite, GCC 14.2 Debug | 20/20 programs passed. |
| Complete IOP suite, Clang 17, `-O2`, assertions and strict floating-point flags | 20/20 programs passed. |
| Complete IOP suite, GCC ASan/UBSan with leak detection | 20/20 programs passed; no sanitizer findings. |
| Corrected NCMD/stream/abort suite | 36/36 groups passed. |
| Identical final tests on unchanged patch-0043 runtime | 24/36 passed; all twelve added regression groups failed. |
| Local patch application and source parity | All 93 IOP files identical to the tested source. |
| Published CI source archive compared to the local tested tree | All 93 IOP files identical; patch provenance hash also matches. |
| Previous full Linux and ARM64 macOS CI, patch 0043 | All four jobs passed in [run 18](https://github.com/dnesdan/mafia-ps2-recomp/actions/runs/40733776289). |
| Full Linux and ARM64 macOS CI for patch 0044 | [Run 20](https://github.com/dnesdan/mafia-ps2-recomp/actions/runs/40757496945); the latest confirmed checkpoint is recorded separately in `.recomp.json`. |
| New original-game execution or screenshot | Not performed. |

Review passes covered API and callback ordering, memory/reset lifetimes, and platform/regression coverage. Findings were corrected before publishing. These are self-review and executed-test results; no new independent reviewer or original-game test is claimed.

| Patch/source identity | SHA-256 |
| --- | --- |
| Patch 0044 | `3c28eacf1a762e20ab69de7fdb7bea35cbc5b29c7029bd8e8dce188c9127d4b4` |
| Downloaded patch-0044 CI source archive | `e1ba87010a970a434f5aaff408be77c342da4f8f1630005919e4ab75c97b4062` |

The source archive was generated from commit `5ba829bd5cb57bf6c824da3251a41b6d92fb3352` and records upstream `e5636981a1c9de368f6b71e819c3dc188dfedbf4` plus patches 0001–0044. CI artifacts contain verified public IOP sources and test logs, not commercial data, generated game code, native game executables or Git metadata. They expire after seven days.

### Preceding source changes

Patch 0042 replaced `std::jthread` in the SIO2 concurrency fixture with `std::thread`, preserving its existing stop/join paths and assertions. This fixed the observed Apple standard-library compilation failure; the complete Linux/macOS matrix passed in runs 16 and 17.

Patch 0043 implements asynchronous stream1 `sceCdRE` reads of actual 2048-byte sectors from the selected medium. Payload writes occur at a later device checkpoint, before callback delivery. Failed/short reads do not partially overwrite destinations; allocation identities are checked before and after host I/O. Its original suite passed 24/24 groups, versus 15/24 on its predecessor. All twenty IOP programs passed under GCC, optimized Clang and sanitizers; the full Linux/macOS matrix subsequently passed in run 18. Patch 0044 adds the cancellation and callback-order correction described above.

## Original-game execution status

The original-data workspace is still unreachable. The current container has recovered public IOP sources, but not the original ISO, extracted files or generated game corpus. The supplied share was not successfully recovered here; no replacement game or BIOS was downloaded.

| Milestone | Historical evidence and remaining limit |
| --- | --- |
| Original ELF and entry | Exact hash/headers verified; entry `0x00100008` and original main execute. |
| Original IOP services | Supplied LOADFILE, CDVDFSV and FILEIO execute their entry functions and register services; later game modules also execute. Kernel/reset behavior is modeled, not a PS2 BIOS boot. |
| Original frontend graphics | Gathering splash, autosave caution and no-card dialog captured from the actual GS surface on Linux x86-64. |
| Original input | Up/Cross consumed through the original SIO2MAN/PADMAN route in full-game Xvfb/software-OpenGL runs. Separate original-driver tests verify Start/Cross press and release. Physical gamepads remain untested. |
| Memory-card query | Original MCSERV reply completes and the no-card dialog appears. No present memory card or working save system is claimed. |
| Movie initialization | Original CDVDSTM starts; stream initialization and layer-zero file search advance; correct trailer metadata reaches EE memory. |
| Movie playback, main menu, missions and save/load | Not verified. |
| Complete original game on macOS ARM64 or Linux ARM64 | Not verified. macOS CI uses constructed input, not the original game corpus. |

### Last recorded original run: patch 0039

The Linux window run lasted 90.861 seconds and recorded 644 presentations, 674 SIO2 transfers, 10,300,542 IOP instructions and 12 loaded modules. Original `FileInfo` at EE address `0x1FFF920` contains trailer LSN **1,269,101** and size **68,435,972 bytes**, matching the independently checked original ISO.

Its strict stop was `cdvdman:46`, version `0x0101`, IOP PC `0x9D2B8`, return address `0x9C188`: original CDVDSTM's `sceCdNop` call after selecting stream1. The final GS image was black, with no active frontend menu. Later patches implement this service and related operations, but no newer full-game trace is available here to establish the next actual stop.

| Run identity | SHA-256 |
| --- | --- |
| Original executable | `457d2f9d80f5ad66c270a284b77e662490559d5f01bb080ffeba25167407bf92` |
| Original runtime report | `f54d53d8c13703917d4bb62020013cb0c939d078010fcb1831205716015b5d19` |

## Latest published screenshot

![Original no-card dialog with Yes selected through the original controller drivers](screenshots/mafia-linux-no-card.png)

This is the actual **patch-0035** capture, not a patch-0044 image. Cross dismissed the caution, Up selected Yes, and the host closed after all keys were released. Capture occurred after joining the EE worker. The original no-card class and focus component were identified in the exact-ELF snapshot; both controller records had mode `0x79`, released buttons and zero grace/settling counters. The surface is **640×448**, VBlank 664, with 221,360 nonblack pixels. PNG encoding preserves its RGB bytes.

| Capture identity | SHA-256 |
| --- | --- |
| Raw PPM | `fd333e32dc14633e7713db95446c7964d267bc2b2eba47c67166990d02e0eb9b` |
| Published PNG | `9c629be34f8ef00e90c52f6e8e8fb8caf8620751c89ccdaed36958c33239cdc3` |
| Decoded RGB | `40187ea2cde72ecc6cd3a20a1ce05db18af41bca8db302fa759711be6e14f1bd` |
| Runtime report | `30c90fe3b4215959f89eabcce2682b7fb9b01a3423ba5c08399c4a9cbc3edebe` |

The earlier [autosave caution](screenshots/mafia-linux-autosave.png) and [Gathering splash](screenshots/mafia-linux-gathering.png) remain in the repository. Their complete provenance is in the linked historical report. None establishes main-menu entry or gameplay.

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

The original functions previously compiled into 245 Linux game objects with assertions and strict floating-point flags. Runtime-only updates through patch 0039 reran the actual generator and verified byte-identical generated sources. The later patches have not been relinked with that corpus here. The 2,506 indirect-control-flow warnings still require runtime/differential validation; compilation and unit tests do not establish complete game coverage.

## Reproduce and continue

Full original-game commands are in [README](../README.md). A new diagnostic run needs the matching ISO/extracted files, verified current dependencies and genuinely regenerated metadata. Retain a new report, log, executable hash and GS capture for each run.

To exercise the IOP suite without commercial input:

```sh
python3 tools/bootstrap.py --tools-only --jobs 2
python3 tools/verify_upstream.py .deps/PS2Recomp
cmake -S .deps/PS2Recomp/ps2xIOP -B build/iop-only \
  -DCMAKE_BUILD_TYPE=Debug -DPS2X_IOP_BUILD_TESTS=ON
cmake --build build/iop-only --parallel 2
ctest --test-dir build/iop-only --output-on-failure --no-tests=error
```

For sanitizer checks, configure a separate build with `-O1 -g1 -fsanitize=address,undefined -fno-omit-frame-pointer` in C++ flags and `-fsanitize=address,undefined` in executable linker flags. Run with `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1`. The reported sanitizer run is Linux GCC 14.2; availability depends on the host toolchain.

The next service gap is the original blocking CDVD synchronization path: suspend and resume the actual IOP continuation while the accepted command and interrupt progress, without pretending an incomplete wait succeeded. Then rebuild/run the complete current game corpus, repeat Up/Cross through the frontend and verify stream reads, cancellation, media output and the title/profile flow. Only mark main-menu entry after the original menu is visible and navigable. PCSX2 comparisons, saves, missions, audio and performance remain separate milestones.
