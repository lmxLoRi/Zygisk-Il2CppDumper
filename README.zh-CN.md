# Zygisk-Il2CppDumper

通过 Zygisk 在游戏运行时导出 IL2CPP 信息，可用于绕过静态 metadata 加密、保护和混淆。输出格式以 AI 检索和程序分析为主要目标。

## 使用方法

1. 安装 Magisk 24 或更高版本并启用 Zygisk。
2. 使用 GitHub Actions、Gradle 或 `build_ndk_standalone.sh` 构建通用模块。
3. 在 Magisk 中安装生成的 ZIP。
4. 打开模块 Web UI，选择一个或多个目标应用并保存。
5. 启动目标应用。

输出位于目标应用的 `files` 目录：

- `dump.cs`：接近 C# 声明的类型、字段、属性和方法视图。
- `managed.json`：完整的运行时托管方法语义，包括无原生实现的方法。
- `native.json`：按 RVA 去重的原生函数索引，以及与托管方法的多对多关系。

`native.json` 第一阶段只包含运行时可可靠观测的方法指针。registration metadata、泛型实例、字符串和 metadata slots 会在后续版本逐步加入，文件中的 `Capabilities` 会明确标记当前支持范围。

## 独立构建

将 NDK sysroot/resource 子集放在 `ndk-slim`，或通过 `NDK` 指定其位置：

```bash
bash build_ndk_standalone.sh
```

可通过 `NDK`、`CLANG`、`CLANGXX`、`LLD` 和 `LLVM_STRIP` 环境变量覆盖工具链。
