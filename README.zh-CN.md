# Zygisk-Il2CppDumper

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

生成的模块位于 `out/zygisk-il2cppdumper-v1.4.1-release.zip`。在 Magisk 中安装、开启 Zygisk、重启后启动目标应用。Zygisk API 2 保留 Magisk 24+ 的接口兼容性；实际测试版本见工程报告。

也可以在 `/data/adb/modules/zygisk_il2cppdumper/targets.txt` 中逐行填写目标进程名。默认精确匹配；`com.example.authorizedapp:*` 明确包含该应用的子进程。此文件覆盖构建时的默认包名，升级时保留。修改目标后，强制停止并重新启动应用。

## 输出与排错

输出为 `<app_data_dir>/files/dump.cs`，通常是 `/data/user/0/包名/files/dump.cs`。子进程使用独立文件名。成功后原子替换旧文件；检测或写入失败时保留旧结果。

```sh
adb -s DEVICE logcat -v threadtime 'Il2CppDumper:V' 'Unity:I' 'CRASH:E' '*:S'
```

创建模块目录下的 `verbose` 文件可开启按程序集显示的详细日志。只有 `stage=complete` 才表示输出已成功写入。若没有目标日志，请检查包名/进程名、Zygisk、模块状态、排除列表，以及应用是否真正启动。

ARM64 使用经过边界检查的初始化指令模式，不识别的模式会明确失败。完全移除或改名的导出 API、特殊运行时布局及部分 NativeBridge 实现仍可能不支持。项目不再依赖固定的 `Il2CppType` 位域或固定长度的托管数组布局。

请勿将游戏二进制、应用资源、私有转储或设备资料提交到仓库。
