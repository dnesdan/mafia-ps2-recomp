# Verified progress

Updated **2026-10-29**. **Main-menu entry, movie playback and gameplay remain unverified.** The recovered exact-profile game has been regenerated, rebuilt and executed with patches 0001–0047 in bounded Linux REA captures. The newest run exits the frontend and times out in original MPEG start-code scanning; no successful movie, menu or save milestone is claimed.

The [patch-0045 checkpoint](https://github.com/dnesdan/mafia-ps2-recomp/blob/f3d55ea43c21e88f33a029bb26e7a26e0e771f23/docs/PROGRESS.md) retains the direct/external blocking-sync tests. The [patch-0044 checkpoint](https://github.com/dnesdan/mafia-ps2-recomp/blob/4af8b23c05a78e40dec0e0c46321c0b28520fbab/docs/PROGRESS.md), [patch-0043 report](https://github.com/dnesdan/mafia-ps2-recomp/blob/59a0d214599524a6229235f9fa1e1880bc21269f/docs/PROGRESS.md) and [earlier detailed history](https://github.com/dnesdan/mafia-ps2-recomp/blob/8c4b294c26ab24daadac57f8f7f677a9e90d051d/docs/PROGRESS.md) preserve the preceding test and screenshot provenance.

## Latest source checkpoint: patch 0047

[Patch 0046](https://github.com/dnesdan/mafia-ps2-recomp/commit/6c710ae1259a89a71562b40b478503e04cad0a80) extends pending `sceCdSync` (`cdvdman:11`) modes **0 and 5** to one synchronous guest driver invocation inside a running managed IOP thread. It pins the exact caller while its separate driver CPU/native frame waits, schedules peers and returns without redispatching the active IOMAN Devctl import. The earlier direct managed-import and external startup/RPC paths remain available. Caller/driver stacks and GP, thread identity, read errors, successor commands and sticky failures are covered by authored tests.

Independent review found that the native return path checked scheduling priority before promoting due `DelayThread` sleepers. A higher-priority peer whose delay expired on that iteration could therefore run after the lower-priority native caller. [Patch 0047 / PR #2](https://github.com/dnesdan/mafia-ps2-recomp/pull/2) promotes due delays before that decision. The regression uses actual guest driver dispatch and fails on the unchanged patch-0046 runtime.

Pending waits recheck the shared command condition after waking because a callback can submit a successor command. Native-call owners reject destructive lifecycle/reset operations while their frames are pinned; independent concurrent native waits remain rejected because their frames cannot yet resume independently. The importer still gives compatible supplied exports precedence. Diagnostic execution budgets do not implement guest timed waits.

**Scope remains limited.** One nested managed guest driver wait is supported; independently resumable concurrent native waits are not. Pending timed/stream-wide waits, waits in IRQ or CPU-interrupt-disabled context, stream0 and raw CDVD IRQ programming remain outside the implemented service model.

The source reference is pinned PS2SDK [CDVDMAN `sceCdSync`](https://github.com/ps2dev/ps2sdk/blob/329e2bb43287e16af16c4dde1c614e2662c24468/iop/cdvd/cdvdman/src/cdvdman.c) and the [CDVDSTM stop/start flow](https://github.com/ps2dev/ps2sdk/blob/329e2bb43287e16af16c4dde1c614e2662c24468/iop/cdvd/cdvdstm/src/cdvdstm.c). This does not claim a PS2 firmware boot, cycle-exact drive timing or a verified complete movie-stop sequence.

### Executed checks

| Check | Observed result |
| --- | --- |
| Source suite, patch 0047 | 118 Python tests and 2 CTest checks passed. |
| Native host/runtime contracts, patch 0047 | 5/5 CTest checks passed. |
| Complete IOP suite, GCC 14.2 Debug, patch 0047 | 20/20 programs passed. |
| NCMD/read/abort/sync suite, patch 0047 | 64/64 groups passed. |
| Identical final tests on unchanged patch-0046 runtime | 63/64 passed; only the new due-delay priority regression failed. |
| Negative-control source check | The 95-file preceding IOP source was verified; only the two regression fixture files differ, with runtime source unchanged. |
| Generated integer operations, patch 0047 | 68 arithmetic cases passed under UBSan. |
| Full Linux and ARM64 macOS CI, patch 0045 | All four jobs passed in [run 22](https://github.com/dnesdan/mafia-ps2-recomp/actions/runs/40762352805). |
| Full Linux and ARM64 macOS CI, patch 0046 | All four jobs passed in [run 40837524243](https://github.com/dnesdan/mafia-ps2-recomp/actions/runs/40837524243). |
| Patch-0047 PR CI | All four jobs passed in [run 40841887834](https://github.com/dnesdan/mafia-ps2-recomp/actions/runs/40841887834) at code commit `8682be884382ed67caf332482c1b99b6379563e6`. |
| Current original-game execution | Two Linux headless REA captures: original autosave screen at 15 seconds, then frontend exit and MPEG scanning at the 90-second diagnostic limit. |
| Firmware execution or BIOS comparison | Not performed. The native kernel remains modeled. |

These are constructed-input tests. The independent review and reproduced priority failure establish the specific source correction; they do not establish a movie-stop sequence or original-game menu entry. The older optimized Clang and ASan/UBSan results apply to preceding checkpoints and have not been rerun locally for patch 0047.

| Source identity | SHA-256 |
| --- | --- |
| Patch 0046 | `4bdc2ad8c76d621a62adcd41d0b97209849c4a8e086916af1ca5ab4d57309369` |
| Patch 0047 | `da9a393a32a9e5664309e35caad9b80088e03a2e394a3c4ec928600ac647df0e` |

The dependency remains upstream `e5636981a1c9de368f6b71e819c3dc188dfedbf4` plus the reviewed patch stack. CI archives contain verified public IOP sources and test logs, not commercial inputs, generated game code, game executables or Git metadata. They expire after seven days.

### Preceding changes

Patch 0042 fixed Apple standard-library test portability while retaining concurrent controller sampling/reset checks. Patch 0043 added owned asynchronous stream1 sector reads. Patch 0044 added cancellation and corrected premature user callbacks when a stream callback submits a successor. All four Linux/macOS jobs passed for patches 0043 and 0044. Their detailed controls remain in the linked checkpoints.

## Original-game execution status

The user-supplied PS2 archive was recovered, its RAR CRC passed, and its ISO and ELF hashes exactly match the existing profile. All 281 disc files are extracted locally. The supplied 4 MiB BIOS is also available, but no firmware execution or BIOS comparison has been performed. A fresh original corpus was generated with the current verified patch stack; the native game build and two bounded executions completed. Private input files, share links and passwords are not stored in this repository or its CI artifacts.

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

### Latest original run: patch 0047

The current executable uses GCC 14.2, generated-code `-O1 -g0 -UNDEBUG`, strict floating-point flags and unity batches of 32. The regenerated source manifest is byte-identical to the historical corpus. REA **6.8.0** ran the actual executable through `capture_process_scenario`; it did not emulate or replay the game.

- **15-second control:** no input pulses. Original autosave caution is visible on the 640×448 GS capture, with 220,652 nonblack pixels and VBlank 258. The exact-ELF snapshot identifies `autosave-caution`.
- **90-second frontend flow:** Cross at 25 seconds, Up at 35 seconds and Cross at 42 seconds, each held for 800 host milliseconds through SIO2. The final menu chain is empty; 12 original IOP modules are loaded, with 9,650,165 IOP instructions and 688 SIO2 transfers. The final 640×448 GS capture is black at VBlank 685.
- **Observed end:** diagnostic timeout, exit 124, EE PC `0x003c3914`, `_nextStartCode + 92`, with branch context `_peepBit`. No strict unsupported-import error was reported. This is a bounded observation, not proof of an infinite loop or its cause. Movie playback, main-menu entry, gameplay and saves remain unverified.

Both REA captures report untruncated observations, quiesced settlement, verified owned-process cleanup and removed temporary roots. Process sampling, prelaunch executable identity, inherited environment and unobserved network activity retain REA's stated limitations. Local complete reports, capture Evidence and image bytes are retained outside the source repository; private share credentials are not included.

| Current-run identity | SHA-256 / Evidence ID |
| --- | --- |
| Native executable | `98de4944215cc5b99dacd93cd5086588e3ccc7d3c2908bab66727f15a966ad4c` |
| 15-second runtime report | `afe5fd01172b2d5b6124d798d10eea831ba76cf2b199296a497229c52e91127a` |
| 90-second runtime report | `8d3af3641263ca28ee72fa3ddd75743870ed4aae13820e6db96e67ff6211a2e5` |
| Autosave PPM | `fe1033ac946a2a5f8cb7a5cb3a2404320ceda20e75a3f8cbb58ba8e184c512ec` |
| Final black PPM | `00c470b9619bb218650748690cd9de4a6d885656deaf8d573a1468b394f3db4a` |
| Autosave REA Evidence | `ev_0ebeef2535c1230b5b1fbac93178f3512084d0bc131e28293c4ecf10c286b799` |
| Frontend-flow REA Evidence | `ev_1af817d09fc94fa30ab4dd40adbf9a90a337618c8a00586240b83fe8416547a0` |

### Historical original run: patch 0039

The Linux window run lasted 90.861 seconds and recorded 644 presentations, 674 SIO2 transfers, 10,300,542 IOP instructions and 12 loaded modules. Original `FileInfo` at EE address `0x1FFF920` contains trailer LSN **1,269,101** and size **68,435,972 bytes**, matching the independently checked original ISO.

Its strict stop was `cdvdman:46`, version `0x0101`, IOP PC `0x9D2B8`, return address `0x9C188`: original CDVDSTM's `sceCdNop` after selecting stream1. The final GS image was black with no active frontend menu. Later patches implement this service and related operations. The newer bounded patch-0047 trace above records a different end position, without establishing complete streaming correctness.

| Run identity | SHA-256 |
| --- | --- |
| Original executable | `457d2f9d80f5ad66c270a284b77e662490559d5f01bb080ffeba25167407bf92` |
| Original runtime report | `f54d53d8c13703917d4bb62020013cb0c939d078010fcb1831205716015b5d19` |

## Latest published screenshot

![Original no-card dialog with Yes selected through the original controller drivers](screenshots/mafia-linux-no-card.png)

This is the actual **patch-0035** capture, not a current-patch image. Cross dismissed the caution, Up selected Yes, and capture occurred after keys were released and the EE worker joined. The exact-ELF snapshot identified the original no-card class and selection, both controller records in mode `0x79`, and zero grace/settling counters. The surface is **640×448**, VBlank 664, with 221,360 nonblack pixels. PNG encoding preserves its RGB bytes.

| Capture identity | SHA-256 |
| --- | --- |
| Raw PPM | `fd333e32dc14633e7713db95446c7964d267bc2b2eba47c67166990d02e0eb9b` |
| Published PNG | `9c629be34f8ef00e90c52f6e8e8fb8caf8620751c89ccdaed36958c33239cdc3` |
| Decoded RGB | `40187ea2cde72ecc6cd3a20a1ce05db18af41bca8db302fa759711be6e14f1bd` |
| Runtime report | `30c90fe3b4215959f89eabcce2682b7fb9b01a3423ba5c08399c4a9cbc3edebe` |

Earlier [autosave caution](screenshots/mafia-linux-autosave.png) and [Gathering splash](screenshots/mafia-linux-gathering.png) captures remain in the repository with provenance in the historical report. None establishes main-menu entry or gameplay.

## Original input and translation

The recovered complete disc is MAFIA, **SLUS-20671 / VER 1.01 / NTSC**, with 281 extracted files totaling 4,126,918,314 bytes. ISO and ELF identities were freshly verified against the existing profile on 2026-10-29.

| Identity | Verified value |
| --- | --- |
| ISO bytes / sectors | 4,128,538,624 / 2,015,888 × 2,048 bytes |
| ISO SHA-256 | `2cabb2b619bb7d43da2ded3fcdaf496b6f237385e05a9922d3bcf1cc82d4a5c0` |
| ELF bytes / entry | 5,613,500 / `0x00100008` |
| ELF SHA-256 | `adf4944c5cdac23fcc4e9af9ef3ec1ba6234497e57cec2af2ff1a7a15c62a46f` |
| Original functions translated | 7,791, including reviewed `_start`, `_exit` and `_root` ranges |
| Explicit stubs / skipped functions / instruction patches | 0 / 0 / 0 |
| Generated manifest SHA-256 | `ae9f147aedbf649ab7f0dc4b862269a9d3085dd960724c3ed6938dbaaa8056a9` |
| Supplied BIOS bytes / SHA-256 | 4,194,304 / `6d23d001daf2a0fa8b381a5d49f51753c36e3622d0f04be46af1a0c548be4744` |

Fresh generation produced 7,792 C++ units for 7,791 original functions, with zero unresolved source lines and no stubs or instruction patches. Its source manifest is byte-identical to the historical value above. The current corpus compiled and linked with the verified runtime, then executed in the bounded captures described above. The 2,506 historical indirect-control-flow warnings still require broader runtime/differential validation.

## Reproduce and continue

Full original-game commands are in [README](../README.md). The matching ISO, complete extraction and freshly regenerated corpus are now available locally. Retain a new report, log, executable hash and actual GS capture for each new run.

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

Next integration work is to trace MPEG `_nextStartCode` / `_peepBit` and its media-feed path, distinguish slow progress from a persistent wait, and validate original stream reads, cancellation and synchronization. Only mark main-menu entry after the original menu is visible and navigable. The missing physical memory-card path, independently resumable concurrent native waits, broader timed synchronization, PCSX2 comparisons, saves, missions, audio and performance remain separate milestones.
