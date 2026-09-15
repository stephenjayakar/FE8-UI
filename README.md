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

# Installing
The project is in beta, so for now it's a pretty basic setup. Head over to https://github.com/stephenjayakar/FE8-UI/releases and download the latest version. Right now, we only have builds for Mac ARM and Linux. Please open an issue if you want me to try building for another platform!

1. Install the app how you would for your platform. On Mac, you should drag the `.app` file to Applications
2. Launch it
3. Drag in FE8 (Sacred Stones) or your romhack of choice
4. Setup your controls, enable extended renderer & mouse controls, and go ham!

Mouse controls right now only work in combat.

# Project Credits
This project heavily leans on
1. libmgba https://github.com/mgba-emu/mgba
2. FE8 Decomp https://github.com/FireEmblemUniverse/fireemblem8u

And both projects have been imported as reference / submodules.

## AI Disclaimer

The project makes heavy use of AI. FYI if that bothers you.