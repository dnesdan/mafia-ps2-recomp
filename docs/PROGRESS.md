# Verified progress

Evidence updated on **2026-10-28**. The original translated game renders its **autosave caution screen and no-memory-card dialog in a Linux x86-64 window and consumes real keyboard input through its original drivers**. Cross dismisses the caution, Up selects Yes, and Cross confirms continuation without a memory card. The original program advances into movie initialization, initializes CDVDSTM through its own registered driver and reaches the original file-search reply. Its current strict stop is the 36-byte secondary SIF DMA in that reply. Main-menu entry and gameplay remain unverified. The Linux compiler is GCC 13.3; Apple Silicon remains a supported build target with separate execution evidence.

## Execution status

| Milestone | Current evidence |
| --- | --- |
| Verify and load the actual ELF | Passed: exact original SHA-256 and ELF headers checked by the native host. |
| Execute original entry and main | Passed: original entry 0x00100008 and original main execute. |
| Initialize IOP services | Original LOADFILE, CDVDFSV and FILEIO execute their entry functions and register their RPC services. Later game-supplied IRX modules also execute. The kernel/reset model is described below. |
| Render original startup/frontend graphics | Passed: both the Gathering publisher splash and the original autosave caution screen are captured from the actual GS presentation surface. |
| Complete the original memory-card query | Passed with patch 0030: original MCSERV secondary DMA completes and the original no-card dialog is displayed. No memory card is emulated as present. |
| Original controller transport | Passed: original-driver tests verify Start and Cross press/release in both original PADMAN DMA buffers. Fresh full-game window runs also consume Up and Cross, with mode 0x79 and no controller grace period. |
| Enter original movie initialization | Passed: patches 0034–0038 resolve the original VSync poll, exact module-name lookup, original CDVDSTM startup/driver dispatch, stream callback registration and layer-zero file search. The next strict boundary is its partial-quadword secondary SIF DMA reply. |
| Complete initialization and enter the main menu | Not yet verified. The current capture after movie initialization is black and is not treated as a menu image. |
| Missions, player, vehicles, objectives, saves and all modes | Not verified. |

All **7,791 translated game functions** were freshly compiled and linked after workspace recovery at `-O1 -g0`, using unity batches of 32, assertions and strict floating-point flags. The full original disc and all 281 extracted files are retained and verified. The initial fresh build took 1,443 seconds and passed all five native CTests. Updates through patch 0038 ran the real generator again, verified all 7,794 source/header files and their manifest were byte-identical, published only genuine new generation metadata, and rebuilt through ordinary CMake. The 245 generated game objects retained their hashes and modification times. Original EE and IRX instructions remain unchanged.

### Fresh original window execution: patches 0033–0038

The actual native executable was run in an isolated X11 display using Xvfb and software OpenGL. Released keyboard pulses passed through the normal window backend and modeled physical SIO2 port. Original SIO2MAN, PADMAN and the EE frontend consumed them. The screen visibly changed from the autosave caution to the no-card dialog, then from No to Yes. After confirmation, both original controller records contained mode `0x79`, Cross buttons `0xBFFF` and pressure `0xFF`; the original frontend recorded Cross and result `0x10001A` with both its settling counter and grace counter zero. These observations verify normal Up/Cross input in the full original program. Start has separate original-driver transport evidence but has not yet been exercised by the full native title flow.

The patch-0033 run then waited in the original `VSync` status poll before movie setup. Patch 0034 latches INTC_STAT at the existing VBlank phase events, independently of masks, while preserving selective W1C and the existing callback route. All four added regression groups fail against the preceding source; the corrected complete runtime suite passes 535/535. The actual patch-0034 run passes that poll and reaches `SearchModuleByName("cdvdstm")` in the original loader.

Patch 0035 adds that exact generic registry query. It returns the greatest matching loaded ID or `KE_UNKNOWN_MODULE` (`-202`), and preserves compatible original-export precedence. A filename stem is not an alias for the declared module name. In the fresh original run, the negative query is followed by the game's ordinary load of `MODULES/CDVDSTM.IRX`, whose declared name is `cdvd_st_driver`. Its original entry executes and returns resident/removable status 2. That run then stops at `ioman:31`, command `0x4396`, targeting the registered `cdrom_stm0:` device.

Patch 0036 forwards this generic Devctl operation to the original registered callback, preserving its seven O32 arguments and driver GP. The actual run enters CDVDSTM's stream initialization and stops at stream1 callback registration. Patch 0037 retains that live callback independently of the ordinary CD/DVD callback, without invoking it at registration. The next original run completes initialization and requests layer-zero lookup of the trailer through `cdvdman:84`.

Patch 0038 reads ISO9660 metadata from the same selected medium used for sector reads. The layer-search record is 36 bytes, including separate date and flags fields; legacy search keeps its bounded 32-byte output. A configured image cannot silently fall back to unrelated extraction-tree sectors. An independent original-media probe returns trailer LSN 1,269,101 and size 68,435,972 bytes, with exact metadata SHA-256 `dea2ba378a9757646c467550c9adf86422909ec09f6bf5152ebcf9f27178f635`; three sector samples match the extracted original video. This probe verifies metadata and bytes, not movie playback.

The actual patch-0038 run returns from the layer-search call into original CDVDFSV and reaches its secondary `sceSifSetDma` descriptor: source `0x17DC8`, destination `0x50C9C0`, size 36, attributes 0. The transport currently rejects that partial final quadword. The original SIFMAN tag builder describes nine source words and three EE quadwords; supporting that shape requires the real FIFO padding rule and ordered delivery before RPC completion. The native report does not capture the final IOP metadata bytes, so the separate original-media probe remains distinct evidence.

| Fresh native run | Executable SHA-256 | Observed result |
| --- | --- | --- |
| 0033 | `2baac57f875f7c84f5aa8609609525041f6b6c797be8c366d8956090e2edaa18` | Up/Cross consumed; original VSync poll blocks movie setup. |
| 0034 | `27bc913ef2350dad2e4d0f7dcf147b910292e8a5be4a10438f285ba842f218f1` | VSync poll passes; strict stop at MODLOAD module-name lookup. |
| 0035 | `780f1950478a7ad7dc7ee98d22369f9571ebfcec284393be6f715eb5d25049fa` | Module-name lookup and original CDVDSTM startup pass; strict stop at streaming Devctl forwarding. |
| 0036 | `57de1505852bcbc971e273aa60193a08b57dbea44dd4ec0de8e85b48787856b8` | Original driver callback executes; strict stop at stream1 callback registration. |
| 0037 | `53ef6204b3fb69d786c4494bc394e69c8b5cbf0394724c38c183586721318bae` | Stream initialization advances; strict stop at layer-zero file search. |
| 0038 | `72fa1dbcf93b2885ac4ade9dda97f7bbc0b9d4dcd95382c78fc8f088faaeb364` | Layer-search call returns; original 36-byte secondary SIF DMA reaches the strict transport boundary. |

The latest 74.023-second patch-0038 run records 566 window presentations, 596 SIO2 transfers and 12 loaded guest modules. The after-join snapshot has no active frontend menu and its current 640×448 GS capture has zero nonblack pixels. These values do not establish video playback or menu entry. Its report SHA-256 is `a51f135b577fbdeb8ad0c4519b190c8294ba49f8f4a026b389eaa7e829ecb0ba`. A separate patch-0035 run was closed while the original no-card dialog remained visible, producing the GS screenshot below. Audio output was unavailable in the isolated environment and is not claimed as verified.

### Current controller and scheduling corrections: patches 0032–0033

Patch 0032 models bounded SIO2 PIO transactions, two ordinary DualShock2 ports and original IRQ17 completion. Host keyboard or headless timeline samples reach this physical device; original SIO2MAN, PADMAN and SIF DMA perform the remaining work. The model handles configuration and pressure-mode negotiation, input release and absent-device acknowledgements. Memory-card probes report absence. SIO2 DMA channels 11/12 and multitap are outside the verified scope.

An earlier full-game controller prototype, before workspace recovery and patch 0033, stopped making frontend progress with the original controller warning over the caution screen. It remained alive until a 330.162-second diagnostic timeout. The host recorded 157,741 samples, but the original modules completed only 250 SIO2 transfers, ending in configuration command 0x43. Host sampling was therefore not treated as successful game input. That historical executable is separate from the freshly reconstructed patch hashes below.

Fresh controlled runs of the restored original drivers reproduced the cause. A host execution budget had been incorrectly treated as a guest-thread yield. Equal-priority work could then starve the original PADMAN continuation. Patch 0033 preserves a running thread across budget boundaries, orders ready peers by FIFO and retains explicit guest rotation, higher-priority preemption, interrupt gates and callback bounds.

| Original-driver control over 360 requested frames | Completed SIO2 transfers | Original VBLANK OVERLAP messages | Original DMA mode |
| --- | ---: | ---: | --- |
| Patch 0032, large execution chunks | 359 | 0 | 0x79 |
| Patch 0032, 32-EE-cycle execution chunks | 14, then stalled | 5 | 0x00 |
| Patches 0032–0033, 32-EE-cycle execution chunks | 360 | 0 | 0x79 |

After successful negotiation, the unchanged original drivers write Start pressed/released as `0xFFF7`/`0xFFFF` and Cross pressed/released as `0xBFFF`/`0xFFFF` into **both** original PADMAN DMA buffers. Cross pressure also changes from `0xFF` to `0x00`. The harness changes only the physical controller sample; it does not inject PADMAN results. These checks prove the original-driver transport, not native menu entry.

Both patches received independent review. All eight new scheduler regression groups fail before patch 0033; the corrected strict-loader suite passes 48/48. The SIO2 suite passes 22/22, including two authored MIPS FIFO/IRQ programs that fail against patch 0031. The combined standalone IOP suite passes all 15 programs. Fresh Linux host contracts pass 5/5 and the Python suite passes 118/118. The complete runtime compatibility suite passes 531/531 with `-O2 -g0`, strict floating-point flags and assertions enabled. Exact upstream and patch verification before and after the run confirms all 333 source files remain unchanged.

| Reviewed patch | SHA-256 |
| --- | --- |
| 0032 | 46698bdf0cc46e6c0beda259275c4417e9171e5cf526b12f8bc06fe3922c3933 |
| 0033 | e273a36cf6f34299683071a004775f7adf8119ac9f06d497c7c6ad81dbe0e8bb |

### Original no-card dialog: patch 0030

The ordered callback-DMA correction resolves the previous MCSERV transport stop. In the original-program run, the server's 192-byte GetInfo transfer completes before the normal RPC result and completion packet. The game then renders its original no-memory-card dialog, with No selected above Yes and the Cross Accept prompt. The process reaches its 480.126-second diagnostic deadline without a strict execution failure. This does not establish menu entry or a working save system.

| Observation | Recorded result |
| --- | --- |
| DMA / GIF / VIF work | 273,494 starts / 18,549 copies / 121,564 writes |
| IOP execution | 19,166,568 instructions; 11 module records; 30 threads; 15 RPC servers |
| GS surface | 640×448; VBlank 2,215; 221,088 nonblack pixels |
| Native executable SHA-256 | cae240659960d17800847204ba22f675f1e74cd3b8d648afc9a45ac59514b1a2 |
| Source PPM SHA-256 | d7db03c0f62dca8cccd213ffdcb02b9c8b54086e3ea1cea9988492085a141b3e |
| Lossless PNG SHA-256 | 45ffc421eb6f9866edb0d8d1ee0624e1e425a451336e8f165fdcc6e05640fd82 |

That historical capture was reviewed before the workspace reset but was not published; its local image is no longer retained. The fresh patch-0035 screenshot below comes from a new run, with its own executable, report, original menu state and lossless pixel provenance.

### Latest screenshot

![Original no-card dialog with Yes selected through the original controller drivers](screenshots/mafia-linux-no-card.png)

This separate patch-0035 run used the same verified `780f1950…5049fa` executable and the complete original installation. Cross dismissed the caution and Up selected Yes. After all keys were released, the host closed the window cleanly at 92.913 seconds, joined the EE worker and captured the actual GS surface. The exact-ELF snapshot identifies the active no-card class (`0x004D8E80`) and focus component 2, with both controller records in mode `0x79`, released buttons and zero grace/settling counters. The surface is 640×448, VBlank 664, with 221,360 nonblack pixels. It shows an original working frontend dialog, not the main menu.

| Current no-card capture provenance | SHA-256 |
| --- | --- |
| Raw source PPM | fd333e32dc14633e7713db95446c7964d267bc2b2eba47c67166990d02e0eb9b |
| PNG | 9c629be34f8ef00e90c52f6e8e8fb8caf8620751c89ccdaed36958c33239cdc3 |
| Decoded RGB bytes | 40187ea2cde72ecc6cd3a20a1ce05db18af41bca8db302fa759711be6e14f1bd |
| Actual native report | 30c90fe3b4215959f89eabcce2682b7fb9b01a3423ba5c08399c4a9cbc3edebe |

### Earlier autosave screenshot

![Original autosave caution screen produced by the translated game](screenshots/mafia-linux-autosave.png)

The 120-second patch-0028 run produced this original **640×448** screen with its
CONTINUE Cross prompt, background artwork and text. Two independent reviews
verified the image. PNG conversion preserves every RGB byte; no pixels were
added, removed or scaled.

| Frontend capture provenance | SHA-256 |
| --- | --- |
| Raw source PPM | fe1033ac946a2a5f8cb7a5cb3a2404320ceda20e75a3f8cbb58ba8e184c512ec |
| Published PNG | 37a3794c9a6247da57cfc5c78ba2a7edc38cc35ec279eda47157fe2aeda20a32 |
| Decoded RGB bytes | 3c04a90c81cc3236b4215c7691ee47e7691e4c8c0a188b551313019e1f3a2c6e |

### Earlier startup screenshot

![Gathering startup splash produced by the original translated program](screenshots/mafia-linux-gathering.png)

This is the original **640×448** startup splash, captured headlessly and encoded as PNG with exactly the same RGB pixels. It is not a menu or gameplay screenshot.

| Capture provenance | SHA-256 |
| --- | --- |
| Raw source PPM | 67ff60d1d8e3f3c114e4fcfdef6aa4b68b960321d1efd3b697d0e8b431d4c39d |
| Published PNG | af7aab0d84b7a2b12744f7847e097231f14ce3ca1d09ac6489ce4e80fcf44ea8 |
| Decoded RGB bytes | 7e2ab2785b7132569849a08f9f0c56fbe98cc6e8bcc6f5e12d291e87faac81b2 |

The captured image has 21,092 nonblack pixels and was checked visually and against its raw pixels by independent reviewers. A later frame after sound initialization begins has slightly different pixels from the splash animation, but still shows the same startup logo. A nonblack count alone is never treated as proof of a correct game frame. The host's automatic game_frame_verified and gameplay_verified fields remain false; the manually reviewed startup image is a separate milestone. Headless execution has no window presentations.

### Historical transport failure: patches 0001–0029

The longer Linux run passes the caution screen's automatic dismissal and
executes the original memory-card query. It stops at a precise unsupported
transport operation before presenting another screen. This is observed original
control flow, not a successful memory-card or menu claim.

| Observation | Recorded result |
| --- | --- |
| Process status | Strict transport failure after 360.825 wall seconds, before the requested 480-second deadline |
| Error | `Physical IOP packet RPC: callback SIF DMA requires an ordered packet transport` |
| Original call chain | `sceMcGetInfo` → original MCSERV RPC `0x80000400`, function 1 |
| EE sample at failure | `sceSifSetDma`, `0x004057E8`; branch from `_sceSifSendCmd`, `0x004079E4` |
| Hardware work | 208,266 DMA starts; 14,070 GIF copies; 91,698 VIF writes |
| IOP snapshot | 15,233,832 interpreted instructions; 11 loaded-module records; 30 threads; 15 RPC servers |
| Last displayed surface | The caution image remains at VBlank 1,735; no newer screen was presented |
| Native executable size | 219,166,672 bytes |
| Native executable SHA-256 | `4f69a627b2aabbd50439c099d09faa817c18f37b9103dfd4fb9637069f7e23b7` |

The original stack contains the `sceMcGetInfo`, `MC_CheckMemCards` and subsequent
`ExecuteMenu` continuation. Its server and request-buffer addresses match the
original resident MCSERV module. That handler issues a 192-byte IOP-to-EE DMA
transfer before its RPC completion; at that checkpoint the physical packet path rejected
such callback transfers. The separate modeled host memory-card service is not
on this original path, so its formatted-card defaults do not establish the
outcome of this scan.

This run uses optimized runtime/IOP/GS libraries (`-O2 -g0`, assertions retained)
and the same verified 245 game objects at `-O0`. A preceding patch-0028 optimized
240-second run still showed the caution screen with original `timeGetTime`
returning 20,618 ms. The unoptimized 360-second run reached 8,220 ms. Those are
original timer readings since its initialization, not time spent on the warning
or a frame-rate measurement. The latest frame's raw PPM SHA-256 is
`afd74791069e028d9f1a545255c6b9310d4a4ead7839218e5c36094882f497f4`;
the published screenshot above retains its separately identified patch-0028
capture provenance.

### First original frontend capture: patches 0001–0028

The actual native program reaches the original autosave caution screen and
continues rendering until the requested diagnostic deadline. It no longer
stops at the corrupted SIF return or at the CDVDMAN tray-change query.

| Observation | Recorded result |
| --- | --- |
| Process status | Clean diagnostic timeout at 120.630 wall seconds; no strict execution failure |
| Original frontend | Autosave caution screen, with CONTINUE Cross prompt |
| EE sample at shutdown | `RBuffProcessOneDmaCommand__9LSIDeviceFb`, `0x003E8F60` |
| Last branch source | `DmaSendToVif__9LSIDeviceFUi`, `0x003EF3D4` |
| EE scheduler | Main thread running; no semaphore waiters |
| Hardware work | 14,129 DMA starts; 528 GIF copies; 2,907 VIF writes |
| IOP snapshot | 10,116,180 interpreted instructions; 11 loaded-module records; 30 threads; 15 RPC servers |
| Latest GS capture | 640×448, VBlank 1,494, framebuffer page 140, 220,652 nonblack pixels |
| Native executable size | 224,400,648 bytes |
| Native executable SHA-256 | `1d85f2df307d0103e1a6a5d6c662cdd943e9d5b40801c245f73440918e1026c1` |

The original caution screen also has a 25,001-millisecond game-time dismissal
path. Its timer is driven by the original EE timer interrupt. VBlank 1,494 is
less than 25 seconds since startup, while the caution screen is created later;
the 120-second wall-time limit therefore does not establish a stuck screen.
The later patch-0029 run verifies the automatic transition and reveals the
memory-card transport failure described above. A sampled rendering function
and a single hardware snapshot are not treated as proof of a deadlock.

At this historical checkpoint the existing host pad override fed only the EE SDK replacement, which this profile disables. Patches 0032–0033 subsequently connect the original PADMAN/SIO2 route. The fresh window runs described above verify Up/Cross consumption by the full native frontend.

#### Corrections that enabled this screen

Patch 0024 reuses freed IOP allocations; four authored churn/fragmentation cases
fail before it and pass afterward. Patch 0025 retires delayed register loads
before modeled imports or original exports; its four new instruction cases
also reproduce the former failures. The missing original animation preload
was restored and hash-verified. Patch 0026 preserves the optical driver's
read-only backing and supplied-export precedence; review corrected an initial
implementation that shadowed foreign file handles.

Patch 0027 fixes the confirmed EE stack collision. Original `_start` owns
`[0x01F00000, 0x02000000)` as its main stack; the former callback pool used the
same interval. A legitimate FILEIO tail copy overwrote the SIF handler's saved
return address. Callbacks now use a bounded, separately owned kernel arena,
preserving the original alarm patch and coordinating reset and lease reuse.
All eight new regressions fail against the former runtime and pass with the
correction. The actual original program passes the corrupted-return location.

Patch 0028 implements the next observed request, `sceCdTrayReq` mode 2, from
original `G_LoadSaveClass::Init`. It consumes the driver's initial media-change
notification and preserves output bounds, controller state and original
export precedence. Six new behavior cases fail before the fix; the complete
eight-case suite passes afterward. Physical tray movement remains unsupported.
The previous 52-file and 54-file runs stopped at the identical missing request;
after the fix, the 54-file run reaches the screen above.

Patch 0029 corrects a separate idle-scheduler ordering error: the earliest
guest-cycle deadline must win before comparing host-clock deadlines. Two
authored periodic-timer regressions fail against the previous runtime; all 21
interrupt cases pass with the correction. The full runtime suite passes 521
cases in both `-O0` and `-O2` builds. Host wait pacing remains enabled. This fix
does not establish a general solution to the cost of software rendering.

Fresh runtime and host code were linked with 245 unchanged game objects. All
object digests, actual transitive upstream include dependencies and 7,794
generated source/header hashes were checked. Launches record the executable
hash before execution and verify that it remains unchanged afterward. Original
EE and IRX instructions remain unchanged; invalid-return and unsupported-service
checks remain active. Audible playback has not been verified.

IOP reset is **modeled**, including the kernel and allocator. The supplied replacement image's original LOADFILE, CDVDFSV and FILEIO complete their own entry and RPC registration before reset completion is reported. No PS2 ROM boot sequence or BIOS execution is claimed.

## Original input

The complete user-supplied archive was restored through UnRAR with successful CRC verification. Every ISO byte and extracted file was hashed. The current workspace retains the complete archive, ISO and all 281 extracted files; it no longer depends on the earlier partial installation.

| Observation | Verified result |
| --- | --- |
| Archive bytes / SHA-256 | 3,591,273,732 / 24b0e1c665c1b38528427eb2598e668d83300febb1bd80a1477acfa83b3af769 |
| ISO bytes / sectors | 4,128,538,624 / 2,015,888 sectors of 2,048 bytes |
| ISO SHA-256 | 2cabb2b619bb7d43da2ded3fcdaf496b6f237385e05a9922d3bcf1cc82d4a5c0 |
| Volume / inventory | MAFIA; 281 files, 16 directories |
| Build identity | SLUS-20671, SYSTEM.CNF version 1.01, NTSC |
| Boot ELF bytes / entry | 5,613,500 / 0x00100008 |
| ELF SHA-256 | adf4944c5cdac23fcc4e9af9ef3ec1ba6234497e57cec2af2ff1a7a15c62a46f |
| Retained original files | 281 files / 4,126,918,314 bytes; all 16 directories |
| Per-file disc manifest SHA-256 | 38e664451ec5484e8472eb4c422690cbec0b0a9909eee598ca52b2c76ea0937f |

Retained inputs include the original ELF, SYSTEM.CNF, all standalone IRX modules, IOPRP271.IMG, DTA archives, every FQL file, SOUND.DAT and all videos, including INTRO.PSS. The full ISO is available for raw-sector requests. Earlier runs used a 54-file subset; restoring missing original sound and animation files had already removed startup aborts without changing guest instructions. The fresh build now uses the complete installation. No BIOS was downloaded.

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

Function-table compaction preserved all 499,431 non-null slots, the 1,004,839-slot bounds, holes and owners. The expanded ordered mapping digest remains 0e4c8288fc46962060fca25e307c5a127e6009d66d81f8866bac58913cbb6b02. Current runtime-only updates rerun the actual pinned tools in a fresh staging directory, verify source and manifest identity, retain genuine generator metadata and let ordinary CMake determine which objects need rebuilding.

## Executed checks and reviews

| Suite / check | Current observed result |
| --- | --- |
| Fresh upstream runtime, patches 0001–0034 | 535 passed, 0 failed; `-O2 -g0`, assertions enabled; all four new VBlank-status groups fail before patch 0034 |
| Focused EE callback ownership regressions | 18/18; all eight new cases fail against the previous runtime |
| All rebuilt IOP CTest programs, patches 0001–0038 | 17 passed, 0 failed |
| Periodic EE timer and interrupt regressions | 21/21; both new idle-deadline cases fail before patch 0029 |
| CDVD tray-query cases, including original-provider precedence | 8/8; six behavior cases fail against the former implementation |
| Strict-loader cases, including exact module-name lookup | 56/56; seven new behavior groups fail before patch 0035 and its unsupported-contract control already passes |
| Original-driver Devctl dispatch cases | 14/14 after patch 0036; exact preceding implementation passes 5/14 |
| Stream1 callback registration cases | 8/8 after patch 0037; exact preceding implementation passes 1/8 |
| Selected-medium file-search cases | 14/14 after patch 0038; exact preceding implementation passes 1/14; separate original-ISO metadata and three payload samples verified |
| Physical SIO2 cases, including authored MIPS FIFO/IRQ integration | 22/22; both MIPS integration cases fail against patch 0031 |
| IOMAN file cases, including optical flags and supplied-export priority | 17/17; the original-export regression caught a review finding |
| Native host/runtime contracts, exact patches 0001–0038 | Five CTests passed |
| Python disc/ELF/profile/generation/report suite | 118 passed, 0 failed |
| Exact pinned source plus all reviewed patches | Passed; no extra source inputs |
| Prior emitted integer arithmetic UBSan suite | 68 cases passed on Linux and ARM64 macOS |
| Prior GS-cache suite | 45 passed; not rerun for unrelated IOP-only corrections |

Independent reviews covered transport framing and ordering, reset lifetime, strict loading, callback and buffer ownership, file and descriptor lifetime, VIF stream/interrupt behavior, native diagnostics, capture integrity and Linux/macOS launch paths. Findings were corrected with focused regressions. Host parser/capture sanitizer checks passed; the complete original game has not been run under ASan.

The previous six-patch baseline passed all four Linux and ARM64 macOS jobs in [Actions run 5](https://github.com/dnesdan/mafia-ps2-recomp/actions/runs/40526989706). That result does not establish macOS execution of the newer runtime changes or the original game.

Review found that the old workflow omitted the upstream runtime and IOP suites. The updated workflow explicitly builds and runs the main runtime executable and all seventeen IOP test programs on both platforms, using a separate pinned build directory. The table above identifies which patch sets were actually tested locally. The enum-coverage test requires the upstream source working directory; CI now launches the suite there. All four jobs in [the checkpoint Actions run](https://github.com/dnesdan/mafia-ps2-recomp/actions/runs/40589657722) passed for patches 0001–0026; [the patch-0027 run](https://github.com/dnesdan/mafia-ps2-recomp/actions/runs/40591779769) also passed all four jobs. This includes the full upstream runtime and thirteen IOP programs, host contracts, Python tests and emitted integer UBSan checks on Linux x86-64 and ARM64 macOS. The updated fourteen-program [patch-0028 run](https://github.com/dnesdan/mafia-ps2-recomp/actions/runs/40592971268) also passed all four jobs. Original game execution on macOS remains unverified.

| Platform | Evidence and remaining limit |
| --- | --- |
| Linux x86-64 | Complete game corpus freshly compiled; original entry, main, IOP services and frontend graphics execute. Actual window runs consume Up/Cross and enter movie initialization. |
| Apple Silicon macOS | Source, host, upstream runtime, IOP and emitted-integer CI passed through patch 0031; complete original game execution remains unverified. |
| Linux ARM64 | Build scripts accept the platform; original game execution has not been tested. |
| Desktop window and controls | Original game verified in Xvfb with software OpenGL and real keyboard events. Up/Cross are consumed through original PADMAN; physical gamepad hardware and hardware GPU behavior remain untested. |

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

All four Linux and ARM64 macOS jobs also passed in the [patch-0031 Actions run](https://github.com/dnesdan/mafia-ps2-recomp/actions/runs/40600119977). Local verification of patches 0032–0038 is recorded separately above; those changes have not yet received a new remote CI result.

The immediate objective is to complete the original movie, title and profile flow and reach the main menu. Ordered complete-quadword secondary DMA, VBlank callbacks, hardware status and physical controller transport have regression and original-program evidence. Native Up/Cross consumption is verified. The next strict boundary is the partial-quadword SIF transfer returning the original file-search record. Subsequent streaming completion, actual video reads and kernel alarms still need original-program verification. After menu execution, verification must cover media, saves and gameplay, then deterministic comparisons against PCSX2. No FPS, mission completion or visual equivalence is inferred from compilation or unit tests.
