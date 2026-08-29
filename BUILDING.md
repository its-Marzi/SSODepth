# Building SSO Depth

This page is for anyone who wants to build or modify SSO Depth from source.

Normal users do **not** need to do this. Prebuilt versions are available on the [Releases](../../releases) page.

## Requirements

The current build setup is made for Linux and uses **MinGW-w64** to compile a 64-bit Windows DLL.

You need:

- Git
- Bash
- MinGW-w64 with the C++ compiler

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

The build script will download the required ReShade 6.8.0 headers automatically if they are not already present.

If everything succeeds, the finished add-on will be created at:

```text
build/SSODepth.addon64
```

The main source file is:

```text
src/addon.cpp
```

## Testing a build

Copy `SSODepth.addon64` into the folder containing `SSOClient.exe`, replacing the previous build if necessary.

Launch Star Stable and check that **SSO Depth** appears in ReShade's **Add-ons** tab.

`DisplayDepth` is the easiest way to confirm that the add-on is supplying the correct depth buffer.

You can also check `ReShade.log` for a line similar to:

```text
[SSO Depth] Active depth bridge: 1920x1080, FBO ..., OpenGL texture ...
```

The framebuffer and texture numbers are expected to change between launches.