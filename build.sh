#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RESHade_DIR="$ROOT/deps/reshade"
BUILD_DIR="$ROOT/build"

echo "==> Building SSODepth"

if ! command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1; then
    echo
    echo "Error: x86_64-w64-mingw32-g++ was not found."
    echo "On Arch Linux, install it with:"
    echo
    echo "    sudo pacman -S mingw-w64-gcc"
    echo
    exit 1
fi

if [ ! -d "$RESHade_DIR/.git" ]; then
    echo "==> Fetching ReShade 6.8.0"
    mkdir -p "$ROOT/deps"

    git clone \
        --depth 1 \
        --branch v6.8.0 \
        https://github.com/crosire/reshade.git \
        "$RESHade_DIR"
fi

# MinGW on Linux needs lowercase windows.h
if grep -q '#include <Windows.h>' \
    "$RESHade_DIR/include/reshade.hpp"; then

    echo "==> Applying MinGW/Linux header compatibility patch"

    sed -i \
        's/#include <Windows.h>/#include <windows.h>/' \
        "$RESHade_DIR/include/reshade.hpp"
fi

mkdir -p "$BUILD_DIR"

echo "==> Compiling"

x86_64-w64-mingw32-g++ \
    -std=c++17 \
    -O2 \
    -shared \
    -static \
    -DWIN32_LEAN_AND_MEAN \
    -DNOMINMAX \
    -I"$RESHade_DIR/include" \
    -static-libgcc \
    -static-libstdc++ \
    -o "$BUILD_DIR/SSODepth.addon64" \
    "$ROOT/src/addon.cpp" \
    -lopengl32

echo
echo "==> Checking output"

file "$BUILD_DIR/SSODepth.addon64"

if x86_64-w64-mingw32-objdump -p \
    "$BUILD_DIR/SSODepth.addon64" \
    | grep -q 'libwinpthread-1.dll'; then

    echo
    echo "Error: build unexpectedly depends on libwinpthread-1.dll."
    exit 1
fi

echo
echo "Build successful:"
echo "  $BUILD_DIR/SSODepth.addon64"
