# Zygisk-Il2CppDumper

从 IL2CPP 应用生成 `dump.cs`。**需要已 Root 的 Android 设备，并安装 Magisk、启用 Zygisk。**

## 使用步骤

1. **Fork 本仓库**，在自己的仓库打开 **Actions → Build and test → Run workflow**。
2. 输入应用的**包名**（例如 `com.example.game`），运行工作流。
3. 构建成功后，在运行结果的 artifacts 中下载 **`zygisk.zip`**，保持 ZIP 格式。
4. 在 **Magisk → 模块 → 从本地安装**中选择该 ZIP，然后**重启设备**。
5. **启动目标应用**，等待导出完成。
6. 使用有 Root 权限的文件管理器，从以下位置复制 `dump.cs`：

   ```text
   /data/user/0/应用包名/files/dump.cs
   ```

已有模块 ZIP 的用户可以从第 4 步开始。部分保护版本仍不受支持，详见[测试结果与已知限制](docs/FINAL_REVIEW.md)。

<details>
<summary>详细说明：本地构建、设置、排障与开发</summary>

手动运行工作流默认只构建 Release 并验证模块 ZIP。普通下载保持 **full_validation** 未勾选即可；代码推送和 Pull Request 会自动运行完整测试，维护者也可手动勾选该选项。

本项目通过 Zygisk 在已授权的 Android 应用进程中调用 IL2CPP 运行时 API，生成类型、字段、属性、方法及地址信息的 `dump.cs`。不保证支持所有加壳或自定义 IL2CPP 运行时。

完整的工具链、实际测试范围与限制请参阅 [英文说明](README.md)、[工程报告](docs/ENGINEERING_REPORT.md) 和 [架构说明](docs/ARCHITECTURE.md)。编译通过不代表已经验证运行兼容性。

## 构建

需要 JDK 25 LTS、Python 3.11+、Android SDK 37、Build Tools 37.0.0 和 NDK 30.0.16248370。Gradle 由仓库提供；CMake/Ninja 使用固定版本。

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -r requirements-dev.txt
./gradlew :module:assembleRelease -PtargetPackage=com.example.authorizedapp
```

生成的模块位于 `out/zygisk-il2cppdumper-v1.4.2-release.zip`。在 Magisk 中安装、开启 Zygisk、重启后启动目标应用。Zygisk API 2 保留 Magisk 24+ 的接口兼容性；实际测试版本见工程报告。

## 多个应用或更换目标（可选）

没有 `targets.txt` 时，模块仍使用 GitHub Actions 中输入的单个包名。若想让同一个模块用于多个应用，可在 `/data/adb/modules/zygisk_il2cppdumper/targets.txt` 中每行填写一个包名：

```text
com.example.gameone
com.example.gametwo
```

启动列表中的任意应用时，模块会在该应用自己的 `files/dump.cs` 中生成输出；模块不会自动启动应用。也可以使用辅助脚本创建或替换列表，并设置正确的权限和 SELinux 标签：

```sh
python scripts/set_targets.py --serial DEVICE com.example.gameone com.example.gametwo
```

此文件覆盖构建时的默认包名，升级模块时保留。删除文件可恢复使用 CI 中输入的包名；空文件表示不选择任何应用。修改列表后，强制停止并重新启动应用即可，无需重新构建、安装或重启设备。

默认精确匹配进程名；`com.example.gameone:*` 明确包含该应用的主进程和子进程。使用 UTF-8 文本，文件大小不超过 4 KB；空行和注释行会被忽略。

## 输出与排错

输出为 `<app_data_dir>/files/dump.cs`，通常是 `/data/user/0/包名/files/dump.cs`。子进程使用独立文件名。成功后原子替换旧文件；检测或写入失败时保留旧结果。

```sh
adb -s DEVICE logcat -v threadtime 'Il2CppDumper:V' 'Unity:I' 'CRASH:E' '*:S'
```

创建模块目录下的 `verbose` 文件可开启按程序集显示的详细日志。只有 `stage=complete` 才表示输出已成功写入。若没有目标日志，请检查包名/进程名、Zygisk、模块状态、排除列表，以及应用是否真正启动。

ARM64 使用经过边界检查的初始化指令模式，不识别的模式会明确失败。完全移除或改名的导出 API、特殊运行时布局及部分 NativeBridge 实现仍可能不支持。项目不再依赖固定的 `Il2CppType` 位域或固定长度的托管数组布局。

请勿将游戏二进制、应用资源、私有转储或设备资料提交到仓库。

</details>
