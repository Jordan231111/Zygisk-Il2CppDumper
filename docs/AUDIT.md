# Initial audit — 2026-09-11

Baseline: `aa83f73bdf95e0674541a8ca1824e23eb8f006a7`, full `master` history (49 commits), no submodules. Work starts on `modernization/android-il2cpp`; a separate, detached baseline checkout is retained locally. Proprietary test inputs and raw device evidence stay outside the repository.

## Architecture and baseline

One Android library module packages four native shared objects into a Magisk ZIP. There is no Java/Kotlin application, JNI UI, Maven runtime dependency, metadata-file parser, code/metadata-registration scanner, or pattern scanner. The languages are C++20, vendored C, Groovy Gradle, and shell. AndroidX/Jetifier/Prefab settings are unused.

The complete original dump flow is:

1. Zygisk API 2 calls `main.cpp` before specialization. The exact process name is compared with a compile-time package name; the data directory is copied. x86 builds map an ARM companion from the module directory.
2. After specialization a detached worker in `hack.cpp` starts. On x86 it waits five seconds, queries hidden Java framework APIs and attempts NativeBridge loading, including a hard-coded namespace pointer.
3. `xdl_open("libil2cpp.so")` polls ten times. Vendored xDL 1.2.1 enumerates linker modules, parses ELF dynamic tables and resolves exports.
4. `il2cpp_dump.cpp` resolves approximately 250 API names, validates only one, then repeatedly calls `il2cpp_is_vm_thread(nullptr)` without a deadline. It attaches the worker to the runtime and never detaches it.
5. Assemblies/images/classes are enumerated through runtime APIs. Older runtimes use direct calls to managed reflection method pointers with assumed native signatures.
6. Types, members, offsets and method addresses are formatted into a vector of strings. The entire dump is retained until written to `<app_data_dir>/files/dump.cs`. Open/write errors are not checked; success is logged unconditionally.

Original tooling: AGP 8.2.0, Gradle 8.6, JDK 17 in CI, SDK 34, min SDK 23, NDK 25.2.9519653, CMake 3.22.1 (minimum 3.18.1), C++20. The wrapper lacks an executable bit and distribution checksum. `sh gradlew :module:assembleRelease` succeeds locally on JDK 21 in 42 seconds, with obsolete SDK-schema warnings. All four native ABIs build. CI only supports manual release builds; checkout v6/setup-java v5/upload-artifact v7 are already recent major versions, but not commit-pinned. User input is interpolated into shell/sed. Packaging relies on AGP private intermediate paths and deprecated variant APIs.

## Findings and planned boundaries

* Pointers and counters from failed API resolution are called or dereferenced unchecked. Initialization timeout and error propagation are missing.
* `Il2CppType` bitfields, `MethodInfo` prefix, and a fake fixed 32-element managed array are assumed across versions. Reflection method calls depend on generated-call signatures. These must be isolated or replaced by public APIs.
* Zygisk JNI arguments may be null. JNI exceptions/references, thread attachment, module-directory descriptors, mmap errors, file sizes, memfd writes and native-bridge callbacks need explicit lifetime/error handling.
* System-server processes unnecessarily retain the module. Exact process matching is safe by default but secondary processes need explicit opt-in, without cross-package prefix matching.
* xDL 1.2.1 contains hard-coded 4 KB alignment, unchecked dynamic/hash walks, addition-overflow bounds checks, unchecked section-name/string indices, and obsolete platform LZMA internals. Upstream 2.4.0 is available; audit the actual changes and bound any added parser/discovery path.
* A ten-second library-load window misses delayed engines. Namespace isolation, split-APK mappings, nonzero mapping offsets, and relocated/stripped ELF headers require evidence-based fallbacks.
* Output retains memory proportional to the entire dump, can truncate an existing good result on failure, and can silently fail when `files` does not exist. Stream through a checked temporary file and publish on success.
* No tests, fixtures, sanitizers, lint, static analysis, or runtime compatibility matrix exist. Add synthetic fixtures; never check in application binaries.

Use incremental C++ refactoring. Rust was considered for binary parsing but would add a second compiler/FFI/ABI build pipeline to a small native module without removing the need to call the live IL2CPP runtime. A bounded, independently tested C++ parser and API-first runtime access provide a lower-risk migration. No complete rewrite is planned.

Runtime compatibility must be reported separately from successful compilation. The available authorized target is Android 13/API 33, ARM64, 4 KB pages, Kitsune Magisk 31.0 with ReZygisk. Actual packages: Rust Axion `com.prayermicelle.rustaxion`, Evil Hunter Tycoon `com.superplanet.evilhunter`, OnceWorld `work.ponix.onceworld`. Existing instrumentation modules are present and will be recorded as a possible test confounder.
