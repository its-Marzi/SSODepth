# SSO Depth

SSO Depth is a small ReShade add-on that fixes depth-based ReShade effects in **Star Stable Online** when ReShade fails to detect the game's depth buffer correctly.

If effects such as depth of field, ambient occlusion, fog, or `DisplayDepth` do not work correctly, this add-on may fix them.

It is designed to work automatically once installed. There are no settings you need to configure.

> [!IMPORTANT]
> SSO Depth is currently **tested and confirmed working on Linux through Wine**.
>
> Star Stable also uses OpenGL on Windows, so the add-on may help Windows players who experience broken ReShade depth detection as well. **Native Windows support is currently experimental and needs more testing.**

---

## What does it fix?

Some ReShade effects need access to the game's **depth buffer**. The depth buffer tells an effect which objects are close to the camera and which are far away.

In SSO on Linux, newer versions of ReShade may fail to detect the correct depth buffer. This can cause effects to:

- blur the entire screen,
- show a black or blank depth map,
- place fog incorrectly,
- make ambient occlusion appear incorrectly,
- or simply do nothing.

SSO Depth finds SSO's real depth texture and gives it directly to ReShade.

It has been tested successfully with:

- `DisplayDepth`
- qUINT ADOF
- qUINT MXAO
- DepthHaze

Other effects that use ReShade's normal `DEPTH` texture should work as well.

---

# Installation

## Before you start

You need:

- Star Stable Online on Linux through Wine, or native Windows for experimental testing
- ReShade **6.8.0**
- the **full add-on version** of ReShade
- ReShade already working inside SSO

If you can open the ReShade menu inside Star Stable, you are most of the way there.

If ReShade itself is not installed or does not open in SSO yet, SSO Depth will not work by itself.

---

## 1. Download SSO Depth

Go to the **Releases** page of this repository and download:

`SSODepth.addon64`

You only need this one file.

---

## 2. Find your Star Stable client folder

You need the folder containing:

`SSOClient.exe`

The correct folder normally also contains your ReShade files, such as:

- `opengl32.dll`
- `ReShade.ini`
- `ReShadePreset.ini`

If you use Bottles, your path may look similar to:

```text
~/.var/app/com.usebottles.bottles/data/bottles/bottles/YOUR-BOTTLE/drive_c/Program Files/Star Stable Online/client/
```

The exact bottle name may be different on your computer.

---

## 3. Put the add-on next to SSOClient.exe

Copy:

`SSODepth.addon64`

into the same folder as:

`SSOClient.exe`

For example:

```text
Star Stable Online/
└── client/
    ├── SSOClient.exe
    ├── opengl32.dll
    ├── ReShade.ini
    └── SSODepth.addon64
```

That is all you need to install SSO Depth.

---

## 4. Start Star Stable

Launch the game normally.

Open the ReShade menu.

If everything loaded correctly, **SSO Depth** should appear in ReShade's **Add-ons** tab.

You do not need to select a depth buffer or change any SSO Depth settings.

---

# Checking if it works

The easiest test is ReShade's `DisplayDepth` effect.

Enable `DisplayDepth`.

A working depth map should show recognizable parts of the game world.

Objects closer to the camera should appear at a different brightness than objects farther away.

If you can see the shapes of things such as:

- your character,
- your horse,
- buildings,
- trees,
- fences,
- and the ground,

then SSO Depth is working.

You can then disable `DisplayDepth` and use your normal preset.

---

# Depth effects

Once SSO Depth is working, effects such as these should be able to use SSO's depth correctly.

### Depth of field

Examples:

- qUINT ADOF
- other ReShade DOF shaders

These can blur the background or foreground based on distance from the camera.

### Ambient occlusion

Examples:

- qUINT MXAO

These add subtle shadows around places where objects meet, such as around a horse's legs or near walls and fences.

### Depth fog and haze

Examples:

- DepthHaze
- AdaptiveFog

These can add mist or fog that becomes stronger farther away from the camera.

---

# Troubleshooting

## SSO Depth does not appear in the Add-ons tab

Make sure:

1. `SSODepth.addon64` is in the same folder as `SSOClient.exe`.
2. You are using 64-bit ReShade.
3. You installed the **full add-on version** of ReShade.
4. You are using ReShade 6.8.0.

A normal or restricted ReShade build may refuse to load external add-ons.

---

## ReShade itself does not open

SSO Depth cannot fix a broken ReShade installation.

First make sure ReShade itself loads correctly in Star Stable.

SSO currently uses OpenGL, so ReShade should normally be installed as:

`opengl32.dll`

next to `SSOClient.exe`.

---

## DisplayDepth is still black

First check the ReShade **Add-ons** tab and make sure SSO Depth is loaded.

You can also look inside:

`ReShade.log`

in the SSO client folder.

A working installation should contain a message similar to:

```text
[SSO Depth] Active depth bridge: 1920x1080, FBO ..., OpenGL texture ...
```

The exact numbers will be different between computers and even between game launches. That is normal.

---

## My depth appears reversed or upside down

SSO Depth provides the depth texture itself, but ReShade shaders can still apply their normal depth preprocessing settings.

The following settings worked during testing:

```text
RESHADE_DEPTH_INPUT_IS_UPSIDE_DOWN=1
RESHADE_DEPTH_INPUT_IS_REVERSED=0
RESHADE_DEPTH_INPUT_IS_LOGARITHMIC=0
```

If your depth map looks incorrect, check these under ReShade's global preprocessor definitions.

---

## The add-on stopped working after changing resolution

Restart the game first.

SSO Depth automatically detects the game's current full-resolution scene depth buffer, including when SSO recreates its OpenGL resources.

If the problem persists, please open an issue and include your `ReShade.log`.

---

# Reporting a problem

If SSO Depth does not work for you, please open an issue on GitHub.

If possible, include:

- your Linux distribution,
- how you run SSO, such as Windows, Bottles, Lutris, or plain Wine,
- your Wine runner/version,
- your ReShade version,
- your GPU,
- what the problem looks like,
- and your `ReShade.log`.

Please do **not** include passwords, login information, or other private information.

---

# Building from source

You do not need to build SSO Depth yourself just to use it.

This section is only for developers or people who want to modify the add-on.

The included build script currently targets Linux using MinGW-w64.

On Arch Linux, install the compiler with:

```bash
sudo pacman -S --needed mingw-w64-gcc git
```

Then run:

```bash
git clone https://github.com/its-Marzi/SSODepth.git
cd SSODepth
./build.sh
```

The build script will automatically download the required ReShade 6.8.0 headers if necessary.

The finished add-on will be created at:

```text
build/SSODepth.addon64
```

---

# How does it work?

Star Stable Online's OpenGL renderer has a usable scene depth buffer.

The problem is that ReShade's normal Generic Depth detection can sometimes fail to identify that buffer correctly. This has been confirmed on Linux through Wine, and similar depth-detection problems have also been reported by Windows players.

SSO Depth watches SSO's OpenGL rendering, identifies the full-resolution scene framebuffer, finds its depth texture, matches that texture to the resource already tracked by ReShade, and supplies it to ReShade effects using the standard `DEPTH` texture semantic.

This means individual shaders do not need to be modified specifically for SSO Depth.

---

# Compatibility

### Confirmed working

- Star Stable Online
- Linux through Wine
- OpenGL
- ReShade 6.8.0 with full add-on support
- AMD Radeon RX 7800 XT / Mesa

### Experimental

- Native Windows

Star Stable uses OpenGL on Windows as well, and SSO Depth's method is not inherently Wine-specific. However, native Windows has not yet been tested directly.

Other Linux distributions, Wine runners, GPUs, resolutions, and configurations may also work, but have not all been tested yet.

If you try SSO Depth on Windows or a different Linux setup, testing reports are welcome.

# Current status

SSO Depth is still an early project.

The basic depth bridge is working, including automatic detection when SSO recreates its framebuffer or depth texture.

More testing on different Linux systems, GPUs, resolutions, and Wine configurations is welcome.

---

# License

SSO Depth is licensed under the [MIT License](LICENSE).

It uses the public ReShade API headers, which are available under the BSD 3-Clause or MIT licenses. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for details.

SSO Depth is an independent community project and is not affiliated with or endorsed by Star Stable Entertainment or the ReShade project.