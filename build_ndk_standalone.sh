#!/bin/bash
# Standalone NDK build script for Zygisk-Il2CppDumper
# Uses system aarch64 clang + NDK sysroot/resource-dir
# All dependencies explicitly specified — no hacks, no fake libgcc.
set -e

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
CPP_DIR="$PROJECT_DIR/module/src/main/cpp"
OUT_DIR="$PROJECT_DIR/out/standalone_build"
NDK="${NDK:-$PROJECT_DIR/ndk-slim}"
SYSROOT="$NDK/sysroot"
RESDIR="$NDK/lib/clang/18"
NDK_LINK_API="${NDK_LINK_API:-35}"
CLANG="${CLANG:-clang-18}"
CLANGXX="${CLANGXX:-clang++-18}"
LLD="${LLD:-lld-18}"
LLVM_STRIP="${LLVM_STRIP:-llvm-strip-18}"

MODULE_NAME="il2cppdumper"

echo "╔══════════════════════════════════════════════╗"
echo "║  Zygisk-Il2CppDumper v1.4.0 Standalone Build║"
echo "╚══════════════════════════════════════════════╝"
echo ""
if [ ! -d "$SYSROOT" ] || [ ! -d "$RESDIR" ]; then
    echo "❌ Invalid NDK path: $NDK"
    exit 1
fi
for tool in "$CLANG" "$CLANGXX" "$LLD"; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "❌ Required build tool not found: $tool"
        exit 1
    fi
done
if ! command -v zip >/dev/null 2>&1; then
    echo "❌ Required packaging tool not found: zip"
    exit 1
fi
echo "NDK:     $NDK"
echo "Clang:   $($CLANGXX --version | head -1)"
echo "Target:  arm64-v8a + armeabi-v7a + x86_64 + x86 (API 23+)"
echo ""

# ─── Source files ──────────────────────────────────────────────────
CXX_SOURCES=(
    main.cpp
    hack.cpp
    il2cpp_dump.cpp
    registration_dump.cpp
    semantic_dump.cpp
)

C_SOURCES=(
    xdl/xdl.c
    xdl/xdl_iterate.c
    xdl/xdl_linker.c
    xdl/xdl_lzma.c
    xdl/xdl_util.c
)

# ─── Build for one ABI ────────────────────────────────────────────
build_abi() {
    local abi="$1"          # arm64-v8a or armeabi-v7a
    local target="$2"       # aarch64-linux-android23 or armv7a-linux-androideabi23
    local lib_arch="$3"     # aarch64-linux-android or arm-linux-androideabi
    local api_level="$4"    # 23

    echo ""
    echo "══════════════════════════════════════════════"
    echo "🔨 Building for $abi (target: $target)"
    echo "══════════════════════════════════════════════"

    local build_dir="$OUT_DIR/$abi"
    mkdir -p "$build_dir"

    local obj_files=()

    # Separate C and CXX flags (C doesn't need -stdlib)
    local CFLAGS_C="-O2 -fPIC -fvisibility=hidden -fvisibility-inlines-hidden"
    CFLAGS_C="$CFLAGS_C -fdata-sections -ffunction-sections"
    CFLAGS_C="$CFLAGS_C -Wall -Werror=format"
    CFLAGS_C="$CFLAGS_C -I$CPP_DIR -I$CPP_DIR/xdl/include"
    CFLAGS_C="$CFLAGS_C --target=$target"
    CFLAGS_C="$CFLAGS_C --sysroot=$SYSROOT"
    CFLAGS_C="$CFLAGS_C -resource-dir=$RESDIR"

    local CFLAGS="$CFLAGS_C -stdlib=libc++ -DMODULE_NAME=$MODULE_NAME"
    local CXXFLAGS="$CFLAGS -std=c++20 -fno-exceptions -fno-rtti"

    # ABI-specific flags
    if [ "$abi" = "arm64-v8a" ]; then
        CFLAGS_C="$CFLAGS_C -ffixed-x18"
        CXXFLAGS="$CXXFLAGS -ffixed-x18"
    fi

    # LDFLAGS — proper explicit deps, no libgcc, no hacks
    local LIBS="-lc -lm -ldl -llog -lc++_static -lc++abi"
    local RT_LIB="$RESDIR/lib/linux/libclang_rt.builtins-aarch64-android.a"
    case "$abi" in
        armeabi-v7a) RT_LIB="$RESDIR/lib/linux/libclang_rt.builtins-arm-android.a" ;;
        x86_64)      RT_LIB="$RESDIR/lib/linux/libclang_rt.builtins-x86_64-android.a" ;;
        x86)         RT_LIB="$RESDIR/lib/linux/libclang_rt.builtins-i686-android.a" ;;
    esac

    local platform_lib_dir="$SYSROOT/usr/lib/$lib_arch/$NDK_LINK_API"
    local LDFLAGS="-fuse-ld=$LLD -nodefaultlibs -nostartfiles"
    LDFLAGS="$LDFLAGS -Wl,--hash-style=both"
    LDFLAGS="$LDFLAGS -Wl,--exclude-libs,ALL -Wl,--gc-sections -Wl,--strip-all"
    LDFLAGS="$LDFLAGS -L$platform_lib_dir"
    LDFLAGS="$LDFLAGS -L$SYSROOT/usr/lib/$lib_arch"
    LDFLAGS="$LDFLAGS -shared"

    # ── Compile C++ sources ──
    for src in "${CXX_SOURCES[@]}"; do
        local obj="$build_dir/$(basename ${src%.cpp}).o"
        echo "  [CXX] $src"
        "$CLANGXX" $CXXFLAGS -c "$CPP_DIR/$src" -o "$obj"
        obj_files+=("$obj")
    done

    # ── Compile C sources ──
    for src in "${C_SOURCES[@]}"; do
        local obj="$build_dir/$(basename ${src%.c}).o"
        echo "  [CC]  $src"
        "$CLANG" $CFLAGS_C -c "$CPP_DIR/$src" -o "$obj"
        obj_files+=("$obj")
    done

    # ── Link ──
    local out_so="$build_dir/lib${MODULE_NAME}.so"
    echo "  [LD]  → lib${MODULE_NAME}.so"
    "$CLANGXX" $CXXFLAGS $LDFLAGS "$platform_lib_dir/crtbegin_so.o" \
        "${obj_files[@]}" $LIBS "$RT_LIB" "$platform_lib_dir/crtend_so.o" -o "$out_so"

    # Strip debug info further
    "$LLVM_STRIP" --strip-all "$out_so" 2>/dev/null || true

    echo "  ✅ Built: $(file "$out_so" | cut -d, -f1-2)"
    echo "     Size: $(stat -c%s "$out_so" | numfmt --to=iec)"
}

# ─── Package Magisk module ─────────────────────────────────────────
package_module() {
    echo ""
    echo "══════════════════════════════════════════════"
    echo "📦 Packaging Magisk module zip"
    echo "══════════════════════════════════════════════"

    local magisk_dir="$OUT_DIR/magisk_module"
    local template_dir="$PROJECT_DIR/template/magisk_module"
    local zip_name="zygisk-il2cppdumper-v1.4.0.zip"

    rm -rf "$magisk_dir"
    mkdir -p "$magisk_dir/zygisk"

    # Copy template files
    cp -r "$template_dir/META-INF" "$magisk_dir/"
    cp -r "$template_dir/webroot" "$magisk_dir/" 2>/dev/null || true

    # Generate module.prop
    cat > "$magisk_dir/module.prop" << EOF
id=zygisk_il2cppdumper
name=Il2CppDumper
version=v1.4.0
versionCode=3
author=Perfare (extended)
description=Runtime IL2CPP dumps for AI-assisted analysis. Configure targets in Web UI.
EOF

    # Copy .so files to zygisk/ (named by ABI)
    cp "$OUT_DIR/arm64-v8a/lib${MODULE_NAME}.so" "$magisk_dir/zygisk/arm64-v8a.so"
    cp "$OUT_DIR/armeabi-v7a/lib${MODULE_NAME}.so" "$magisk_dir/zygisk/armeabi-v7a.so"
    cp "$OUT_DIR/x86_64/lib${MODULE_NAME}.so" "$magisk_dir/zygisk/x86_64.so"
    cp "$OUT_DIR/x86/lib${MODULE_NAME}.so" "$magisk_dir/zygisk/x86.so"

    # Create zip
    cd "$magisk_dir"
    zip -qr "$OUT_DIR/$zip_name" .
    cd "$PROJECT_DIR"

    echo "  ✅ Module packaged: $OUT_DIR/$zip_name"
    echo "     Size: $(stat -c%s "$OUT_DIR/$zip_name" | numfmt --to=iec)"
    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "  Output files:"
    for abi in arm64-v8a armeabi-v7a x86_64 x86; do
        echo "    $abi:  $OUT_DIR/$abi/lib${MODULE_NAME}.so"
    done
    echo "    Magisk zip: $OUT_DIR/$zip_name"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
}

# ─── Main ──────────────────────────────────────────────────────────
rm -rf "$OUT_DIR"

build_abi "arm64-v8a"   "aarch64-linux-android23"        "aarch64-linux-android"  23
build_abi "armeabi-v7a" "armv7a-linux-androideabi23"    "arm-linux-androideabi"  23
build_abi "x86_64"      "x86_64-linux-android23"        "x86_64-linux-android"   23
build_abi "x86"         "i686-linux-android23"          "i686-linux-android"     23

package_module
