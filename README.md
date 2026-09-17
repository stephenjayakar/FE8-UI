# FE8 Extended Frontend

This project mainly adds extended rendering to FE8 + romhacks of FE8.

![Archanae Extended](/docs/archanae-extended.png)

A big criticism I had of playing romhacks of FE8 was that they
* Only rendered a small portion of the map
* Didn't have mouse controls

This project aims at fixing that. However, it requires small patches per romhack. The only ones I've tested myself are Sacred Echoes, Sacred Stones, and Archanae. Feel free to open issues for other ones.

# Features
* Extended renderer - make sure to enable this in settings
* Mouse controls - only works when extended renderer is on. Only works for unit movement
* Inventory editor - Press `I`. This allows you to more easily swap units' stuff. Note that you can cheat with this, since it works all the time (even in combat!). Do with that as you wish.
* Anchored native map HUD - supported unit, objective and terrain panels stay at the window edges, independently of map zoom. `+` / `-` resize the HUD; `0` resets it. Action menus stay beside the selected unit. [Controls, supported layouts and fallback behavior](docs/native-map-hud.md).

Here's what the inventory view looks like:
![Sacred Echoes Inventory View](/docs/sacred-echoes-inventory.png)

## Experimental voxel view

The macOS **Voxel rendering** setting now offers **OpenGL (GPU)** (default) and
**Software (CPU)**. Selection, range preview, walking and native action menus stay
in the voxel view. [GPU/tactics controls, scope and validation](docs/voxel-gpu-tactics.md).

Use the **Voxel Renderer** hotkey under **Settings → Settings… → Hotkeys** on a
supported tactical map, or launch with `--voxel`, for a native voxel presentation
generated from the running ROM. The default binding is F7, and it can be rebound
to an ordinary key such as V. Units use live sprite pixels; buildings and foliage
are inferred from terrain. Combat, dialogue and other unsupported scenes retain
the original renderer. [Controls, scope and validation](docs/voxel-renderer.md).

# Installing
The project is in beta. Download the latest version from [Releases](https://github.com/stephenjayakar/FE8-UI/releases). Builds are available for Mac ARM, Linux x86-64, and Windows x64. Windows includes a ROM library and native game menus; see the [Windows launch instructions](docs/windows.md#run).

1. Install the app how you would for your platform. On Mac, you should drag the `.app` file to Applications
2. Launch it
3. Drag in FE8 (Sacred Stones) or your romhack of choice
4. Setup your controls, enable extended renderer & mouse controls, and go ham!

Mouse controls right now only work in combat.

# Building for Windows

Windows x64 build support is available through PowerShell and GitHub Actions.
You can trigger the Windows build from a Mac and download the resulting ZIP.
See [Windows build instructions](docs/windows.md) for setup and launch commands.

# Testing / reporting bugs
Please open GitHub issues! Right now, the way I've tested this project is by doing a run through a game. I beat Archanae on it, but I'm obviously limited by how much time I have. Generally, games need to have a profile for them to work 100%. The games I've tested are:
* Sacred Stones (vanilla)
* Sacred Echoes
* Pokemblem
* Archanae
* Cerulean Crescent

Please try out more romhacks and file issues if they don't work well!

# Project Credits
This project heavily leans on
1. libmgba https://github.com/mgba-emu/mgba
2. FE8 Decomp https://github.com/FireEmblemUniverse/fireemblem8u

And both projects have been imported as reference / submodules.

## AI Disclaimer

The project makes heavy use of AI. FYI if that bothers you.
