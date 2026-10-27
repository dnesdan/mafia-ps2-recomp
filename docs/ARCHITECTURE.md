# Architecture

## Boundaries

| Component | Responsibility |
| --- | --- |
| `tools/mafia/iso.py` | Bounded ISO9660 directory/file/sector access; original path preservation; safe extraction. |
| `tools/mafia/elf.py` | Checked ELF32 MIPS metadata, symbol extents and an explicitly limited instruction survey. |
| `profiles/` | Exact disc/ELF identity and reviewed build-specific code boundaries. No generic runtime hacks. |
| `tools/mafia/symbolmap.py` | Function map from sized ELF symbols plus reviewed untyped boot ranges. |
| `tools/recompile.py` | Real upstream analyzer/recompiler invocation, source audit and generation provenance. |
| `patches/` | Small generic upstream fixes with regression coverage. |
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

The patches apply in order to the locked revision. Regression tests use constructed input. `patches/tests/check_integer_codegen.py` compiles code emitted by the real translator and executes signed-boundary cases under UBSan. The upstream `ps2x_tests` executable must be run directly; its separate GS-cache CTest directory does not run that executable.

## Runtime and platform

The native host uses the actual upstream 32 MiB EE memory and scheduler. It sets missing-function handling to `Stop`. Reviewed patches make unknown syscalls and generic unsupported services fail under that policy. Scheduler initialization and execution stay on the same worker. Guest context/metrics are read after joining; the live timeout path uses only an atomic sampled PC.

The main thread owns the window and texture presentation. Pixel data comes from the upstream GS framebuffer; absent output remains absent. The initial backend uses the CPU GS and raylib. FFmpeg stays enabled for original media decoding. A Metal/MoltenVK accelerated GS is future work and needs comparison against the verified CPU path first.

Guest saves have a separate root; macOS defaults to `~/Library/Application Support/MafiaNative/memory-cards`. Source data paths are set after `loadELF`, because upstream loading resets its I/O defaults. An extracted directory supports file access, while raw sector operations require the original ISO. Partial extraction is reported explicitly.

macOS builds request `arm64`, reject Rosetta and verify the produced Mach-O architecture. ARM translation uses a pinned sse2neon revision. Generated code/runtime disable fast-math and FMA contraction. This is a portability configuration, not a claim of complete R5900/VU floating-point equivalence.

## Evidence

The host reports input verification, ELF loading and actual original-entry invocation separately. Window presentations and hardware counters are diagnostic values, not proof of a correct game frame. Failures retain the stage, guest PC and error; `symbolize_report.py` resolves that PC against the exact input ELF.

Tests use constructed input bytes. They cover malformed discs/ELFs, source provenance, real runtime memory/scheduling, missing dispatch and upstream instruction regressions. PCSX2 differential tests, visual/audio comparisons, mission completion and Apple Silicon performance measurements require the original game running in both environments; none is inferred from unit-test totals.
