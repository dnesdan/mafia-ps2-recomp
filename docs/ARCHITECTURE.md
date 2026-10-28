# Architecture

## Boundaries

| Component | Responsibility |
| --- | --- |
| `tools/mafia/iso.py` | Bounded ISO9660 directory/file/sector access; original path preservation; safe extraction. |
| `tools/mafia/elf.py` | Checked ELF32 MIPS metadata, symbol extents and an explicitly limited instruction survey. |
| `profiles/` | Exact disc/ELF identity and reviewed build-specific code boundaries. No generic runtime hacks. |
| `tools/mafia/symbolmap.py` | Function map from sized ELF symbols plus reviewed untyped boot ranges. |
| `tools/recompile.py` | Real upstream analyzer/recompiler invocation, source audit and generation provenance. |
| `patches/` | Reviewed generic upstream corrections and runtime extensions with regression coverage. |
| `src/` | Hash-checked ELF loading, native process/window lifecycle, scheduler execution and diagnostic reports. |
| PS2Recomp | EE AOT code generation, guest memory, kernel services, IOP, DMA, VIF/VU/GIF/GS and audio/media runtime. |

The EE executable is statically translated; the host does not interpret or JIT the EE game program. PS2 hardware behavior remains necessary. In particular, the upstream IOP subsystem interprets original IRX modules and implements service fallbacks. This project does not deliver PCSX2, a PS2 BIOS or a rewritten version of Mafia's game logic.

VCS Recomp uses PSP Allegrex/GE, not PS2 R5900/GS. Its separation of framework/profile/host and native packaging informed this design; its GPU code cannot supply Mafia's PS2 graphics pipeline.

## Disc and function identity

Boot discovery starts at `SYSTEM.CNF/BOOT2`. Serial and region are observations, not assumptions. A full ISO hash and separate ELF hash identify the build. Profiles are selected by ELF bytes and additionally validate size and entry point.

SLUS-20671 retains 7,788 sized function symbols but no DWARF sections. `_start`, `_exit` and `_root` are untyped symbols. Their short ranges were checked against instructions and recorded in the exact profile. All other boundaries come from sized function symbols. Metrowerks template names containing commas receive safe address-qualified CSV labels; the local symbol report preserves their original spelling.

The game combines executable text and data in a WAX section. Sweeping that section as instructions produced false functions and thousands of unsupported operations. A symbol-derived map prevents that failure without patching the original instructions. Function discovery is still not proof that every indirect target or dynamic overlay is covered.

## Reproducible generation

`dependencies.lock.json` pins upstream and FetchContent sources to commits. Bootstrap creates isolated dependency checkouts and applies the repository patches. `verify_upstream.py` constructs the expected patched Git index without changing the real index, compares tracked source bytes and rejects extra source inputs. Existing unrelated changes are never reset.

Analyzer output is saved as evidence, including its proposed HLE bindings, jump tables and instruction patches. Those proposals are not automatically adopted. The strict configuration disables inferred instruction patches, skipped functions and name/relocation-driven host substitutions. Genuine SYSCALL instructions continue through the runtime service dispatcher.

Generation writes a per-file SHA-256 manifest. CMake checks the exact source/header set, every digest, the manifest digest, patch identities and upstream tree before building the app. The host independently checks the original ELF's SHA-256 and header before loading a private copy of exactly those bytes.

The source audit distinguishes explicit traps from silent zero-return/no-op placeholders. Diagnostic builds may retain explicit failures but cannot silently substitute SDK/game functions. A zero-error translation remains weaker evidence than execution or differential testing.

Built analyzer/recompiler binaries also have a stamp covering their hashes, the exact patch set and CMake configuration. Generation rejects stale or independently modified tools. Single-config and Xcode outputs are resolved from the actual CMake cache, not from whichever old executable happens to exist.

### Generic upstream patches

| Patch | Reason and behavior |
| --- | --- |
| `0001` | Unknown syscall/TODO services throw under `MissingFunctionPolicy::Stop`. R5900 ADD/SUB/DADD/DSUB/DADDI/DADDIU use defined unsigned arithmetic before overflow decisions. Two upstream fixtures bind the scheduler context they invoke; runtime assertions remain enabled. |
| `0002` | `auto_bind_runtime_calls=false` disables implicit known-name stubs and relocation-based host calls. The default remains compatible with upstream. Explicit stub selection still works; actual SYSCALL routing remains intact. |
| `0003` | Adjacent table slots with the same function are emitted as `std::fill_n`. Holes, precedence and owner transitions remain identical. This reduces the measured registration source from 52,745,043 to 3,983,745 bytes; every original slot was compared. |
| `0004` | Strict return dispatch accepts the scheduler's zero return-address sentinel only for its active callback context. Invalid transfers are checked before a scheduler yield; unmapped invocation PCs remain errors. The original game's first syscall callback exposed this generic runtime inconsistency. |
| `0005` | `SetupThread` supplies the default executable argument through the original 324-byte startup ABI. The guest path comes from configured CD/host roots; bounds, aliases, version suffixes and payload limits are checked before mutation. The game's original `argc == 1` assertion remains intact. |
| `0006` | Strict mode rejects the negotiated guest SIF command path with packet and missing-transport diagnostics before any descriptor in the batch commits. Full physical RAM/scratchpad bounds prevent descriptor or payload wrapping; valid aliases and ordinary DMA remain supported. This exposes the missing IOP packet receiver, bootstrap RPC services and guest interrupt replies; it does not implement them. |
| `0007`–`0009` | Add a per-runtime SIF command endpoint, bounded batch snapshots, negotiated RPC initialization, original EE DMAC5 reply delivery and strict physical IOP loading. Implement SIF receive-DMA stop and modeled reset from the supplied IOP image. Original LOADFILE entry and RPC registration run before reset completion is advertised. |
| `0010`–`0014` | Start the supplied CDVDFSV service with owned workspace; expose live CD/DVD controller state; load mounted modules with their original argument ABI; model IOP DMA priority/channel enables and the PS2 RTC representation. |
| `0015`–`0017` | Start original FILEIO, return the real shared CD/DVD event, implement owned IOP mailboxes and interrupt callback contexts, and provide bounded read-only host file handles through the IOP imports used by the original service. |
| `0018` | Preserve deferred VIF command bytes across partial payloads and interrupt stalls. Deliver VIF I-bit interrupts from the EE executor, respect INTC acknowledgement/masks, and distinguish stream resume, reset and DMA cancellation. |
| `0019` | Preserve the PS2 optical-drive convention that permits a read/write open request while keeping the mounted disc handle read-only. No host write capability is granted. |
| `0020` | Own host image-file handles immediately and close them exactly once on read, size, allocation or parsing failures. Cleanup preserves the original exception even if the host close callback throws. |
| `0021` | Initialize and own the reserved IOP invocation stack once at reset, including its O32 argument home area. Physical RPC results retain the actual stack bytes; null pointers, unsupported aliases and unowned ranges remain errors. |
| `0022` | Accept the optical driver's mode-zero read convention for explicit CD-ROM paths. Registered guest drivers retain priority; host and memory-card mode zero, writes and unmodeled option bits remain unsupported. |
| `0023` | Include bounded IOP register, branch, pending-load and call-depth values in strict execution failures. Diagnostics read only the failed CPU state and do not access device memory or change execution. |
| `0024` | Reuse freed IOP heap ranges without recycling lifetime identities; account separately for maximum contiguous and total free space. |
| `0025` | Retire pending IOP loads before an import thunk enters a modeled service or original guest export. Arguments, results and return addresses retain their original instruction ordering. |
| `0026` | Honor conventional optical open flags with existing read-only backing. Reject writes to owned read-only handles while preserving supplied guest exports for foreign handles. |
| `0027` | Own EE callback stacks outside application stacks and heaps; preserve the original kernel-patch workspace, check aliases and declared ranges, and coordinate stack leases with scheduler reset and owner lifetime. |
| `0028` | Implement the CDVDMAN mode-2 tray-change query with a consumed notification, validated output and original guest-export precedence. Physical tray motion remains unresolved. |
| `0029` | Select idle EE events by their guest-cycle deadlines before host-clock tie breaking, preventing an overdue host event from skipping earlier periodic timer callbacks. Preserve host wait pacing. |
| `0030` | Capture owned secondary IOP-to-EE DMA from original RPC callbacks and deliver it before the ordinary result and completion packet. Validate all destinations before writing, preserve SIF0/DMAC gates, and discard queued replies on reset. Unsupported attributes and partial quadwords remain errors. |
| `0031` | Deliver original IOP VBlank callbacks with phase cadence, priority, GP and interrupt-context semantics. Preserve live CPU/mask gates, coalesce masked edges, and give supplied IRQ handlers and compatible original exports precedence. |
| `0032` | Model bounded SIO2 PIO transactions, two physical DualShock2 ports and original IRQ17 completion. Synchronized host snapshots feed the serial device while original PADMAN executes. Preserve negotiation, press/release and missing-device status by descriptor index. Memory-card probes return no-card; DMA11/12 and multitap remain unsupported. |
| `0033` | Preserve a running IOP thread across host cycle budgets, order ready peers by FIFO and preempt for higher-priority work through the live interrupt gate. Retain explicit guest yield/rotation, mailbox wakeups and callback-budget bounds. A short host slice no longer starves original PADMAN publication. |
| `0034` | Latch INTC_STAT bits 2 and 3 at the existing VBlank start/end events. Original EE clear-and-poll loops can observe each hardware edge independently of interrupt masks. Preserve selective W1C, unrelated status bits and the existing single callback route. |
| `0035` | Model MODLOAD module-name queries using exact names and the greatest matching loaded ID, with `KE_UNKNOWN_MODULE` for absence. Preserve bounded IOP string reads, actual registry lifetime and compatible original-export precedence. Filename stems do not alias declared module names. |
| `0036` | Forward IOMAN Devctl to the original registered device's callback with all seven O32 arguments, saved driver GP and an owned bounded callback stack. Preserve exact device/unit parsing, opaque payloads, original-export precedence and temporary-record lifetime across nested calls and failures. |
| `0037` | Retain the original stream1 callback pointer and registration GP independently of ordinary CD/DVD callbacks. Match setter, replacement, null-clear, ordinary-init and full-reset behavior without invoking the callback. Keep unsupported stream reads strict before I/O or completion effects. |
| `0038` | Resolve layer-zero file searches from the same mounted ISO metadata used for sector reads, including the original 36-byte layer-search record, date and flags. Preserve bounded 32-byte legacy output, staged owned RAM, original-export precedence and strict unsupported layers. A configured image remains authoritative when lookup, readiness or reading fails. |

The patches apply in order to the locked revision. Regression tests use constructed input. `patches/tests/check_integer_codegen.py` compiles code emitted by the real translator and executes signed-boundary cases under UBSan. The upstream `ps2x_tests` executable must be run directly from the upstream source directory; its separate GS-cache CTest directory does not run that executable.

The startup argument layout and syscall handoff were checked against the primary PS2SDK [startup header](https://ps2dev.github.io/ps2sdk/startup_8h_source.html) and [CRT source](https://ps2dev.github.io/ps2sdk/ee_2startup_2src_2crt0_8c_source.html), as well as the original executable's startup instructions. This supplies the default program name; a general multi-argument `ExecPS2` implementation is outside this correction.

The SIF boundary recognizes the published command route and `ERT | INT_O` framing defined by the PS2SDK [DMA header](https://ps2dev.github.io/ps2sdk/sifdma_8h_source.html) and [EE command implementation](https://ps2dev.github.io/ps2sdk/ee_2kernel_2src_2sifcmd_8c_source.html). It is scoped to that negotiated path. Patches after `0006` replace its diagnostic stop with supported transport: readiness follows endpoint/service initialization, and the original guest handler consumes replies and updates its own software registers.

IOP reset is explicitly modeled. The runtime parses the supplied replacement image, resets its owned kernel/allocator and starts the original LOADFILE, CDVDFSV and FILEIO service modules. Their entry functions must return resident and register actual RPC servers within a bounded interval. This is not execution of a PS2 ROM boot sequence; neither a guessed firmware entry nor a host stub is reported as an executed original service. Strict loads retain original import and module-start failures as errors.

The CDVDMAN tray check follows the original driver and the primary
[PS2SDK implementation](https://github.com/ps2dev/ps2sdk/blob/329e2bb43287e16af16c4dde1c614e2662c24468/iop/cdvd/cdvdman/src/cdvdman.c).
Mode 2 consumes observer 1's change notification: the initial query writes one,
later unchanged queries write zero, and a completed query returns one regardless
of disc presence. Reset restores the notification; ordinary initialization,
file reads and searches do not consume or rearm it. Output validation occurs
before state changes. The modeled fixed-media device does not perform physical
tray opening or closing; those modes retain the original export or strict
unsupported path.

## Runtime and platform

The native host uses the actual upstream 32 MiB EE memory and scheduler. It sets missing-function handling to `Stop`. Reviewed patches make unknown syscalls and generic unsupported services fail under that policy. Scheduler initialization and execution stay on the same worker. Guest context/metrics are read after joining; the live timeout path uses only an atomic sampled PC.

### Callback stack ownership

The original startup declares its main stack over `[0x01F00000, 0x02000000)`.
Using that same interval for runtime callbacks let a legitimate FILEIO tail copy
overwrite the SIF interrupt handler's saved return address. Callback stacks now
belong to the modeled kernel arena `[0x00084000, 0x00100000)`. Application heap
space and the original main-stack region remain available to the program.

The lower boundary preserves the supplied program's original kernel patches:
their fixed copies occupy ranges starting at `0x74000`, `0x75000`, `0x76000`
and `0x82000`; the highest copied byte is below `0x82028`. The alarm trampoline
uses a descending stack from `0x81FC0`. This startup map was checked against
the original ELF, including the copied routines' BSS and fixed globals.
The primary [Open PS2 Loader changelog](https://github.com/ps2homebrew/Open-PS2-Loader/wiki/Home/efaa9c0c1cda2894e9b5bbbb3de78778cd4d1139)
also identifies `0x84000` as the free-memory boundary after the Sony alarm
patch. This is a deliberate reservation in this kernel model, not a claim
that arbitrary guest kernels cannot use that memory.

ELF segments, declared thread stacks and configured heaps must not overlap the
arena. Checks account for the runtime's supported RAM aliases. The bounded
pool fails explicitly on exhaustion. Scheduler reset clears cached leases and
rewinds their allocator together; a completed or deleted owner's inactive
lease can be reused without reclaiming a suspended callback's live stack.
These changes affect runtime implementation files, preserving the public
header layout used by the already compiled game corpus.

### Presentation and platform

The main thread owns the window and texture presentation. Pixel data comes from the upstream GS framebuffer; absent output remains absent. The initial backend uses the CPU GS and raylib. FFmpeg stays enabled for original media decoding. A Metal/MoltenVK accelerated GS is future work and needs comparison against the verified CPU path first.

Guest saves have a separate root; macOS defaults to `~/Library/Application Support/MafiaNative/memory-cards`. Source data paths are set after `loadELF`, because upstream loading resets its I/O defaults. An extracted directory supports file access, while raw sector operations require the original ISO. Partial extraction is reported explicitly.

macOS builds request `arm64`, reject Rosetta and verify the produced Mach-O architecture. ARM translation uses a pinned sse2neon revision. Generated code/runtime disable fast-math and FMA contraction. This is a portability configuration, not a claim of complete R5900/VU floating-point equivalence.

Linux has dedicated build/run scripts and uses the same host, generated corpus and runtime. The actual game has been exercised on x86-64, both headlessly and in an Xvfb window with software OpenGL. Real keyboard events travel through the window backend, physical SIO2 model and original PADMAN to the original EE frontend; fresh runs verify Up/Cross consumption. Linux ARM64, physical gamepads and hardware GPU behavior remain unverified with the game.

## Evidence

The host reports input verification, ELF loading and actual original-entry invocation separately. Window presentations and hardware counters are diagnostic values, not proof of a correct game frame. Failures retain the stage, guest PC, last branch source and error; `symbolize_report.py` resolves addresses against the exact input ELF.

The host can capture a GS presentation surface after the EE worker has joined. Capture records its dimensions, VBlank, source/display framebuffer, pixel hash and nonblack count, and writes a new lossless PPM without overwriting existing files. Synthetic fixture captures remain test evidence only. Screenshots under `docs/screenshots/` require an actual original-program run, exact RGB identity with its raw capture and visual review; the retained images show the Gathering startup splash, autosave caution and no-card dialog with Yes selected. Headless captures can therefore exist while the window-presentation counter remains zero.

Failure reports also include bounded snapshots of EE threads, semaphores, registers/stack and IOP modules/services after execution has stopped. Verified ELF symbols drive address lookup; diagnostics do not dereference arbitrary host pointers or trigger reads through device MMIO handlers.

For the exact supported ELF, the host also records a bounded, read-only frontend snapshot after joining the worker. It follows at most eight original menu-parent records, validates RAM aliases and spans, detects cycles and reports original controller samples and readiness counters. A menu class identified beneath an active child remains an ancestor, not proof of an unobstructed main menu. Synthetic contract inputs receive no frontend snapshot. The host clears an old presentation texture when the GS no longer supplies a valid current surface, so movie setup cannot leave a stale dialog displayed as live progress.

Tests use constructed input bytes. They cover malformed discs/ELFs, source provenance, real runtime memory/scheduling, missing dispatch and upstream instruction regressions. PCSX2 differential tests, visual/audio comparisons, mission completion and Apple Silicon performance measurements require the original game running in both environments; none is inferred from unit-test totals.

`MafiaHostContract` compiles the same native application entry code with a small authored ELF and test function table. It checks input rejection, validation versus execution, strict-stop reports and the ARM64 `.app` bundle. It is a test executable, is not installed, and contains no game corpus.
