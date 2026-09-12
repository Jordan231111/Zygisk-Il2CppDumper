# Third-party components

| Component | Source and pinned version | License / local changes |
|---|---|---|
| xDL | [v2.4.0, 6ab03d9](https://github.com/hexhacking/xDL/tree/6ab03d9d8976b5aa2d264f435ea3a4c4c5b3e67c) | MIT; source copied without implementation patches; project-owned static-library CMake build. See `module/src/main/cpp/xdl/UPSTREAM.md`. |
| Zygisk public header | [API 2](https://github.com/Jordan231111/Zygisk-Il2CppDumper/blob/aa83f73bdf95e0674541a8ca1824e23eb8f006a7/module/src/main/cpp/zygisk.hpp) | 0BSD public API. Retained for Magisk 24+ loading compatibility; only declaration attribute placement was corrected for current Clang. The current upstream API is 5 and would raise the minimum manager version without adding a needed capability. |
| Android NativeBridge callback declarations | [AOSP ART public interface](https://android.googlesource.com/platform/art/+/refs/heads/main/libnativebridge/include/nativebridge/native_bridge.h) | Apache-2.0; the adapter uses a checked prefix through interface version 7. No implementation code or native-bridge binary is bundled. |
| libc++ / libc++abi / LLVM runtime support | Android NDK r30, `30.0.16248370` | Distributed under the LLVM licenses and exceptions. Linked statically with hidden exports. See the bundled LLVM license and the NDK's upstream notices for runtime sources. |

The project retains its original MIT license and copyright notice. Module ZIPs include the project, xDL and LLVM license texts under `licenses/`. Build tools, Android SDK system libraries and root managers are external prerequisites; they are not distributed as project source or module payloads.

The previous unused AndroidX, Jetifier, Prefab and Maven-local settings were removed. Built-in Kotlin is disabled in this native-only Android library, avoiding an otherwise implicit Kotlin standard-library/annotations runtime dependency. No Rust toolchain, additional C++ runtime shared library, game binaries, or git submodule is introduced.
