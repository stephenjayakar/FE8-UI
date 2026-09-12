# FE8 Extended Frontend

This project mainly adds extended rendering to FE8 + romhacks of FE8.

![Archanae Extended](/docs/archanae-extended.png)

A big criticism I had of playing romhacks of FE8 was that they
* Only rendered a small portion of the map
* Didn't have mouse controls

This project aims at fixing that. However, it requires small patches per romhack. The only ones I've tested myself are Sacred Echoes, Sacred Stones, and Archanae. Feel free to open issues for other ones.

# Features
* Extended renderer - make sure to enable this in settings
* Mouse controls - point to move the map cursor; left-click confirms (A), right-click goes back (B). Scroll up/down to select native menu options and weapons. On the tactical map, scrolling still zooms; Ctrl+scroll zooms from native menus too. Map pointing requires the extended renderer, but native menu controls do not. Armory keeps its own list scrolling.
* Inventory editor - Press `I`. This allows you to more easily swap units' stuff
