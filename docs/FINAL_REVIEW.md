# Final review and compatibility evidence

Review date: September 13, 2026. The comparison baseline is the former `master`, `aa83f73bdf95e0674541a8ca1824e23eb8f006a7`. The modernization was merged into `master` at `fe77cde73d678dddc48ca43b3b42fb3665b4711e`; subsequent compatibility fixes are recorded in its history. Current module version: **1.4.2**.

This is an engineering review with build, sanitizer, synthetic runtime and authorized application evidence. It is not a guarantee that every protected Unity runtime, root provider or device behaves identically.

## Before and after

| Area | Former master | Modernized implementation | Reason / compatibility impact |
|---|---|---|---|
| Core purpose | Zygisk module writes an IL2CPP `dump.cs` | Same purpose and Magisk installation model | Preserved; no replacement app or language migration |
| Languages | C++20, C, Groovy, shell | C++20, C17, Groovy, shell; Python developer tools | C++ remains appropriate for the native runtime; Rust was considered and not introduced |
| Gradle / AGP | 8.6 / 8.2.0 | 9.7.1 / 9.4.0 | Public variant/AAR APIs replace deprecated and private build paths |
| JDK | CI 17 | CI 25 LTS; local 21 and 25 tested | Current supported build tools, no managed runtime dependency |
| Android SDK | Compile 34 | Compile 37, Build Tools 37.0.0 | Android 17 build coverage; the module inherits the target app's target-SDK behavior |
| Minimum Android | API 23 | API 23 retained | A binary compatibility floor, distinct from the real Android runtime matrix below |
| NDK / Clang | r25c / its bundled compiler | r30, 30.0.16248370 / Clang 21 | Current stable Android compiler/sysroot pair |
| CMake / Ninja | 3.22.1 / implicit bundled Ninja | 4.4.3 / 1.13.2 | Explicit reproducible development requirements |
| Wrapper | No distribution checksum; script not executable | Regenerated wrapper, verified checksum, executable script | Clean clone supports the documented invocation |
| Unused build dependencies | AndroidX/Jetifier/Prefab settings and obsolete repository/build logic | Removed; automatic unused Kotlin runtime dependency disabled | No Java/Kotlin/AndroidX runtime library is required |
| xDL | Vendored 1.2.1 | Pinned upstream 2.4.0 with provenance and license | Updated Android, page-size and resource handling; additional parsing is independently bounded |
| Zygisk API | API 2 | API 2 retained, minimal Clang attribute portability fix | Preserves Magisk 24+ API compatibility instead of requiring a newer provider unnecessarily |
| ABIs | ARMv7, ARM64, x86, x86_64 builds | Same four payloads, with additional execution and stress evidence | NativeBridge translation remains separately qualified |
| Page sizes | Old 4 KB assumptions | Runtime mapping sizes and 16 KB ELF alignment | Tested on real Android 17 ARM64 16 KB mappings |
| Local package selection | Edit source header | `-PtargetPackage=PACKAGE`; source default still exists | No source edit is needed for the ordinary build |
| Manual CI selection | Required package input, interpolated into shell/sed | Required package input passed as a validated Gradle property | Same user flow, without command interpolation |
| Module download | Workflow assembled a ZIP from private build paths | Both routes use the same public AAR packaging task; `out/zygisk.zip` aliases the versioned Release ZIP | CI uploads exactly one artifact, the installable ZIP directly, without an outer ZIP wrapper |
| Module details | CI description included the package | Default target shown consistently for local and CI builds | Users can identify the build target in Magisk |
| Target configuration | One compile-time exact process | Same default plus optional `targets.txt`, exact secondary processes and explicit `PACKAGE:*` | Advanced overrides persist; installer explains when they supersede the build target |
| Library discovery | Named library, short fixed retry window | Named loader lookup, renamed module/API anchors, validated mapped ELF fallback | Handles split-APK loading and more namespace/name variations without forced library loading |
| API binding | Large unchecked export table | Curated required/optional capabilities; normal lookup, bounded ELF and applicable file symbols | Missing APIs fail with diagnostics; file fallback also recovers enumeration/calibration groups |
| Initialization | Unbounded null-thread probe | Deadlines and validated ARM64 getter profiles, including GOT indirection | Avoids the observed Evil Hunter crash; supports the older getter form encountered in Unity 2021/2022 |
| Runtime layouts | Private type bitfields, fixed method prefix and fake managed-array layout | Opaque runtime objects, API-based types/reflection, two-delegate method calibration | No game-specific offsets or execution addresses |
| Enumeration | Borrowed assembly array, repeated image lookup | Bounded verified snapshots; image/name pairs captured once | Detects changing/unreadable arrays and avoids redundant lookups |
| Memory access | Unchecked direct structure reads | Checked kernel reads, pointer batches, lazy thread-safe `/proc/self/mem` fallback, wide offsets on 32-bit ABIs | Reduces unsafe reads and unnecessary open descriptors |
| Output | Entire dump retained; unchecked/truncating file write | Class-at-a-time output, private temporary file, checked flush/fsync/rename | A failed dump preserves the previous successful file |
| Diagnostics | Limited logs and unconditional success paths | Stage, ABI/API/page, strategy, capabilities, calibration, output and failure diagnostics | Verbose per-image output is optional |
| Optional mount isolation | No explicit option | Provider's public `FORCE_DENYLIST_UNMOUNT`, opt-in | Demonstrated benefit for two protected apps on Android 17; off by default to preserve other modules' behavior |
| Dump collection | Root-shell `cat` | SHA-256-verified `adb pull` helper with private staging | Avoids observed root-shell newline conversion; failed transfer preserves the previous local file |
| Automated tests | None | Portable parsers, output/memory checks, synthetic runtime scenarios and deterministic stress corpus | Fixtures contain no proprietary game material |
| CI | Manual Release build | Push/PR/manual builds, Debug/Release, lint, sanitizers, static analysis, archive validation and Android runtime tests | Official actions pinned to release commits; manual downloads compile Release and verify the ZIP; full maintainer validation runs on pushes/PRs or explicit request |
| Documentation / licenses | Minimal setup instructions | Build/install/troubleshooting, architecture, evidence, limitations and bundled notices | Clarifies tested support and the unchanged main workflow |

Toolchain versions were checked against [AGP's release notes](https://developer.android.com/build/releases/agp-9-4-0-release-notes), [Gradle's current release notes](https://docs.gradle.org/current/release-notes.html) and [the NDK downloads page](https://developer.android.com/ndk/downloads). The remaining component decisions and original audit are in [the engineering report](ENGINEERING_REPORT.md).

## Review findings and fixes

The final pass found and corrected actual gaps instead of treating a green build as sufficient:

1. File-symbol recovery previously ran only when an individually required export was missing. Hidden enumeration/calibration exports could therefore disable a valid capability. Synthetic libraries reproduced both failures; the resolver now considers complete capability groups.
2. The memory-file fallback used `off_t`, which is narrow on Android's 32-bit ABIs. It now uses wide offsets. A forced fallback above 2 GiB is tested on x86 and explicitly required in 32-bit CI.
3. Assembly-array copying did not check whether the borrowed array changed. Three bounded attempts now verify pointer, count and contents. Persistent change fails without replacing an existing dump; a transient change recovers.
4. Runtime tests could pass even with missing method addresses. They now verify exact expected RVAs/virtual addresses, including the shifted layout fixture.
5. Additional ELF section/range checks, cache byte budgets, final class-output bounds and setter parameter limits reduce malformed-input amplification. Output names reject embedded NULs.
6. `/proc/self/mem` was opened even when unused. It is now opened on demand with `call_once`; concurrent readers and descriptor cleanup are tested.
7. Post-merge application testing exposed a GOT-indirect corlib getter used by the supplied Unity 2021/2022 builds. The bounded ARM64 decoder now represents the extra data load without executing the getter or embedding observed offsets. A compiled GOT fixture and negative tests accompany this change.
8. Package selection, target visibility, preserved-configuration messages and transfer cleanup were reviewed for the ordinary end-user workflow.

No complete rewrite, game-specific offset table, arbitrary process-wide fault suppression, or proprietary test binary was added. The maintained tests are necessary regression coverage and are retained. The final Release ZIP is 643,598 bytes (original baseline ZIP: 580,887); its ARM64 payload is 409,896 bytes (original: 442,568). No test fixtures are packaged in the module.

## User workflow

The ordinary workflow remains:

1. In GitHub Actions, run **Build and test** on `master`, supplying the app's package name; download **zygisk.zip** after success. Alternatively compile locally with `./gradlew :module:assembleRelease -PtargetPackage=PACKAGE`.
2. Install that ZIP in Magisk, enable a functioning Zygisk provider and reboot.
3. Launch the selected app and collect its `files/dump.cs`.

Every successful workflow publishes exactly one artifact, `zygisk.zip`. Normal manual downloads compile Release for all four ABIs and validate the ZIP. Pushes/PRs and manual runs with `full_validation` enabled also require host tests, Debug/lint, static analysis and emulator checks to pass. Test diagnostics remain in the workflow logs.

The runtime configuration helper is optional. A user who has created `targets.txt` has deliberately selected an override; the installer preserves it and now explains how to return to the build target. Mount isolation is also optional. The [public Zygisk API](https://raw.githubusercontent.com/topjohnwu/zygisk-module-sample/master/module/jni/zygisk.hpp) specifies that this option requests provider unmounting during specialization. It can affect other modules' mounts in selected processes, so it is not silently enabled for everyone.

GitHub's [direct-file artifact option](https://github.com/actions/upload-artifact#upload-an-individual-file-unzipped) prevents accidental ZIP nesting. Both automatic and manual workflow artifacts were downloaded, checksum-verified and installed through Magisk. Local and CI ZIPs need not be byte-identical across operating systems; their payload structure and runtime behavior were checked independently. The local `zygisk.zip` alias is byte-identical to that local build's versioned Release ZIP. A final manual build selected Neural Cloud, showed that target in module details and selected it without any `targets.txt`; it dumped successfully on Android 17 after enabling the optional mount-isolation feature required by that test environment.

## Expanded runtime evidence

The following are real app-process dumps, not synthetic Unity-version claims. Raw APKs, dumps and logs are not committed.

| App | Installed package | App version | Unity / metadata | Result on provided Android 13 ARM64 target | Additional evidence |
|---|---|---|---|---|---|
| Rust Axion | `com.prayermicelle.rustaxion` | 1.1.8 | 6000.3.8f1 / 39 | 15,562 classes; 115,584 methods; 114,613 nonzero RVAs | Final code rechecked on Android 10, 15 and 17 |
| Evil Hunter Tycoon | `com.superplanet.evilhunter` | 1.413 | 6000.3.9f1 / 39 | 17,609 classes; 147,376 methods; 146,057 nonzero RVAs | Original null-thread probe crashed; modern code rechecked on Android 10, 15 and 17 |
| OnceWorld | `work.ponix.onceworld` | 2.7.5 | 6000.3.23f1 / 39 | 16,649 classes; 127,262 methods; 126,059 nonzero RVAs | Android 15 and 17; app itself requires API 32+ |
| Slayer Legend | `com.gear2.growslayer` | 600.9.6 | 6000.3.10f1 / nonstandard metadata prefix | 32,517 classes; 248,725 methods; 247,006 nonzero RVAs | Android 17 / 16 KB with module-mount isolation |
| Idle Poseidon | `com.mouseduck.seawar` | 1.4.26 | 6000.0.62f1 / nonstandard metadata prefix | 14,123 classes; 93,873 methods; 92,718 nonzero RVAs | Android 17 / 16 KB with module-mount isolation |
| Neural Cloud | `com.sunborn.neuralcloud.en` | 2.0.4 | 2021.3.56f2 / 31 | 10,027 classes; 96,936 methods; 87,054 nonzero RVAs | Passed after the generic GOT-profile fix; also verified on Android 17 with mount isolation |
| Seven Knights Idle Adventure | `com.netmarble.skiagb` | 1.31.00 | 2022.3.67f2 / 31 | 20,835 classes; 140,366 methods; 138,874 nonzero RVAs | Passed after the same fix; zero invalid names |
| OXIDE | `com.catsbit.oxidesurvivalisland` | 1.13.11915 | 6000.3.18f1 / 39 | No dump: required exports have been renamed | Zero `il2cpp_` dynamic names; no `.symtab`; Unity's loader also uses renamed symbols; partial Mono exports do not provide the missing capability set |

All seven successful apps had zero invalid-name diagnostics. Native addresses remain unavailable for some methods: Neural Cloud has 9,882 such entries and Seven Knights has 1,492; the dumper does not guess addresses for them. Their complete output was identical before and after the review fixes after normalizing ASLR-dependent virtual addresses, across the applicable device/AVD comparisons. All 240,672 original nonzero RVAs from Rust Axion and OnceWorld remained present.

For Slayer Legend and Idle Poseidon on the stock Android 17 AVD, the same binary with mount isolation **off** exited before dumping. With it **on**, both completed on three repeated launches; a downloaded CI build also succeeded. This supports a concrete compatibility benefit in this environment. It does not establish invisible instrumentation or unrestricted gameplay/protection compatibility.

## Architecture and stress coverage

| Execution environment | Coverage and limitation |
|---|---|
| Actual Android ARM64 | API 29/33/35 with 4 KB pages, API 37 with 16 KB pages; real Zygisk loading and application dumps; synthetic native suites |
| Android 17 x86_64 CI emulator | Real Android/Bionic execution of the native resolver/dumper against synthetic runtimes; not a proprietary x86 Unity game |
| Local x86_64 full-system emulation | Android 11 Bionic in an isolated Linux guest; 33 runtime scenarios including repetitions and 200,000 ELF mutations |
| Local 32-bit x86 execution in that guest | Same 33 runtime scenarios and stress corpus; forced wide-offset read above 2 GiB passed |
| Local full-system ARMv7 emulation | Android 6 Bionic on a Linux ARMv7 kernel; 13 runtime scenarios and 200,000 ELF mutations passed; the kernel did not supply mappings above 2 GiB |
| Host sanitizers | ASan/UBSan on macOS and 32/64-bit Linux CI; deterministic malformed ELF input; bounds, output and memory utilities |

The initial ARM user-mode emulator retained host mappings after guest unmapping, invalidating that negative memory test. Full-system ARM emulation passed the unmapped-memory check. The high-address test was made capability-aware while remaining mandatory on CI's 32-bit x86 runner. No production check was weakened to accommodate an emulator. The final GOT change adds two integration scenarios, bringing the maintained runtime suite to 15 scenarios.

These tests exercise pointer widths, calling conventions, ELF layouts, field/method formatting and failure handling. They do not substitute for live Unity/Zygisk tests on every ABI. In particular, ARM translation through vendor NativeBridge implementations remains unverified end to end. Android [ABI documentation](https://developer.android.com/ndk/guides/abis) is the basis for keeping native ABI and translation coverage separate.

## Performance and confidence

The original single-run formatting measurements were 343 ms for Rust Axion and 295 ms for OnceWorld. The modern implementation performs checked reads and writes more complete names/address comments, so those numbers are not an equal-output CPU benchmark. No blanket CPU speedup is claimed.

A separate three-launch comparison on the provided device, before and after the final review changes, produced:

| App | Before review median ms (range) | After review median ms (range) |
|---|---:|---:|
| Rust Axion | 947 (643–1,055) | 652 (647–1,383) |
| Evil Hunter Tycoon | 684 (594–689) | 715 (646–726) |
| OnceWorld | 516 (510–557) | 628 (571–692) |
| Slayer Legend | 1,299 (1,164–1,352) | 1,251 (1,225–1,455) |
| Idle Poseidon | 565 (539–790) | 684 (600–706) |

These small samples have variable app startup/cache/scheduling state. Some medians increased; they do not prove that CPU overhead is optimal or unchanged. Correctness and preservation of output took priority. The isolated equal-output staging benchmark retained its substantial memory improvement: 109,504–109,520 KiB versus 1,392 KiB peak RSS, with comparable elapsed time. That measurement concerns staging only, not whole-game RSS.

Confidence is high in the documented package-to-Magisk workflow, checked native components and the tested app/ABI paths. Confidence is necessarily lower for unknown protected runtimes, vendor translation and concurrent runtime teardown. OXIDE is a precise known unsupported case, not a successful dump claim. The project is a tested release for its documented scope; “best possible,” universally undetectable and universally compatible would be unsupported claims.

## Newest Unity formats and useful next targets

The highest game build actually dumped in this audit was OnceWorld's Unity **6000.3.23f1**, using metadata **39**. That is not the newest Unity format available. Unity published [6000.6.0f1](https://unity.com/releases/editor/whats-new/6000.6.0f1) on August 31, 2026. [Cpp2IL's current metadata reader](https://github.com/SamboyCoding/Cpp2IL/blob/development/LibCpp2IL/Metadata/Il2CppMetadata.cs) documents newer 6.5 formats, including 106/107, and metadata 108 in the 6.6 line. Version numbers alone are insufficient: some 107 layouts differ between engine branches.

The runtime API design avoids depending on those raw metadata table layouts, but this does **not** establish compatibility with their runtime getters, class/type handles or method layout. Unity 6.5/6.6 runtime compatibility remains unverified. Synthetic acceptance of a metadata header number is not a real runtime test.

| Candidate | Verified facts | Practical use |
|---|---|---|
| [Sentry Android Unity test app](https://github.com/getsentry/sentry-unity/actions/runs/34669261431/artifacts/10290896166) | Inspected the `test.apk` in `testapp-android-compiled-6000.5-runtime`: ARM64 IL2CPP, Unity 6000.5.0f1, metadata 106 | A directly available Android test-app candidate. Download the artifact and extract its APK. It was inspected but not installed or run in this audit; CI artifacts can expire. |
| [Redline Legends](https://github.com/mohamedmastouri2000-boop/Redline-Legends) | The Android racing project's [ProjectVersion.txt](https://github.com/mohamedmastouri2000-boop/Redline-Legends/blob/main/ProjectSettings/ProjectVersion.txt) specifies 6000.6.0f1 | A newest-stable game source candidate. There is no published release APK in its repository; an Android IL2CPP build and header inspection are still needed. |
| [Hellforged Demo](https://github.com/BepInEx/BepInEx/issues/1395) | A firsthand diagnostic report identifies patch 0.1.14, Unity 6000.5.8f1 and metadata 107 | A concrete newer-format game example, but the reported build is Windows x64 and cannot directly test an Android Zygisk module. |

No pair of ready-to-install Play Store games with independently verified 6.6/108 builds was found. These candidates are labeled by what was actually verified; none is presented as an already-supported newest-format target.

## Remaining limits

- Fully renamed/removed API exports require a verified binding strategy; this release does not guess their identities from opaque names or addresses.
- ARM64 readiness uses bounded supported instruction profiles. Other native ABIs retain a corlib/settling heuristic that needs more live Unity validation.
- NativeBridge callbacks and namespace behavior vary by vendor; compilation and native x86 tests do not prove ARM-on-Intel translation.
- File-only symbols in embedded APK members and compressed `.gnu_debugdata` are not recovered by the bounded file fallback.
- The output is an inspection aid, not a complete compilable C# reconstruction: complete generic arguments, nested-type qualification, events and all custom attributes are outside its current coverage.
- Runtime APIs must still obey their ABI contracts. Arbitrary code replacement, runtime unloading/teardown and extreme memory pressure cannot be made universally safe by pointer checks.

## Reproduction and cleanup

The [README](../README.md) contains the exact supported build, installation, target selection, log and dump-collection commands. `scripts/test_device.py` runs the maintained synthetic suite on an explicitly selected device. `build/host/elf_tests --stress` runs 200,000 deterministic mutations. On 32-bit Linux/Android, `core_tests --require-high-address` makes the above-2-GiB regression mandatory.

The [final automatic master run](https://github.com/Jordan231111/Zygisk-Il2CppDumper/actions/runs/34772834045) passed the host and Android jobs at `ba2ddf6`, including all 15 runtime scenarios. The artifact API confirmed exactly one artifact, `zygisk.zip` (SHA-256 `956d557292e5ff84bb305478787fa34b87b50bc2866a6b9d9f2bf48f9aeb275a`). The earlier [manual workflow run](https://github.com/Jordan231111/Zygisk-Il2CppDumper/actions/runs/34767022121) and [automatic artifact run](https://github.com/Jordan231111/Zygisk-Il2CppDumper/actions/runs/34766761026) produced verified installable ZIPs that were installed and exercised. The [final manual master workflow](https://github.com/Jordan231111/Zygisk-Il2CppDumper/actions/runs/34771924567) also passed at `8977871` and produced the install-tested 1.4.2 ZIP. Its SHA-256 was `dcba5ec3d1fb88132e3d086b685c584950c7395259426d9b71784685f34c96f7`. A fresh independent full clone, updated to the final code and cleaned, built Debug, Release and lint in 13 seconds with Gradle caching; host sanitizer tests passed afterward. Cleanup removes the three test AVDs, the temporary Intel VM, the two SDK images installed solely for architecture tests, the additional guest-agent package, duplicate checkouts, build scratch, and private test inputs/raw logs. The final module ZIPs, seven useful final dumps and maintained regression source are retained. On the provided device, 11 task staging entries and incomplete dump files were removed. Fifty-eight older debug/backup entries and three previous app dumps were first copied to a private local backup; the archive SHA-256 was verified before removing their device copies. The existing device toolkit and cache remain in place.

The fork's Issues feature is enabled. Bug reports can be filed through the [Issues tab](https://github.com/Jordan231111/Zygisk-Il2CppDumper/issues).

## CI download latency adjustment

The full verification pipeline was unnecessarily expensive for end users rebuilding unchanged native code with a different package name. In run `34772834045`, host-job steps totaled about 34 seconds (the ordinary host build/tests were 5 seconds; multilib setup/testing was 21 seconds). Emulator setup and execution took 118 seconds, and static analysis took 23 seconds. This is useful maintainer validation but low yield for each package-only download.

Manual `workflow_dispatch` now defaults to Release compilation plus the fast archive check. The full suite remains automatic on pushes and pull requests and is available with the optional `full_validation` checkbox. Manual builds do not install or boot an emulator, build synthetic fixtures, or build Debug/run lint. Native warnings remain errors, all four ABI payloads remain included, and the one-artifact rule is unchanged. Validation failures continue to block publication when full validation is requested. Manual builds and automatic code checks use separate concurrency groups so downloading a package build does not cancel code validation.

Both routes passed at `4202edf`: the [default manual run](https://github.com/Jordan231111/Zygisk-Il2CppDumper/actions/runs/34775167024) made its single ZIP available in **79 seconds** and finished in **88 seconds**, compared with **310 seconds** to the artifact and **318 seconds** total in the [preceding full manual run](https://github.com/Jordan231111/Zygisk-Il2CppDumper/actions/runs/34774793617). These are observed runs, not guaranteed timings; runner queues and caches vary. The downloaded ZIP's SHA-256 matched the artifact digest, its configured package was correct, and validation confirmed all four ABI payloads, isolated exports, static C++ linkage and 16 KB alignment. The separate [automatic push run](https://github.com/Jordan231111/Zygisk-Il2CppDumper/actions/runs/34775167218) passed the complete host and Android validation suite. Follow-up wording changes explain optional multiple-target configuration in the README and keep the Actions package field simple; they do not alter build or runtime behavior.

## Optional CI unmount selection (v1.4.3)

The manual workflow now exposes an unchecked **Unmount module files in target apps** option. Local builds use `-PunmountModules=true`; the default is `false`. The choice is stored in `module.prop`, and the installer creates or removes the existing runtime `unmount` flag with Magisk's normal permission helper. Each installation applies the new ZIP's selection, including turning off a previously enabled flag. Target and verbose configuration remain preserved. The installer and module description display the selection. This adds no native code, dependencies, emulator steps or extra artifacts to manual builds.

Local Release builds passed with the setting off, on, then omitted to verify the default and incremental packaging. The on/off archives differed only in `module.prop`; all four native payloads were byte-identical. The packaged installer passed 12 fresh/upgrade/in-place configuration cases plus invalid input in an isolated POSIX shell harness with Magisk helper stubs. Existing target/debug settings were preserved. The archive verifier rejects a mismatched selection, and Gradle rejects invalid boolean values. These are packaging/installer checks; the previously tested unmount runtime is unchanged. The supplied ADB endpoint was unavailable during this follow-up, so no new device-installation claim is made. Temporary installer fixtures were removed.
