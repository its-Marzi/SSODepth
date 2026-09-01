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

No manual depth configuration is required. SSO Depth automatically applies the ReShade depth settings Star Stable needs.

ReShade's Generic Depth add-on is not required.

## UI and cursor

SSO Depth runs ReShade effects before Star Stable draws its interface and mouse cursor. This means effects such as DOF can affect the game world while the UI stays sharp automatically.

No extra UI shader or special technique order is required.

## Having problems?

First make sure:

- `SSODepth.addon64` is next to `SSOClient.exe`
- you are using ReShade 6.8.0 with full add-on support
- SSO Depth appears in ReShade's Add-ons tab

<details>
<summary><b>Still not working?</b></summary>

Open ReShade's **Add-ons** tab and expand **SSO Depth**.

The built-in self-test checks scene detection, depth access, UI-safe rendering, and the depth configuration expected by SSO Depth.

Click **Copy diagnostic report** and include the report when asking for help or opening a GitHub issue.

You can also test the depth buffer visually with `DisplayDepth`.

</details>

## License

SSO Depth is licensed under the [MIT License](LICENSE).

It uses the ReShade add-on API and state-tracking utility code. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for details.

SSO Depth is an independent community project and is not affiliated with or endorsed by Star Stable Entertainment or the ReShade project.

## Building
Want to build or modify SSO Depth yourself? See [BUILDING.md](BUILDING.md).
