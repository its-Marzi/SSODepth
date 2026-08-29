# Building SSO Depth

This page is for anyone who wants to build or modify SSO Depth from source.

Normal users do **not** need to do this. Prebuilt versions are available on the [Releases](../../releases) page.

## Requirements

The current build setup is made for Linux and uses **MinGW-w64** to compile a 64-bit Windows DLL.

You need:

- Git
- MinGW-w64 with the C++ compiler
- Bash

### Arch Linux

```bash
sudo pacman -S --needed git mingw-w64-gcc
```

Other Linux distributions should work as long as they provide an equivalent `x86_64-w64-mingw32-g++` compiler.

## Building

Clone the repository:

```bash
git clone https://github.com/its-Marzi/SSODepth.git
cd SSODepth
```

Then run:

```bash
./build.sh
```

If everything succeeds, the finished add-on will be created at:

```text
build/SSODepth.addon64
```

## Testing a build

Copy `SSODepth.addon64` into the folder containing `SSOClient.exe`, replacing the previous test build if necessary.

Launch Star Stable and check that **SSO Depth** appears in ReShade's **Add-ons** tab.

`DisplayDepth` is the easiest way to confirm that the add-on is supplying the correct depth buffer.

You can also check `ReShade.log` for a line similar to:

```text
[SSO Depth] Active depth bridge: 1920x1080, FBO ..., OpenGL texture ...
```

The framebuffer and texture numbers are expected to change between launches.

## Project structure

```text
SSODepth/
├── src/
│   └── addon.cpp
├── build/
│   └── SSODepth.addon64
├── deps/
│   └── reshade/
├── build.sh
├── README.md
└── BUILDING.md
```

`src/addon.cpp` contains the add-on itself.

`build.sh` handles the Linux/MinGW build process.

`deps/reshade` contains the ReShade headers downloaded by the build script and is not tracked by Git.

`build` contains compiled output and is also not tracked by Git.

## Manual compiler command

`build.sh` currently builds the add-on with:

```bash
x86_64-w64-mingw32-g++ \
    -std=c++17 \
    -O2 \
    -shared \
    -static \
    -DWIN32_LEAN_AND_MEAN \
    -DNOMINMAX \
    -I"$PWD/deps/reshade/include" \
    -static-libgcc \
    -static-libstdc++ \
    -o "$PWD/build/SSODepth.addon64" \
    "$PWD/src/addon.cpp" \
    -lopengl32
```

Using `./build.sh` is recommended instead, since it also handles the ReShade dependency and checks the resulting binary.