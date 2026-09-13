# Zygisk-Il2CppDumper

Dump IL2CPP type definitions, fields, properties, methods and native method addresses from an authorized Android application into `dump.cs`. The module runs inside the selected app through Zygisk and uses the app's live IL2CPP APIs.

It does not reconstruct executable code, decrypt arbitrary metadata files, or promise support for every protected/custom IL2CPP runtime. See [the architecture](docs/ARCHITECTURE.md), [test evidence and limitations](docs/ENGINEERING_REPORT.md), and [中文说明](README.zh-CN.md).

## Requirements

- Android 6/API 23 or newer at the native ABI level. Actual runtime coverage is listed in the engineering report; compilation alone is not a compatibility claim.
- Magisk 24+ with Zygisk enabled, or a compatible Zygisk implementation. API 2 is deliberately retained for older Magisk compatibility. Tested manager versions are listed in the report.
- `arm64-v8a`, `armeabi-v7a`, `x86`, and `x86_64` module payloads are built. Native-bridge translation is best effort and has additional vendor restrictions.
- 4 KB and 16 KB native page sizes are supported by the build and address calculations.

## Build

Use JDK 25 LTS (tested with Temurin 25.0.4.1), Python 3.11+, and Android SDK tools. JDK 21 also builds the project. Gradle is supplied by the checksum-verified wrapper.

```sh
git clone --branch master --recurse-submodules https://github.com/Jordan231111/Zygisk-Il2CppDumper.git
cd Zygisk-Il2CppDumper
# Use the modernization branch until these changes are merged:
git switch modernization/android-il2cpp

android sdk install platforms/android-37.0
android sdk install build-tools/37.0.0
android sdk install ndk/30.0.16248370

python3 -m venv .venv
. .venv/bin/activate
python -m pip install -r requirements-dev.txt
# Set ANDROID_HOME to your SDK directory and JAVA_HOME to your JDK if needed.
./gradlew :module:assembleRelease -PtargetPackage=com.example.authorizedapp
```

The pinned build uses AGP 9.4.0, Gradle 9.7.1, NDK r30, CMake 4.4.3 and Ninja 1.13.2. Activate the virtual environment so AGP finds the pinned CMake/Ninja on `PATH`. Windows users can activate `.venv\Scripts\activate` and invoke `gradlew.bat`.

Outputs are `out/zygisk-il2cppdumper-v1.4.1-release.zip` and, for `:module:assembleDebug`, the corresponding Debug ZIP. These are Magisk modules, not APKs. Release and Debug both isolate their C++ symbols and statically link the C++ runtime. Debug retains native debugging information before AGP's packaging strip step; unstripped binaries are under `module/build/intermediates/cxx/`.

Release builds also produce `out/zygisk.zip`, a byte-identical copy of the versioned Release module. Both supported build routes use the same Gradle packaging task:

- **Local:** run the Release command above and install `out/zygisk.zip`.
- **GitHub Actions:** open **Actions → Build and test → Run workflow**, select the modernization branch, enter the default package, and download the **zygisk.zip** artifact after all jobs pass. It is uploaded directly and can be installed in Magisk as downloaded. The separate diagnostics artifact contains reports and versioned archives.

The library has no Java/Kotlin/AndroidX runtime dependencies. Its compile SDK is 37; a native Zygisk module inherits the target app's Android behavior and cannot change the app's target SDK.

## Install and select targets

Install the ZIP in Magisk, enable Zygisk, reboot, and launch the selected application. From an explicitly selected device:

```sh
adb -s DEVICE push out/zygisk-il2cppdumper-v1.4.1-release.zip /data/local/tmp/il2cppdumper.zip
adb -s DEVICE shell su -c 'magisk --install-module /data/local/tmp/il2cppdumper.zip'
adb -s DEVICE reboot
```

The package supplied with `-PtargetPackage` is the default. You can change targets without rebuilding by placing a UTF-8 `targets.txt` file in `/data/adb/modules/zygisk_il2cppdumper/`. For example:

```text
com.example.authorizedapp
com.example.anotherapp:worker
```

Entries match exact process names. `com.example.authorizedapp:*` explicitly includes that package's secondary processes. It cannot match `com.example.authorizedapp2`. Blank/comment lines are ignored. Keep the file at most 4 KB. An empty file selects no apps. The installer preserves `targets.txt` and `verbose` during upgrades; remove `targets.txt` to return to the build-time default.

Use the helper to write the file atomically and match the installed module's SELinux label:

```sh
python scripts/set_targets.py --serial DEVICE com.example.authorizedapp
```

A direct `adb push` into the module directory can leave an `adb_data_file` label that Zygisk cannot read. The helper copies the label from `module.prop`; it does not change SELinux policy.

To request the Zygisk provider's module-mount isolation for the selected apps:

```sh
python scripts/set_targets.py --serial DEVICE --unmount on com.example.authorizedapp
```

This optional setting invokes the public `FORCE_DENYLIST_UNMOUNT` API after configuration and any bridge payload have been copied. It affects module mounts in selected app processes and can interfere with other modules that need them. It is off by default; use `--unmount off` to disable it. It does not guarantee that root, emulation, Zygisk or debugging checks will pass. A normal dump uses no debugger attachment or application-code patches; `/proc/self/mem` is opened only if the primary kernel read path fails. Non-target processes unload the module.

After a target-file change, force-stop and relaunch the selected app. A module binary update still requires a reboot. Never replace just one ABI in a module using native-bridge translation.

## Output and diagnostics

The default output is `<app_data_dir>/files/dump.cs`, usually `/data/user/0/PACKAGE/files/dump.cs`. Secondary processes use `dump-PROCESS_SUFFIX.cs`. Work-profile/user directories come from Zygisk and are not hard-coded.

A completed dump atomically replaces the previous file. An initialization, validation, or I/O failure preserves the previous dump. A killed process can leave a hidden `.dump.cs.PID.N.tmp` file; it is not a completed dump.

```sh
adb -s DEVICE logcat -v threadtime 'Il2CppDumper:V' 'Unity:I' 'CRASH:E' '*:S'
python scripts/pull_dump.py --serial DEVICE \
  --source /data/user/0/PACKAGE/files/dump.cs --output dump.cs
```

The collection helper verifies SHA-256 and preserves an existing local output if transfer fails; it avoids newline conversion in root shells.

Create `/data/adb/modules/zygisk_il2cppdumper/verbose` to log per-image progress on the next launch. Remove it to return to normal logging. Logs identify the process, ABI, API level, page size, ELF load bias, resolution strategy, readiness stage, metadata header/source when visible, method-layout calibration, counts, output path, elapsed time, and failure reason. `stage=complete` confirms a checked, published file.

If there are no target logs, check the exact installed package/process name, enabled Zygisk, module status, denylist/configuration, and whether the app actually launched. Headless AVDs can reject Monkey's physical-key defaults; use `am start -n PACKAGE/ACTIVITY` after resolving the launcher with `cmd package resolve-activity --brief PACKAGE`.

If initialization times out, include the stage and readiness diagnostic. ARM64 uses bounded instruction profiles to read runtime readiness globals without calling APIs while they are uninitialized. Unrecognized profiles fail explicitly. Other ABIs currently use corlib readiness plus a settling interval; this is a heuristic and is not equivalent to universal runtime support.

If required APIs cannot be resolved, dynamic and applicable file-symbol fallbacks are reported. Fully stripped/renamed APIs, erased unregistered ELF headers, inaccessible mappings, or custom runtime object layouts may remain unsupported. The module does not guess executable addresses or suppress process-wide faults.

## Tests and development

```sh
. .venv/bin/activate
cmake -S . -B build/host -G Ninja -DDUMPER_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/host
ctest --test-dir build/host --output-on-failure
python scripts/check_format.py
./gradlew :module:assembleDebug :module:assembleRelease :module:lint --warning-mode=fail
python scripts/verify_module.py out/zygisk-il2cppdumper-v1.4.1-release.zip \
  --readelf "$ANDROID_HOME/ndk/30.0.16248370/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf"
```

Use `darwin-x86_64` instead of `linux-x86_64` for the NDK tool directory on macOS (including Apple Silicon). Native tests run on POSIX hosts. Android builds also work independently of the host test project.

Build and run synthetic Android integration tests, without requiring games or root:

```sh
cmake -S . -B build/android -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_HOME/ndk/30.0.16248370/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-23 -DANDROID_STL=c++_static \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/android
python scripts/test_device.py --serial DEVICE --build-dir build/android
```

Use `x86_64` when building tests for an x86_64 emulator. These fixtures exercise the real resolver/dumper against a synthetic runtime, including shifted layouts, legacy reflection, file-only capability groups, map discovery, malformed counts, changing assembly arrays, missing APIs, timeout, and atomic output failures. Exact recovered method RVAs and concurrent fallback readers are checked. They do not claim to substitute for live Unity/Zygisk testing.

For a newly encountered variation, first establish the failing stage, preserve a private runtime baseline, and add a minimal synthetic regression. Prefer runtime APIs over copying private Unity structs. Keep new instruction/layout profiles bounded and centralized in `core/runtime_layout.*`; add negative and overflow tests. See [architecture and compatibility guidance](docs/ARCHITECTURE.md).

For a longer deterministic ELF stress run, execute `build/host/elf_tests --stress` (200,000 mutations). Linux CI also runs 32-bit host tests, including a forced `/proc/self/mem` read above 2 GiB.

GitHub Actions builds all four ABIs in Debug/Release, runs sanitizers, formatting, Android lint, archive checks, and Android 17 x86_64 synthetic runtime tests. Every action is pinned to a reviewed release commit. Proprietary application binaries, dumps, device records, and signing secrets must stay out of Git and CI artifacts.
