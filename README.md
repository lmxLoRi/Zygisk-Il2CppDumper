# Zygisk-Il2CppDumper

Runtime IL2CPP metadata dumping through Zygisk, designed primarily for AI-assisted program analysis.

[中文说明](README.zh-CN.md)

## Usage

1. Install Magisk 24 or later and enable Zygisk.
2. Build the generic module with GitHub Actions, Gradle, or `build_ndk_standalone.sh`.
3. Install the generated ZIP in Magisk.
4. Select target applications in the module Web UI and save the selection.
5. Start a target application.

The target application's `files` directory receives:

- `dump.cs`: C#-like types, fields, properties, and method declarations.
- `managed.json`: complete managed runtime method semantics, including methods without native code.
- `native.json`: native functions deduplicated by RVA and their many-to-many managed bindings.

The native index also performs range-checked runtime registration discovery for codegen modules and generic method pointers. Check `Capabilities` before consuming optional data; generic instance names, strings, and metadata slots remain staged work.

## Standalone Build

Place an NDK sysroot/resource subset at `ndk-slim`, or set `NDK` to its location:

```bash
bash build_ndk_standalone.sh
```

Override `NDK`, `CLANG`, `CLANGXX`, `LLD`, or `LLVM_STRIP` when needed.
