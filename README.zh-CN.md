# Zygisk-Il2CppDumper

[English](README.md) | **简体中文**

从 IL2CPP 应用在运行时导出 il2cpp 数据，可绕过保护、加密与混淆。**需要已 Root 的 Android 设备，并安装 Magisk 且启用 Zygisk。**

## 使用步骤

1. **Fork 本仓库**，然后在你的 Fork 中打开 **Actions → Build and test → Run workflow**。如果 GitHub 提示，请先启用 workflows。
2. 输入应用的**包名**（例如 `com.example.game`）。有需要时可勾选 **Unmount module files in target apps**（卸载目标应用中的模块挂载），否则保持未勾选，然后运行工作流。
3. 打开成功的运行记录，从 artifacts 中下载 **`zygisk.zip`**。
4. 在 **Magisk → 模块 → 从本地安装** 中选择 `zygisk.zip`，然后**重启设备**。
5. **打开目标应用**，等待导出完成。
6. 使用有 **Root 权限的文件管理器**，从以下位置复制 `dump.cs`：

   ```text
   /data/user/0/PACKAGE/files/dump.cs
   ```

   将 `PACKAGE` 替换为第 2 步中输入的包名。

已有模块 ZIP？请直接从第 4 步开始。部分加固应用仍不受支持，详见[测试结果与已知限制](docs/FINAL_REVIEW.md)。[报告问题](https://github.com/Jordan231111/Zygisk-Il2CppDumper/issues)。

---

<details>
<summary>本地构建（不使用 GitHub Actions）</summary>

需要 JDK 25 LTS（已用 Temurin 25.0.4.1 测试）、Python 3.11+ 和 Android SDK 工具。JDK 21 也可以构建本项目。Gradle 由带校验和验证的 wrapper 提供。

```sh
git clone --branch master --recurse-submodules https://github.com/Jordan231111/Zygisk-Il2CppDumper.git
cd Zygisk-Il2CppDumper

android sdk install platforms/android-37.0
android sdk install build-tools/37.0.0
android sdk install ndk/30.0.16248370

python3 -m venv .venv
. .venv/bin/activate
python -m pip install -r requirements-dev.txt
# 如有需要，请将 ANDROID_HOME 指向你的 SDK 目录，将 JAVA_HOME 指向你的 JDK。
./gradlew :module:assembleRelease -PtargetPackage=com.example.authorizedapp
```

固定版本构建使用 AGP 9.4.0、Gradle 9.7.1、NDK r30、CMake 4.4.3 和 Ninja 1.13.2。请激活虚拟环境，以便 AGP 能在 `PATH` 中找到固定版本的 CMake/Ninja。Windows 用户可以激活 `.venv\Scripts\activate` 并调用 `gradlew.bat`。

输出为 `out/zygisk-il2cppdumper-v1.4.3-release.zip`，`:module:assembleDebug` 则对应 Debug 版 ZIP。这些都是 Magisk 模块，不是 APK。Release 和 Debug 都会隔离各自的 C++ 符号并静态链接 C++ 运行时。Debug 在 AGP 打包剥离步骤之前保留原生调试信息；未剥离的二进制文件位于 `module/build/intermediates/cxx/` 下。

如需启用与 CI 复选框相同的可选卸载设置，请在构建命令中添加 `-PunmountModules=true`。默认值为 `false`。

Release 构建还会生成 `out/zygisk.zip`，它是带版本号的 Release 模块的字节一致副本。两种支持的构建方式使用同一个 Gradle 打包任务：

- **本地：**运行上面的 Release 命令，然后安装 `out/zygisk.zip`。
- **GitHub Actions：**按本页顶部的步骤操作。手动运行默认构建 Release ZIP。普通下载保持 **full_validation** 未勾选即可。每次成功运行只发布 `zygisk.zip`；诊断详情保留在工作流日志中。

该库没有 Java/Kotlin/AndroidX 运行时依赖。编译 SDK 为 37；原生 Zygisk 模块会继承目标应用的 Android 行为，无法改变应用自身的 target SDK。

</details>

<details>
<summary>多应用或更换目标（可选 targets.txt）</summary>

**可选：**一个已安装的模块可以为多个应用导出 dump。你也可以在不重新构建、不重装模块的情况下更换目标。

没有目标文件时，模块沿用之前的方式：使用 GitHub Actions 中输入的单个包名，或 `-PtargetPackage` 传入的包名。若要覆盖该默认值，请创建 `/data/adb/modules/zygisk_il2cppdumper/targets.txt`，**每行填写一个包名**，例如：

```text
com.example.gameone
com.example.gametwo
```

打开其中任意一个应用即可导出。每个应用都会执行各自的导出，并写入各自的 `/data/user/0/PACKAGE/files/dump.cs`；模块不会替你启动应用。该文件会替换构建时的默认选择，而不是追加。删除 `targets.txt` 即可恢复使用 CI 中输入的包名；空文件表示有意不选择任何应用。

辅助脚本会创建或替换该列表，并设置正确的权限和 SELinux 标签。将 `DEVICE` 替换为你的 ADB 设备 ID，并将示例包名替换为你的应用：

```sh
python scripts/set_targets.py --serial DEVICE com.example.gameone com.example.gametwo
```

修改列表后，强制停止并重新启动所选应用即可。仅改目标列表不需要重启。模块升级会保留该列表，因此它会继续覆盖新安装 ZIP 中的包名。

条目精确匹配进程名。`com.example.gameone:worker` 只选择一个子进程；`com.example.gameone:*` 包含该包的主进程和子进程。它不会匹配 `com.example.gameone2`。子进程使用各自独立的输出文件名。空行和注释行会被忽略。请使用 UTF-8 文本，并保持文件不超过 4 KB。

直接 `adb push` 到模块目录可能会留下 Zygisk 无法读取的 `adb_data_file` 标签。辅助脚本会从 `module.prop` 复制标签；它不会修改 SELinux 策略。

</details>

<details>
<summary>高级安装与模块挂载选项</summary>

在明确选定的设备上通过 ADB 安装：

```sh
adb -s DEVICE push out/zygisk-il2cppdumper-v1.4.3-release.zip /data/local/tmp/il2cppdumper.zip
adb -s DEVICE shell su -c 'magisk --install-module /data/local/tmp/il2cppdumper.zip'
adb -s DEVICE reboot
```

CI 中的 **Unmount module files in target apps** 复选框（或本地的 `-PunmountModules=true`）会在安装 ZIP 时设置该选项。**每次新安装都以该 ZIP 的选择为准：**安装未勾选的构建会关闭此选项，即使之前已启用。Magisk 安装日志和模块说明会显示构建时的选择。该选项作用于所有选定的目标，包括 `targets.txt` 覆盖列表中的应用。

如需在安装后修改，可使用下面的辅助脚本。该命令也会用传入的包名替换目标列表；修改后请强制停止并重新启动这些应用：

```sh
python scripts/set_targets.py --serial DEVICE --unmount on com.example.authorizedapp
```

该可选项会在配置和桥接载荷复制完成后，调用公开的 `FORCE_DENYLIST_UNMOUNT` API。它会影响所选应用进程中的模块挂载，并可能干扰其他需要这些挂载的模块。默认关闭；用 `--unmount off` 可关闭。它不保证能通过 root、模拟器、Zygisk 或调试检测。常规导出不附加调试器、不修改应用代码；只有当主要内核读取路径失败时才会打开 `/proc/self/mem`。非目标进程会卸载本模块。

更新模块二进制文件需要重启。切勿在依赖 native-bridge 转译的模块中只替换其中一个 ABI。

</details>

<details>
<summary>没有生成 dump？排错与日志</summary>

默认输出为 `<app_data_dir>/files/dump.cs`，通常是 `/data/user/0/PACKAGE/files/dump.cs`。子进程使用 `dump-PROCESS_SUFFIX.cs`。分身/工作资料目录由 Zygisk 提供，不是硬编码的。

一次完整的导出会原子替换上一次的文件。初始化、校验或 I/O 失败时会保留上一次的 dump。进程被杀死可能会留下隐藏的 `.dump.cs.PID.N.tmp` 文件；它不是已完成的 dump。

```sh
adb -s DEVICE logcat -v threadtime 'Il2CppDumper:V' 'Unity:I' 'CRASH:E' '*:S'
python scripts/pull_dump.py --serial DEVICE \
  --source /data/user/0/PACKAGE/files/dump.cs --output dump.cs
```

收集辅助脚本会校验 SHA-256，传输失败时会保留已有的本地输出；它会避免在 root shell 中发生换行符转换。

创建 `/data/adb/modules/zygisk_il2cppdumper/verbose` 可在下次启动时记录每个镜像的进度。删除它即恢复普通日志。日志包含进程、ABI、API 等级、页大小、ELF 加载基址、解析策略、就绪阶段、可见时的元数据头/来源、方法布局校准、计数、输出路径、耗时和失败原因。`stage=complete` 才表示文件已经校验并发布。

如果完全没有目标日志，请检查实际安装的包名/进程名、Zygisk 是否启用、模块状态、denylist/配置，以及应用是否真正启动过。无头 AVD 可能拒绝 Monkey 默认的物理按键；请先用 `cmd package resolve-activity --brief PACKAGE` 查出启动 Activity，再用 `am start -n PACKAGE/ACTIVITY` 启动。

如果初始化超时，请附上阶段和就绪诊断信息。ARM64 使用带边界的指令模式来读取运行时就绪变量，不会在其未初始化时调用 API；无法识别的模式会明确失败。其他 ABI 目前使用 corlib 就绪加一段等待时间的启发式策略，这不等同于通用运行时支持。

如果必需 API 无法解析，动态查找和适用的文件符号回退都会上报。完全剥离/改名的 API、被擦除的未注册 ELF 头、无法访问的映射或自定义运行时对象布局仍可能不受支持。模块不会猜测可执行地址，也不会压制进程级错误。

</details>

<details>
<summary>运行环境与支持的设备</summary>

- 原生 ABI 层面要求 Android 6/API 23 或更高。实际运行时覆盖范围见工程报告；仅编译通过不代表兼容性声明。
- 需要 Magisk 24+ 并启用 Zygisk，或兼容的 Zygisk 实现。出于旧版 Magisk 兼容考虑，有意保留 API 2。已测试的管理器版本见报告。
- 会构建 `arm64-v8a`、`armeabi-v7a`、`x86` 和 `x86_64` 四种模块载荷。Native-bridge 转译仅为尽力支持，且有额外的厂商限制。
- 构建和地址计算支持 4 KB 和 16 KB 两种原生页大小。

</details>

<details>
<summary>开发者测试与兼容性工作</summary>

```sh
. .venv/bin/activate
cmake -S . -B build/host -G Ninja -DDUMPER_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/host
ctest --test-dir build/host --output-on-failure
python scripts/check_format.py
./gradlew :module:assembleDebug :module:assembleRelease :module:lint --warning-mode=fail
python scripts/verify_module.py out/zygisk-il2cppdumper-v1.4.3-release.zip \
  --readelf "$ANDROID_HOME/ndk/30.0.16248370/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf"
```

macOS（含 Apple Silicon）请把 NDK 工具目录中的 `linux-x86_64` 换成 `darwin-x86_64`。原生测试在 POSIX 主机上运行。Android 构建不依赖主机测试工程，也可独立工作。

构建并运行合成 Android 集成测试，无需游戏或 root：

```sh
cmake -S . -B build/android -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_HOME/ndk/30.0.16248370/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-23 -DANDROID_STL=c++_static \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/android
python scripts/test_device.py --serial DEVICE --build-dir build/android
```

为 x86_64 模拟器构建测试时请使用 `x86_64`。这些用例用合成运行时检验真实的解析器/导出器，包括偏移布局、旧版反射、纯文件能力组、映射发现、异常计数、变化的程序集数组、缺失 API、超时和原子输出失败。会校验精确恢复的方法 RVA 和并发回退读取器。它们不能替代真机 Unity/Zygisk 测试。

遇到新的变种时，请先确定失败阶段，保留私有的运行时基线，并补充最小合成回归用例。优先使用运行时 API，而不是复制私有的 Unity 结构体。新增指令/布局模式要带边界并集中放在 `core/runtime_layout.*` 中，同时补充负向和溢出测试。详见[架构与兼容性说明](docs/ARCHITECTURE.md)。

如需更长时间的确定性 ELF 压力测试，可执行 `build/host/elf_tests --stress`（200,000 次变异）。Linux CI 还会运行 32 位主机测试，包括在 2 GiB 以上强制走 `/proc/self/mem` 读取。

推送和 Pull Request 会运行完整验证：四个 ABI 的 Debug/Release、sanitizer、格式检查、Android lint、归档检查和 Android 17 x86_64 合成运行时测试。手动运行默认构建 Release 并校验可安装 ZIP；维护者可勾选 **full_validation** 运行完整套件。每个 Action 都固定到已审核的发布提交。专有应用二进制文件、dump、设备记录和签名密钥不得进入 Git 和 CI 产物。

</details>
