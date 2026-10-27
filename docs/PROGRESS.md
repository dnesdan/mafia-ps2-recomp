# Verified progress

Evidence collected on **2026-10-27**. The original translated game now renders its **Gathering startup splash on Linux x86-64**. Menu, complete initialization and gameplay remain unverified. The current Linux build uses GCC 13.3; Apple Silicon remains a supported build target with separate execution evidence.

## Execution status

| Milestone | Current evidence |
| --- | --- |
| Verify and load the actual ELF | Passed: exact original SHA-256 and ELF headers checked by the native host. |
| Execute original entry and main | Passed: original entry 0x00100008 and original main execute. |
| Initialize IOP services | Original LOADFILE, CDVDFSV and FILEIO execute their entry functions and register their RPC services. Later game-supplied IRX modules also execute. The kernel/reset model is described below. |
| Render original startup graphics | Passed: the Gathering publisher splash is captured from the actual GS presentation surface. |
| Complete initialization and enter the menu | Not verified; the latest strict stop is described below. |
| Missions, player, vehicles, objectives, saves and all modes | Not verified. |

All **7,791 translated game functions** compiled and linked into the native Linux executable. The diagnostic build uses -O0 -g0 and unity batches of 32. Runtime corrections reuse the same 245 compiled game objects; every size and modification time is checked against the retained build record. The original EE code has not been edited or replaced with native SDK functions.

### Actual screenshot

![Gathering startup splash produced by the original translated program](screenshots/mafia-linux-gathering.png)

This is the original **640×448** startup splash, captured headlessly and encoded as PNG with exactly the same RGB pixels. It is not a menu or gameplay screenshot.

| Capture provenance | SHA-256 |
| --- | --- |
| Raw source PPM | 67ff60d1d8e3f3c114e4fcfdef6aa4b68b960321d1efd3b697d0e8b431d4c39d |
| Published PNG | af7aab0d84b7a2b12744f7847e097231f14ce3ca1d09ac6489ce4e80fcf44ea8 |
| Decoded RGB bytes | 7e2ab2785b7132569849a08f9f0c56fbe98cc6e8bcc6f5e12d291e87faac81b2 |

The captured image has 21,092 nonblack pixels and was checked visually and against its raw pixels by independent reviewers. A later frame after sound initialization begins has slightly different pixels from the splash animation, but still shows the same startup logo. A nonblack count alone is never treated as proof of a correct game frame. The host's automatic game_frame_verified and gameplay_verified fields remain false; the manually reviewed startup image is a separate milestone. Headless execution has no window presentations.

### Latest original-program run: patches 0001–0026

The native program now passes the earlier sound-initialization stops and reads the original animation preload. Patch 0024 reuses freed IOP allocations; four authored churn/fragmentation regressions fail before it and pass after it. Patch 0025 retires delayed register loads before modeled imports or original guest exports; four authored instruction tests reproduce stale arguments, overwritten results and stale return addresses before the correction. These are runtime changes; the original EE and IRX instructions remain unchanged. Audible playback has not been verified.

The missing original `FQL/PRL_ANIM.FQL` was restored from the supplied archive and verified against its full-disc inventory. Original `FQLoadBegin` first attempts a read-only open, then falls back to flags `0x202` after failure. Patch 0026 follows the actual optical driver: conventional open flags still select an existing read-only file, and a missing file returns `ENOENT` without creating data. Writes to live modeled read-only handles return `EBADF`; foreign handles retain the original guest-export route. Independent review caught and fixed an initial implementation that shadowed that route.

A 20-second diagnostic run sampled a valid `memcpy` within `dtaRead` and `LoadAnimation5ds`. Extending the deadline exposed the next actual failure, rather than proving a permanent loading loop:

| Observation | Recorded result |
| --- | --- |
| Process status | `missing-dispatch`; stopped before the 120-second diagnostic deadline |
| Original handler / branch source | `_sceSifCmdIntrHdlr`, `0x00407BB0` |
| Invalid return target | `0xEFFA00F6` |
| Callback stack pointer | `0x01FFBFF0` |
| Hardware work | 5,678 DMA starts; 12 GIF copies; 12 VIF writes |
| IOP snapshot | 6,236,756 interpreted instructions; 11 loaded-module records; 30 threads; 15 RPC servers |
| Latest GS capture | 640×448, VBlank 1,024, 21,092 nonblack pixels; Gathering splash |
| Native executable size | 224,391,440 bytes |
| Native executable SHA-256 | `a0e2edabb0083b2499f56568ba3489a3416c36b9f1c4cc36cba31e8ad1aa7958` |

The original program declares its main stack over `[0x01F00000, 0x02000000)`. The current runtime also takes asynchronous callback stacks from that interval. The observed FILEIO tail-copy spans the SIF handler's saved return address. The strict invalid-return check stops execution; no address or game-code patch conceals the failure. A disjoint, owned callback-stack allocation is the next implementation step.

The fresh runtime and host were linked with 245 unchanged game objects. Every object digest and all game-facing include trees were verified, together with all 7,794 generated source/header hashes. Runtime-only changes did not retranslate the game. The final capture is byte-identical to the already reviewed raw source of the published screenshot.

IOP reset is **modeled**, including the kernel and allocator. The supplied replacement image's original LOADFILE, CDVDFSV and FILEIO complete their own entry and RPC registration before reset completion is reported. No PS2 ROM boot sequence or BIOS execution is claimed.

## Original input

The complete user-supplied archive was streamed through UnRAR with successful CRC verification. Every ISO byte and file was hashed. Only a verified subset is retained locally because the complete input and build exceed available workspace capacity.

| Observation | Verified result |
| --- | --- |
| Archive bytes / SHA-256 | 3,591,273,732 / 24b0e1c665c1b38528427eb2598e668d83300febb1bd80a1477acfa83b3af769 |
| ISO bytes / sectors | 4,128,538,624 / 2,015,888 sectors of 2,048 bytes |
| ISO SHA-256 | 2cabb2b619bb7d43da2ded3fcdaf496b6f237385e05a9922d3bcf1cc82d4a5c0 |
| Volume / inventory | MAFIA; 281 files, 16 directories |
| Build identity | SLUS-20671, SYSTEM.CNF version 1.01, NTSC |
| Boot ELF bytes / entry | 5,613,500 / 0x00100008 |
| ELF SHA-256 | adf4944c5cdac23fcc4e9af9ef3ec1ba6234497e57cec2af2ff1a7a15c62a46f |
| Retained original files | 52 files / 695,391,444 bytes |

Retained inputs include the ELF, SYSTEM.CNF, all eleven standalone IRX modules, IOPRP271.IMG, six DTA archives, selected FQL files SOUND.DAT and PRL_ANIM.FQL. The original menu files 00MENU.FQL and 00MENU_F.FQL and both small startup logo videos have also been restored. Their bytes, the full archive stream and the full ISO stream were independently reverified against the original hashes.

Restoring the genuinely missing SOUND.DAT removed the earlier original-program abort during sound initialization without a native-code change. INTRO.PSS, other large media and some FQL files remain absent. There is no retained complete ISO, so raw-sector requests and later missing files can still block execution. This subset is not a complete installation. No BIOS was downloaded.

## Static translation

PS2Recomp is pinned to **e5636981a1c9de368f6b71e819c3dc188dfedbf4** plus the reviewed generic patches in this repository.

| Check | Result |
| --- | --- |
| ELF symbols | 15,236; 7,788 unique sized function symbols; no DWARF |
| Additional reviewed boot ranges | _start, _exit and _root |
| Original functions translated | 7,791 |
| Explicit stubs / skipped functions / instruction patches | 0 / 0 / 0 |
| Decoder failures / unhandled instructions / translator errors | 0 / 0 / 0 |
| Indirect-control-flow warnings | 2,506; further runtime/differential validation is required |
| Generated files / C++ translation units | 7,794 / 7,792 |
| Generated source and header bytes | 238,938,733 |
| Generated manifest SHA-256 | ae9f147aedbf649ab7f0dc4b862269a9d3085dd960724c3ed6938dbaaa8056a9 |
| Named native SDK replacements / silent placeholders | 0 / 0 |

The initial analyzer swept WAX data as instructions and proposed automatic bindings. That diagnostic corpus was excluded. The final build uses verified symbol extents, explicit boot boundaries and disabled automatic host bindings. GNU readelf/nm and the checked Python parser inspected the ELF; no Ghidra execution is claimed.

Function-table compaction preserved all 499,431 non-null slots, the 1,004,839-slot bounds, holes and owners. The expanded ordered mapping digest remains 0e4c8288fc46962060fca25e307c5a127e6009d66d81f8866bac58913cbb6b02. Runtime-only updates revalidate source patches, tool binaries, generation metadata and unchanged game objects without retranslating the corpus.

## Executed checks and reviews

| Suite / check | Current observed result |
| --- | --- |
| Fresh upstream runtime, patches 0001–0026 | 510 passed, 0 failed |
| All rebuilt IOP CTest programs | 13 passed, 0 failed |
| Strict-loader cases, including pending loads and stack ownership | 31/31; all four new pending-load regressions failed before their fix |
| IOMAN file cases, including optical flags and supplied-export priority | 17/17; the original-export regression caught a review finding |
| Native host/runtime contracts, current Linux build | Five CTests passed |
| Python disc/ELF/profile/generation/report suite | 118 passed, 0 failed |
| Exact pinned source plus all reviewed patches | Passed; no extra source inputs |
| Prior emitted integer arithmetic UBSan suite | 68 cases passed on Linux and ARM64 macOS |
| Prior GS-cache suite | 45 passed; not rerun for unrelated IOP-only corrections |

Independent reviews covered transport framing and ordering, reset lifetime, strict loading, callback and buffer ownership, file and descriptor lifetime, VIF stream/interrupt behavior, native diagnostics, capture integrity and Linux/macOS launch paths. Findings were corrected with focused regressions. Host parser/capture sanitizer checks passed; the complete original game has not been run under ASan.

The previous six-patch baseline passed all four Linux and ARM64 macOS jobs in [Actions run 5](https://github.com/dnesdan/mafia-ps2-recomp/actions/runs/40526989706). That result does not establish macOS execution of the newer runtime changes or the original game.

Review found that the old workflow omitted the upstream runtime and IOP suites. The updated workflow explicitly builds and runs the main runtime executable and all thirteen IOP test programs on both platforms, using a separate pinned build directory. All selected suites were freshly built and run locally. The enum-coverage test requires the upstream source working directory; CI now launches the suite there. The updated remote CI result remains pending.

| Platform | Evidence and remaining limit |
| --- | --- |
| Linux x86-64 | Complete game corpus compiled; original entry, main, IOP services and startup splash executed headlessly. |
| Apple Silicon macOS | Prior synthetic host/runtime/toolchain CI passed; current changes and complete original game execution still require platform validation. |
| Linux ARM64 | Build scripts accept the platform; original game execution has not been tested. |
| Desktop window and controls | Supported host paths exist; this environment's evidence is headless. |

## Reproduce and continue

Build from a complete, matching disc using the platform commands in [README](../README.md). For a bounded Linux run:

~~~sh
export MAFIA_PS2_ISO="/absolute/path/to/Mafia.iso"
python3 tools/bootstrap.py --diagnostic
bash tools/test.sh
bash tools/build_linux.sh
bash tools/run_linux.sh --elf .work/extracted/SLUS_206.71 \
  --data-root .work/extracted --iso "$MAFIA_PS2_ISO" \
  --headless --timeout-ms 10000 --report .work/runtime.json \
  --capture-frame .work/frame.ppm
~~~

Use build_macos.sh and run_macos.sh on Apple Silicon. Capture requires a new destination path; it does not overwrite existing output. Each fresh run should retain its report, log, exact executable/source provenance and actual GS capture.

The immediate objective is to complete original initialization and reach the menu. The next blocker is separation of EE callback stacks from the original main stack, followed by a new original-program run. After menu execution, verification must cover original input handling, media, saves and gameplay, then deterministic comparisons against PCSX2. No FPS, mission completion or visual equivalence is inferred from compilation or unit tests.
