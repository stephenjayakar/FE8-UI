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
* Inventory editor - Press `I`. This allows you to more easily swap units' stuff
* Anchored native map HUD - supported unit, objective and terrain panels stay at the window edges, independently of map zoom. `+` / `-` resize the HUD; `0` resets it. Action menus stay beside the selected unit. [Controls, supported layouts and fallback behavior](docs/native-map-hud.md).

# Project Credits
This project heavily leans on
1. libmgba https://github.com/mgba-emu/mgba
2. FE8 Decomp https://github.com/FireEmblemUniverse/fireemblem8u

And both projects have been imported as reference / submodules.