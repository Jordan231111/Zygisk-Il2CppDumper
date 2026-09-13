# Runtime architecture and compatibility

## Responsibilities

`main.cpp` owns the Zygisk boundary: exact target selection, JNI string lifetimes, the app-provided data directory, root-only configuration reads, and optional ARM companion bytes captured before specialization. It unloads from unselected apps and system-server processes. It does not retain a `JNIEnv*` for worker use.

`hack.cpp` owns worker creation, the bounded discovery window, platform diagnostics, and the isolated NativeBridge path. Workers are created with checked pthread APIs and detached attributes. JNI native-bridge requests use a small checked wire structure; C++ objects are not shared across native runtimes. The bridge checks advertised callback versions, supports the v7 trampoline API, and uses the bridge's default namespace instead of inventing namespace addresses.

`discovery.cpp` first finds the already loaded `libil2cpp.so`. Periodic fallbacks inspect other linker modules and ELF headers at readable mapping starts. Renamed/mapped candidates need the expected architecture, valid bounded ELF tables, and multiple executable IL2CPP API anchors. It does not force-load an absent engine or scan arbitrary executable signatures across the process.

`il2cpp_api.cpp` binds a small capability table. It tries normal `RTLD_NOLOAD`/`dlsym`, then a bounded in-memory ELF symbol index. Where ordinary ELF files are accessible, missing required exports or enumeration/calibration capability groups can be recovered from `.symtab`; each recovered address must match resident code bytes. APK-embedded file-only symbols and `.gnu_debugdata` are not currently parsed by this fallback. Normal loaded-library handles are held until the dump completes. Namespace fallback handles cannot pin an independently unloading custom runtime.

`core/elf.*` distinguishes file offsets, ELF virtual addresses, and process load bias. It supports ELF32/ELF64, SYSV/GNU symbol counts, sectionless dynamic lookup, and relative/already-relocated dynamic pointers. Count, extent, alignment, string termination, multiplication/addition and iteration limits precede reads. Symbol indexing is performed once rather than following potentially cyclic hash chains for every API. IFUNC symbols are only supported through the normal linker resolver.

`core/maps.*` parses complete map lines, including spaces, APK paths, anonymous mappings and deleted suffixes. Range lookup uses sorted mappings. ARM64 data tags and ARM Thumb function bits are handled according to their role. Reads use `process_vm_readv` with a lazily opened `/proc/self/mem` fallback and 64-bit file offsets on every ABI, including bounded scatter/gather batches of method pointers after enumeration, rather than installing process-wide signal handlers. A cached non-readable allocator reservation is refreshed when it later becomes committed memory.

`il2cpp_dump.cpp` owns capability-checked initialization, IL2CPP thread attachment/detachment, assembly snapshots, class/member enumeration and formatting. The preferred path uses image APIs. Legacy reflection invokes managed methods through `il2cpp_runtime_invoke`, checks exceptions, selects `Assembly.Load(string)`, and enumerates returned types through managed enumeration. It does not call generated method bodies with guessed native signatures or index a made-up managed array struct.

`core/output.*` writes incrementally to a private, exclusive temporary file in the destination directory. It checks write/flush/close errors and atomically renames a completed result. Failed work does not truncate an existing successful dump. It refuses a symlinked output directory and cleans up its own temporary file on normal failure.

## Initialization and private layouts

The old `il2cpp_is_vm_thread(nullptr)` loop was not a valid readiness contract. In one supplied target it dereferenced runtime state before that state existed. Simply waiting for corlib was also insufficient: modern Unity can expose corlib before domain/GC initialization finishes, and some releases lazily create their domain.

The ARM64 adapter in `core/runtime_layout.*` recognizes bounded, validated accessor/predicate instruction shapes. It reads corlib/domain globals and, where available, the pointer chain used by the VM-thread predicate. Branch counts, instruction counts, arithmetic and actual mapped pointers are checked. It waits for consecutive ready samples before calling the runtime. It never invokes an arbitrary address recovered by a broad pattern search. Unsupported getter shapes produce an explicit timeout/unsupported diagnostic. Other ABIs currently retain a corlib-plus-settling heuristic and need further live Unity validation.

Runtime structures are opaque. Type names go through `Class::FromType` and class-name APIs. This preserves the known-good access path for compact/runtime-derived types that caused direct type-name formatting to fail in the supplied v39 runtimes. Byref/parameter attributes use APIs, with managed reflection available for legacy byref inspection. Generic arguments and complete nested-type qualification are not reconstructed; the output remains descriptive dump text, not a compilable C# project.

Method addresses are the remaining private-layout dependency. The adapter obtains two known corlib delegates through APIs, checks pointer-field types, and finds the common method-pointer offset within a bounded prefix. It checks the resulting pointers against executable mappings and only computes RVAs within the selected ELF's load segments. If calibration is unavailable, signatures can still be dumped while addresses are explicitly marked unavailable. No Unity version offsets or application addresses are compiled into the project.

Metadata and code registrations remain owned by IL2CPP. The dumper does not need to decode `global-metadata.dat` tables or locate registration structs to enumerate a functioning runtime. Named metadata mappings are inspected for the common magic/version prefix for diagnostics only. A recognized version number is not a claim of full file-format support.

## Adding a variation

1. Reproduce on an authorized target and record package/process, ABI, Android/page size, Unity version, metadata prefix and load path privately.
2. Identify the failing stage. Distinguish missing/renamed libraries, absent exports, initialization, layout calibration and output errors.
3. Prefer an existing public runtime accessor. Keep binary layout/instruction assumptions in the core adapter instead of spreading offsets through formatting code.
4. Add a synthetic positive fixture and negative/truncated/overflow cases. Test the real integration path and a prior successful app.
5. Add a bounded fallback only when there is a defensible identity check. If no safe strategy is available, keep the failure explicit.
6. Update the runtime matrix and limitations. Never check in proprietary binaries or present a synthetic fixture as a Unity-version test.

The implementation stays in C/C++ because it shares the Zygisk/Android ABI and does not justify a second Rust compiler/FFI pipeline. Small bounded components, explicit ownership and sanitizers address the observed parser problems without a complete rewrite.

The assembly list is copied through the kernel and checked for stable address, count and contents, with three bounded attempts. Image/name pairs are cached once per dump. Name-cache string payload is capped at 8 MiB and type-cache string payload at 32 MiB, in addition to entry and output limits. These checks reduce known races and resource amplification; they do not lock the runtime against teardown.
