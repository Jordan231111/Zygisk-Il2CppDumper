# Engineering report — Zygisk-Il2CppDumper modernization

Audit date: September 11, 2026. Original revision: `aa83f73bdf95e0674541a8ca1824e23eb8f006a7`. Work branch: `modernization/android-il2cpp`. The clone includes the complete original `master` history, not a shallow export. Changes are separated into audit, toolchain, dependency, parser/test, runtime, CI and documentation commits.

This report distinguishes real Unity/Zygisk tests, synthetic runtime tests and compile-only coverage. Raw application inputs, dumps and device logs remain outside the repository.

## 1. Initial state

The project contained one Android library module, C++20 dumper code, C xDL sources, a Zygisk API 2 header, Groovy Gradle scripts and a shell Magisk installer. There was no Java/Kotlin app, external Maven runtime library, git submodule, test suite, metadata-file parser or registration scanner.

The original path was: exact package-name match in `preAppSpecialize`; detached worker after specialization; ten one-second attempts to locate `libil2cpp.so`; bulk export lookup through xDL; an unbounded `il2cpp_is_vm_thread(nullptr)` loop; thread attachment; image/class/member enumeration; retention of the entire output in strings; unchecked writing of `files/dump.cs`. Older Unity used direct calls to reflection method pointers with assumed native signatures.

The original release build succeeded for all four ABIs using `sh gradlew` in 42 seconds. The wrapper lacked its executable bit, so the documented `./gradlew` invocation initially failed. AGP emitted obsolete SDK-schema warnings. The initial configuration was Gradle 8.6, AGP 8.2.0, CI JDK 17, SDK 34, NDK 25.2.9519653 and CMake 3.22.1.

Major findings:

- Missing APIs were logged but subsequently called; initialization had no meaningful failure propagation or deadline.
- The null-thread readiness probe crashed Evil Hunter Tycoon before dumping. Runtime state was not yet initialized.
- Private `Il2CppType` bitfields and a fixed 32-element managed array were assumed. Method addresses assumed a fixed prefix without validation.
- JNI nulls/exceptions, native-bridge callback versions, fd/mapping errors, thread attachment and resource ownership were insufficiently checked.
- The ELF dependency had old page-size, bounds, concurrency and LZMA lifetime issues. Its unbounded dynamic/hash walks were not suitable as a general hostile-input parser.
- Output could silently fail, truncate an existing successful file, or retain memory proportional to the full dump.
- Packaging used private AGP intermediate paths and legacy variant APIs. Workflow input was interpolated into `sed` shell commands. CI only ran a manually requested Release build.

See [the initial audit](AUDIT.md) for the end-to-end trace.

## 2. Dependencies and toolchain

| Component | Original | Modernized | Reason / migration |
|---|---|---|---|
| Gradle | 8.6 | 9.7.1 | Current stable release; regenerated wrapper scripts/JAR, executable bit and verified distribution SHA-256. |
| Android Gradle Plugin | 8.2.0 | 9.4.0 | Current stable API-37-capable plugin. Replaced legacy variants/private intermediates with `androidComponents` and the public AAR artifact. |
| Compile SDK | 34 | 37 / Android 17 | Latest stable major Android release at audit time. A native module inherits its host app's target behavior. |
| Build Tools | AGP default 34.0.0 | 37.0.0 | Explicit current stable SDK build tools. |
| Minimum SDK | 23 | 23 | Retained Android 6 native ABI floor. Older-OS runtime claims remain limited to actual tests below. |
| NDK | 25.2.9519653 / r25c | 30.0.16248370 / r30 LTS | Current stable NDK. Android's supported Clang/sysroot are used together. |
| Android Clang | NDK r25 toolchain | NDK r30 Clang 21, `clang-r574158c` | Do not substitute unrelated host LLVM binaries for the Android compiler. |
| CMake | 3.22.1 | 4.4.3 | Current stable CMake, pinned in development requirements and discovered by AGP on PATH. SDK CMake 4.1.2 was also exercised during migration. |
| Ninja | SDK CMake's bundled version | 1.13.2 package | Explicit pinned build tool; wheel reports its Kitware jobserver build suffix. |
| CI JDK | Temurin 17 | Temurin 25 LTS | Current LTS line. Local final validation uses 25.0.4.1; JDK 21 also built the project. JDK 26 is not required by a project with no JVM source. |
| C++ | C++20 | C++20, required, extensions off | Kept an adequate supported standard; no benefit justified a language/standard rewrite. |
| C | Compiler default | C17 for xDL | Explicit upstream language requirement. |
| xDL | 1.2.1 | 2.4.0, commit `6ab03d9…` | Upstream page-size, concurrency, bounds and lifetime fixes. Kept vendored and statically linked, with provenance/license. |
| Zygisk API | 2 | 2, portable attribute syntax | API 5 is available but raises the Magisk floor to 27 without supplying a required feature. API 2 preserves Magisk 24 loading compatibility. |
| NativeBridge | Unversioned callback use; namespace pointer `3` | Checked v2/v3/v7 capabilities | No guessed namespace address; checked loading, trampoline and JNI paths. Translation remains untested on this ARM host. |
| AndroidX / Jetifier / Prefab | Enabled, unused | Removed | No corresponding dependency or code. |
| Kotlin / annotations | No original source dependency | Disabled AGP built-in Kotlin | Prevented AGP's implicit Kotlin stdlib/annotations dependency from entering a native-only library. |
| Maven local repository | Enabled, unused | Removed | Reproducible central/plugin repositories only. |
| checkout | v6 | v7.0.1, SHA pinned | Current official action. |
| setup-java | v5 | v6.0.1, SHA pinned | Current official action; Gradle cache handled separately. |
| setup-python | Absent | v7.0.0, SHA pinned | CI Python 3.14, explicit development-requirements cache key. |
| Gradle setup | Absent | gradle/actions v6.3.0, SHA pinned | Wrapper validation and Gradle caching; read-only PR caches. |
| upload-artifact | v7 | v7.0.1, SHA pinned | Publishes actual ZIPs and checked reports. |
| Hosted runner | `ubuntu-latest` | `ubuntu-24.04` | Current stable runner image; Ubuntu 26.04 was still marked preview. |
| Android SDK CLI | Host 1.0.15985488 | Host 1.0.16261425 | Explicitly updated and verified at the user's request. CI bootstraps verified Command-line Tools 23 and updates Android CLI. |
| Formatter | Absent | clang-format 23.1.1 | Pinned formatting check for maintained sources. Upstream xDL/Zygisk headers are excluded from restyling. |

Release references: [Gradle versions](https://services.gradle.org/versions/current), [AGP 9.4](https://developer.android.com/build/releases/agp-9-4-0-release-notes), [NDK downloads](https://developer.android.com/ndk/downloads), [NDK r30 changes](https://github.com/android/ndk/wiki/Changelog-r30), [CMake 4.4.3](https://github.com/Kitware/CMake/releases/tag/v4.4.3), [Zygisk API compatibility](https://github.com/topjohnwu/zygisk-module-sample), [runner image status](https://github.com/actions/runner-images).

No Rust, AndroidX, Kotlin application code, new native shared dependency, or proprietary regression fixture was introduced. License notices are bundled with the module.

## 3. Android compatibility

| Environment | API / Android | ABI / pages | Runtime coverage |
|---|---|---|---|
| Provided ADB target, BlueStacks Air | 33 / Android 13 | ARM64 / 4 KB | Actual Zygisk app selection, initialization and full dumps for all three supplied apps. Kitsune Magisk 31.0 with ReZygisk 1.0.0 build 521. |
| `Il2cppAudit_API29_ARM64` | 29 / Android 10 | ARM64 / 4 KB | Stock Magisk 30.7 Zygisk; Rust Axion and Evil Hunter Tycoon runtime dumps. OnceWorld requires API 32 and was not installed here. |
| `Il2cppAudit_API35_ARM64` | 35 / Android 15 | ARM64 / 4 KB | Stock Magisk 30.7 Zygisk; full dumps for all three apps. |
| `Il2cppAudit_API37_ARM64_16K` | 37 / Android 17 | ARM64 / 16 KB | Full dumps for all three apps through ReZygisk 1.0.0 build 515, plus the synthetic native integration suite. |
| Host sanitizer tests | macOS / Apple Silicon | ARM64 host | ELF32/ELF64, maps, arithmetic, metadata prefix, output and instruction-profile fixtures. |
| GitHub runtime job | Android 17 | x86_64 | Synthetic native tests; the completed CI result is recorded in the validation section. |
| Other module payloads | Native floor API 23 | ARMv7, x86, x86_64 | Debug/Release compilation and archive/ELF checks. This is not a live Unity compatibility claim. |

The provided ADB target is virtual, not a physical device. No physical-device or cross-ABI NativeBridge runtime claim is made.

The local AVDs use Google APIs images. Separate patched ramdisks were kept outside the shared SDK, preserving the original system images. On Android 17, stock Magisk 30.7 failed to transfer module memfds because of a provider-level SELinux denial before our entry point. ReZygisk was used after completing its standard policy setup; the AVD's manual bootstrapping lacked Magisk's preinit policy installation. SELinux remained enforcing. The dumper itself installs no SELinux policy.

Directly pushing a configuration file into a module directory also exposed a practical label issue: `adb_data_file` was unreadable from the pre-specialization context. `scripts/set_targets.py` now stages the file, copies the installed module's label, sets mode 0644 and renames it atomically. Misconfiguration gets a diagnostic instead of silent selection failure.

Headless AVD app launches use the resolved `am start` component. Monkey's default physical-key event mix rejected the headless devices and was corrected in the test driver. Initial Android 10 cold-launch ART faults were observed; a subsequent launch completed the dump. These transient platform/app-startup faults are not evidence of universal Android 10 app stability.

## 4. IL2CPP compatibility

The original implementation resolved exports through xDL and then trusted fixed private structures. The modernized implementation separates discovery, symbol binding, readiness, member enumeration, layout calibration and output.

Resolution strategies are:

1. An already loaded `libil2cpp.so`, followed by normal `RTLD_NOLOAD`/`dlsym` where namespace access permits it.
2. A bounded in-memory dynamic ELF index, including sectionless ELF and relative/already-relocated table addresses.
3. Applicable file `.symtab` fallback, with resident-code verification for recovered addresses.
4. Renamed linker modules and mapped ELF candidates with multiple executable IL2CPP API anchors.
5. Image APIs for enumeration; managed reflection through `il2cpp_runtime_invoke` where image enumeration is absent.

Private executable addresses are not guessed from broad signatures. ARM64 readiness uses bounded accessor/predicate instruction profiles to read validated runtime globals. Method-pointer layout is independently calibrated using two corlib delegates and checked pointer fields, then validated against executable mappings. Type/member runtime structures are opaque. Parameter attributes/byref are obtained through APIs, and a made-up fixed managed-array layout is no longer used.

All three supplied applications use metadata version 39 and Unity 6000.3 variants. Rust Axion and OnceWorld retained their original working paths. Evil Hunter Tycoon's original failure was the unsafe readiness probe, rather than a need to decode metadata version 39 tables. Its APK-mapped library and altered on-disk section/text layout also demonstrate why in-memory program-header/API discovery matters.

Direct type-name formatting encountered invalid compact/runtime-derived member representations during testing; the final renderer normalizes through `Class::FromType` and uses class-name APIs. A separate regression fixed stale map permissions when runtime-created array/type names occupied newly committed allocator reservations.

The dumper still uses the live runtime as the authority for metadata and registrations. A named metadata mapping's common header is identified for diagnostics, not parsed as a complete v39/v104/v106 file format. Completely absent/renamed APIs and unknown private layouts remain explicit limitations.

## 5. Supplied application results

The baseline comparison below is on the provided Android 13 ARM64 target, with the same installed application versions.

| Application | Package / version | ABI | Unity / metadata | Original result | Modernized result |
|---|---|---|---|---|---|
| Rust Axion | `com.prayermicelle.rustaxion` / 1.1.8 | ARM64 | 6000.3.8f1 / 39 | Successful, 15,562 classes | Successful, 15,562 classes, 115,584 methods; all 114,613 nonzero RVAs match the original. |
| Evil Hunter Tycoon | `com.superplanet.evilhunter` / 1.413 | ARM64 | 6000.3.9f1 / 39 | SIGSEGV during the null-thread readiness probe; no dump | Successful, 17,609 classes and 147,376 methods, including 146,057 nonzero method addresses. |
| OnceWorld | `work.ponix.onceworld` / 2.7.5 | ARM64 | 6000.3.23f1 / 39 | Successful, 16,649 classes | Successful, 16,649 classes, 127,262 methods; all 126,059 nonzero RVAs match the original. |

The two known-good targets preserve **240,672 nonzero method RVAs exactly**. Final checked dumps have zero invalid-name diagnostics. Null/abstract/unavailable method addresses remain explicitly represented rather than underflowing the load bias.

Rust Axion and OnceWorld expose extracted native-library paths on the supplied target. Evil Hunter Tycoon is loaded from the ARM64 split APK (`...apk!/lib/arm64-v8a/libil2cpp.so`). Runtime metadata was visible in app-specific `il2cpp/Metadata/global-metadata.dat` mappings. The tests used the actual installed package names, not display-name guesses.

No game-specific offsets, addresses, signatures or application-condition branches were added.

## 6. Performance

Correctness checks and richer qualified type names add work compared with the original unchecked formatter. Do not interpret a successful build, an isolated benchmark, or total app RSS as proof that every runtime became faster.

Observed provided-device baseline and intermediate modernized runs:

| Target | Original dump stage | Modernized checked dump stage before batched reads | Original / new bytes | Launch-to-observed-completion |
|---|---:|---:|---:|---|
| Rust Axion | 343 ms | 708 ms | 17,942,153 / 22,126,036 | 2.253 / 2.317 s |
| OnceWorld | 295 ms | 541 ms | 19,079,389 / 23,619,276 | 2.330 / 2.249 s |
| Evil Hunter Tycoon | Failed | 768 ms | 0 / 26,962,004 | 3.367 s for the working implementation |

These are single runtime samples, not statistically controlled whole-app benchmarks. Formatting overhead is reported openly; end-to-end availability stayed in the same range for the previous successful targets. Unrelated game startup allocations make whole-process peak RSS unsuitable for claiming dumper-only savings.

The full-output retention was removed. An isolated Release benchmark wrote identical 83,480,000-byte synthetic output through the old buffering strategy and the new streaming strategy, using the same checked file writer:

| Strategy | Three elapsed samples | Median | Peak RSS |
|---|---|---:|---:|
| Retain all output strings | 160 / 218 / 232 ms | 218 ms | 109,504–109,520 KiB |
| Stream each output | 157 / 158 / 148 ms | 157 ms | 1,392 KiB |

This demonstrates the staging-memory improvement and about 28% lower median staging time for that workload. It is not a claim of the same whole-game speedup.

Additional work reduces repeated immutable-name resolution and string copies. Method-pointer reads are batched with `process_vm_readv` after class-method enumeration, respecting the system IOV limit. Partial/failed batches use the same checked per-pointer fallback. This removes one syscall per method without assuming contiguous/private object allocations or caching mutable memory pages. Runtime logs distinguish formatting and publication timing.

## 7. Architecture decisions

Incremental C++ refactoring was chosen over Rust or a complete rewrite. The Android/Zygisk/IL2CPP boundary remains a C/C++ ABI; a Rust parser would introduce another compiler, cross-compilation and FFI/lifetime boundary without removing the need to invoke the live runtime. Bounded components, fixed-width ELF types, opaque runtime handles, RAII and sanitizer-tested fixtures address the observed problems directly.

The module keeps its static C++ runtime, isolates exported symbols in both configurations, and has no extra shared-library deployment requirement. SDK/Gradle upgrades were separated from xDL and runtime changes. Source-compatible Zygisk API 2 was retained rather than unnecessarily raising the manager version floor.

## 8. Tests and CI

Automated coverage includes:

- Strict maps parsing: malformed lines, overflowing addresses, spaces, deleted/APK/anonymous names, adjacent ranges, permissions and target-prefix isolation.
- Binary bounds and unaligned reads, addition overflow, common metadata-header identification and truncated/invalid headers.
- ELF32/ELF64 dynamic and local symbols; GNU/SYSV shapes, missing terminators, bad offsets/counts/links/entry sizes, relocated pointers, missing sections, hash-cycle behavior and 2,000 deterministic mutation cases.
- Bounded ARM64 getter/predicate decoding, invalid branches/registers, cycles and alignment.
- Freshly committed allocator-page names, exact boundary NULs, unreadable pointers and partial batched pointer reads.
- Atomic output success/failure, previous-file preservation, temporary cleanup, invalid paths and symlink refusal.
- Synthetic runtime integration: ordinary exports, renamed modules, forced map fallback, hidden/file-only exports, absent required APIs, shifted method prefixes, legacy reflection, managed exceptions, invalid counts, initialization timeout and balanced thread attachment.

Host tests use ASan and UBSan. Android fixtures execute the real resolver/dumper and are not proprietary Unity fixtures. Native sources compile with `-Wall -Wextra -Wpedantic -Wformat=2 -Wshadow -Werror`; vendor code has separate diagnostics. Selected NDK clang-tidy analyzer checks found no project-source findings. The formatter and archive checks run in CI.

CI uses current official SHA-pinned actions, stable Ubuntu 24.04, JDK 25 and a checksum-verified SDK bootstrap. It builds Debug and Release across four ABIs, runs Android lint, validates ZIP contents/exports/static-runtime/page alignment, and executes synthetic tests in an Android 17 x86_64 emulator. It fails on actual build/test failures and uploads the module ZIPs and reports. [The clean GitHub Actions run](https://github.com/Jordan231111/Zygisk-Il2CppDumper/actions/runs/34660293687) passed on source revision `cbbcd14`: both jobs succeeded, the four-ABI build/lint completed, and Android 17 x86_64 passed 98 core checks, 192 ELF checks, 2,000 mutations and all eight runtime scenarios. A separate full fresh clone of that revision built Debug, Release and lint with the documented pinned tools in 18 seconds; its ASan/UBSan host tests also passed. Later documentation/license-only packaging updates are checked by the final branch CI run.

## 9. Remaining limitations

- There is no universal support claim for protected or custom IL2CPP runtimes. Missing APIs, erased unregistered ELF headers and unrecognized initialization/layout profiles can remain unsupported.
- File-only `.symtab` recovery does not currently extract embedded APK members or decompress `.gnu_debugdata`; normal in-memory exported-symbol discovery works for APK-loaded libraries.
- ARM64 readiness profiles are tested against the supplied Unity versions. Other ABIs currently use a corlib/settling heuristic; they need further live Unity validation beyond builds/synthetic tests.
- NativeBridge translation was audited and compile-checked, not exercised on this ARM64 host. Vendor namespace/trampoline behavior can differ.
- No complete raw metadata parser, code-registration scanner, arbitrary decryption or broad executable-signature guessing was added. The runtime remains the source of type/registration information.
- Generic arguments, complete nested-type qualification, custom attributes/events and arbitrary static values are not reconstructed into a compilable C# project. Enum integral values are handled with validated signedness/width.
- Runtime library unload, incompatible API implementations, or concurrent runtime teardown cannot be made universally safe by checking pointers in the dumper. No process-wide fault suppression is installed.
- Tested Android 17 Zygisk coverage requires a functioning provider and its normal policy setup; a broken provider that cannot deliver any module fd fails before this module can act.
- No physical-device, ARMv7 Unity, x86 Unity or cross-ABI translation guarantee is inferred from compilation.

## 10. Major changed files

| Area | Files / change |
|---|---|
| Build | Root/module Gradle files, wrapper, `requirements-dev.txt`, native CMake: stable tools, public artifact packaging, four ABIs, no unused managed dependencies. |
| Zygisk/JNI | `main.cpp`, `hack.*`, `game.h`, minimal `zygisk.hpp` portability patch: safe selection, ownership, callbacks and worker setup. |
| Discovery/API | `discovery.*`, `il2cpp_api.*`, curated API declarations: bounded binding and explicit capability failures. |
| Runtime/dump | `il2cpp_dump.*`, opaque `il2cpp-class.h`, logging: safe readiness, API-based types/reflection, calibration, streaming and diagnostics. |
| Portable core | `core/binary.h`, `elf.*`, `maps.*`, `runtime_layout.*`, `target.*`, `output.*`: independent checked primitives. |
| Dependency | `xdl/`: pinned 2.4.0 sources, MIT license and upstream provenance. |
| Tests | Root CMake and `tests/`: host sanitizers, mutation corpus, synthetic runtimes and output benchmark. |
| Tooling | `scripts/`: formatting, module verification, native device testing, bounded boot wait and atomic/context-correct target configuration. |
| CI | Build workflow and Dependabot: official pinned actions, SDK bootstrap, caches, builds and runtime tests. |
| Docs/licenses | Both READMEs, audit/architecture/report, notices, module license texts and upgrade-preserving installer. |

## 11. Reproduction

Use the [README build/install commands](../README.md). The exact major checks are:

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -r requirements-dev.txt
./gradlew :module:assembleDebug :module:assembleRelease :module:lint --warning-mode=fail
cmake -S . -B build/host -G Ninja -DCMAKE_BUILD_TYPE=Debug -DDUMPER_SANITIZERS=ON
cmake --build build/host
ctest --test-dir build/host --output-on-failure
python scripts/check_format.py
python scripts/verify_module.py out/zygisk-il2cppdumper-v1.4.0-release.zip --readelf "$ANDROID_HOME/ndk/30.0.16248370/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf"
```

For native device tests, substitute the device ABI and use `darwin-x86_64` for NDK executable paths on macOS:

```sh
cmake -S . -B build/android -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_HOME/ndk/30.0.16248370/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-23 -DANDROID_STL=c++_static \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/android
python scripts/test_device.py --serial DEVICE --build-dir build/android
```

For the isolated benchmark:

```sh
cmake -S . -B build/bench -G Ninja -DCMAKE_BUILD_TYPE=Release -DDUMPER_BENCHMARKS=ON
cmake --build build/bench
build/bench/output_benchmark buffered
build/bench/output_benchmark streamed
```

AVD configurations can be recreated with installed Google APIs images:

```sh
android sdk install system-images/android-29/google_apis/arm64-v8a
android sdk install system-images/android-35/google_apis/arm64-v8a
android sdk install system-images/android-37.0/google_apis_ps16k/arm64-v8a
avdmanager create avd -n Il2cppAudit_API29_ARM64 -k 'system-images;android-29;google_apis;arm64-v8a' --device pixel_5
avdmanager create avd -n Il2cppAudit_API35_ARM64 -k 'system-images;android-35;google_apis;arm64-v8a' --device pixel_5
avdmanager create avd -n Il2cppAudit_API37_ARM64_16K -k 'system-images;android-37.0;google_apis_ps16k;arm64-v8a' --device pixel_8
```

Real Zygisk tests additionally require a correctly installed/rooted provider; synthetic native tests do not. Test-device root modifications and application APKs are not distributed with the repository.

Install, select targets, enable verbose logs and collect a completed dump:

```sh
adb -s DEVICE push out/zygisk-il2cppdumper-v1.4.0-release.zip /data/local/tmp/il2cppdumper.zip
adb -s DEVICE shell su -c 'magisk --install-module /data/local/tmp/il2cppdumper.zip'
# Enable Zygisk in the chosen manager/provider, then reboot.
adb -s DEVICE reboot
python scripts/set_targets.py --serial DEVICE com.example.authorizedapp
adb -s DEVICE shell su -c 'touch /data/adb/modules/zygisk_il2cppdumper/verbose'
adb -s DEVICE shell am force-stop com.example.authorizedapp
adb -s DEVICE shell cmd package resolve-activity --brief com.example.authorizedapp
adb -s DEVICE shell am start -n PACKAGE/RESOLVED_ACTIVITY
adb -s DEVICE logcat -v threadtime 'Il2CppDumper:V' 'Unity:I' 'CRASH:E' '*:S'
adb -s DEVICE shell su -c 'cat /data/user/0/PACKAGE/files/dump.cs' > dump.cs
```
