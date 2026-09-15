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

For the normal ReShade 6.8.0 build, run:

```bash
./build.sh
```

This downloads the required ReShade 6.8.0 headers automatically and produces:

```text
build/SSODepth.addon64
```

For the legacy ReShade 5.8.0 compatibility build, run:

```bash
./build-reshade58.sh
```

This uses a separate ReShade 5.8.0 SDK checkout and produces:

```text
build/SSODepth-ReShade58.addon64
```

The two builds target different ReShade add-on API versions, so use the binary that matches the installed ReShade version.

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