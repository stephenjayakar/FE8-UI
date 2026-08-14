# Pokémblem extended-renderer compatibility

This branch adds an adaptive terrain-palette profile that allows the existing
FE8 extended renderer to support Pokémblem without embedding or modifying its
ROM. The tested ROM identifies as an FE8U-family game (`BE8E`) and has SHA-1:

```text
fd8b68e404c480ddb37dce035960c6850b7614df
```

No ROM data or Pokémblem assets are included in this repository.

## What was different

Pokémblem's free-roaming overworld preserves the FE8 map dimensions, row
pointers, metatile configuration, camera, and map-sprite handles used by the
frontend. Its terrain palettes are loaded with bank offset 6, however, while
standard FE8 maps normally add offset 11 to each metatile palette index.

The renderer now treats normal and fog palette-bank offsets as map-render
profile data. When the default layout does not visually match mGBA's canonical
240×160 frame, the frontend probes all 16 possible bank offsets, selects only a
candidate that passes the existing visual validator, and periodically retries
while no candidate is valid. This is runtime structure detection rather than a
Pokémblem filename or hash exception, so other FE8U hacks with relocated
palette banks can use the same path.

## Verified checkpoint

The Pokémblem checkpoint in the local game library exposes a 24×20 overworld.
At the stable outdoor frame the adaptive profile selected offset 6 with a 60%
exact sampled-pixel match. FE8's complete linked SMS list contained 20 world
sprites: four unit-backed actors plus 16 non-unit map sprites. Rendering the
list extended Pokémblem's Trap Rework decorations, including rocks below the
native viewport, without ROM-specific trap IDs or graphics addresses. A
30-frame comparison also confirmed that an actor outside the native viewport
continued to animate from live OBJ VRAM.

An existing Fire Emblem Archanea checkpoint was used as a regression case. It
selected the standard offset 11 with an 84% match and rendered its 34×15 map
and 21 units. Non-tactical screens continue to fall back to the canonical mGBA
frame.

## Reproduce the diagnostic

Use the launcher for normal play. For a deterministic terrain-only capture:

```sh
build/prototype/fe8-mgba-sdl.app/Contents/MacOS/fe8-mgba-sdl \
  --rom "/path/to/Pokemblem.GBA" \
  --state "/path/to/quick-state.ss" \
  --auto-continue \
  --capture-terrain /tmp/pokemblem-terrain.bmp \
  --capture /tmp/pokemblem-composited.bmp \
  --capture-after 150
```

Expected log lines include `Terrain profile: palette offset 6` and `Extended
renderer active`. The terrain-only output should show the whole map with
correct colors; the composited output additionally includes the authoritative
mGBA frame, menus and overlays, extended actors, and world-space trap effects.

## Remaining edge cases

The canonical mGBA frame remains authoritative for dialogue, menus, transient
OAM particles, and moving-unit (MU) animations. Standing units and trap effects
outside that frame are decoded from FE8's linked SMS state. Any Pokémblem scene
that replaces those structures will safely fall back, but needs its own
validated provider before its off-screen transient particles can be extended.
