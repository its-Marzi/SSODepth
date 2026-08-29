# SSO Depth

![GitHub release](https://img.shields.io/github/v/release/its-Marzi/SSODepth)
![GitHub downloads](https://img.shields.io/github/downloads/its-Marzi/SSODepth/total)

A small ReShade add-on that restores depth access in **Star Stable Online** when ReShade can't find it properly.

In other words: if your DOF, MXAO, fog, or other depth effects aren't working, this is for you.

Tested on Linux through Wine and Windows 11 with ReShade 6.8.0.

> [!NOTE]
> Already have ReShade working in SSO? Installation is just one file.

## Installation

You need **ReShade 6.8.0 with full add-on support** already working in Star Stable.

1. Download `SSODepth.addon64` from the [Releases](../../releases) page.
2. Find the folder containing `SSOClient.exe`.
3. Put `SSODepth.addon64` in that same folder.
4. Start Star Stable normally.

SSO Depth should appear in ReShade's Add-ons tab.

There are no settings to configure. ReShade's Generic Depth add-on is not required.

## Having problems?

First make sure:

- `SSODepth.addon64` is next to `SSOClient.exe`
- you are using ReShade 6.8.0 with full add-on support
- SSO Depth appears in ReShade's Add-ons tab

<details>
<summary><b>Still not working?</b></summary>

You can test the depth buffer with `DisplayDepth`.

The settings used during testing are:

```text
RESHADE_DEPTH_INPUT_IS_UPSIDE_DOWN=1
RESHADE_DEPTH_INPUT_IS_REVERSED=0
RESHADE_DEPTH_INPUT_IS_LOGARITHMIC=0
```

You can also check `ReShade.log`. A working installation should contain something similar to:

```text
[SSO Depth] Active depth bridge: 1920x1080, FBO ..., OpenGL texture ...
```

If you open a GitHub issue, please include your `ReShade.log`, operating system, GPU, ReShade version, and how you run SSO.

</details>

## License

SSO Depth is licensed under the [MIT License](LICENSE).

It uses the public ReShade API headers. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for details.

SSO Depth is an independent community project and is not affiliated with or endorsed by Star Stable Entertainment or the ReShade project.

## Building
Want to build or modify SSO Depth yourself? See [BUILDING.md](BUILDING.md).
