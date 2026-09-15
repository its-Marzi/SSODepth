#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RESHADE_DIR="$ROOT/deps/reshade-5.8"
BUILD_DIR="$ROOT/build"

echo "==> Building SSODepth for ReShade 5.8.0"

if ! command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1; then
    echo
    echo "Error: x86_64-w64-mingw32-g++ was not found."
    exit 1
fi

if [ ! -d "$RESHADE_DIR/.git" ]; then
    echo "==> Fetching ReShade 5.8.0"

    mkdir -p "$ROOT/deps"

    git clone \
        --depth 1 \
        --branch v5.8.0 \
        https://github.com/crosire/reshade.git \
        "$RESHADE_DIR"
fi

if [ ! -f "$RESHADE_DIR/deps/imgui/imgui.h" ]; then
    echo "==> Fetching Dear ImGui headers"

    git -C "$RESHADE_DIR" submodule update \
        --init \
        --depth 1 \
        deps/imgui
fi

# MinGW/Linux compatibility.
if grep -q '#include <Windows.h>' \
    "$RESHADE_DIR/include/reshade.hpp"; then

    echo "==> Applying MinGW/Linux header compatibility patch"

    sed -i \
        's/#include <Windows.h>/#include <windows.h>/' \
        "$RESHADE_DIR/include/reshade.hpp"
fi

# ReShade 5.8's header casts function pointers to void * with
# static_cast, which MSVC accepts but MinGW rejects. reinterpret_cast
# is accepted by MinGW and produces the intended callback pointer.
if grep -Fq 'static_cast<void *>(callback)' \
    "$RESHADE_DIR/include/reshade.hpp"; then

    echo "==> Applying ReShade 5.8 MinGW callback compatibility patch"

    sed -i \
        's/static_cast<void \*>(callback)/reinterpret_cast<void *>(callback)/g' \
        "$RESHADE_DIR/include/reshade.hpp"
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
    -I"$RESHADE_DIR/include" \
    -I"$RESHADE_DIR/deps/imgui" \
    -I"$RESHADE_DIR/examples/13-effects_during_frame" \
    -include "$ROOT/src/reshade_mingw_compat.hpp" \
    -static-libgcc \
    -static-libstdc++ \
    -o "$BUILD_DIR/SSODepth-ReShade58.addon64" \
    "$ROOT/src/addon.cpp" \
    "$RESHADE_DIR/examples/13-effects_during_frame/state_tracking.cpp" \
    -lopengl32

echo
echo "==> Checking output"

file "$BUILD_DIR/SSODepth-ReShade58.addon64"

if x86_64-w64-mingw32-objdump -p \
    "$BUILD_DIR/SSODepth-ReShade58.addon64" \
    | grep -q 'libwinpthread-1.dll'; then

    echo
    echo "Error: build unexpectedly depends on libwinpthread-1.dll."
    exit 1
fi

echo
echo "Build successful:"
echo "  $BUILD_DIR/SSODepth-ReShade58.addon64"
