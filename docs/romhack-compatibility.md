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
frontend, but its runtime terrain palette layout does not follow retail FE8's
single bank offset.

The renderer learns a separate destination for each of the 16 source palette
banks by comparing decoded terrain inside mGBA's authoritative 240×160 frame.
A bank needs at least 24 samples, 35% exact agreement, a ten-point lead over
the runner-up, and the same result on two consecutive presented frames. Normal
and fog banks are learned independently. Terrain using unresolved banks is never
drawn with a guessed palette: the frontend reuses a previously validated tile
or draws its neutral background. The mapping and tile cache are cleared when
the chapter, map dimensions, tile rows, tileset configuration, ROM, or state
changes. This is runtime structure detection rather than a filename/hash
exception.

## Verified checkpoint

The current Pokémblem checkpoint exposes a 24×40 overworld. The adaptive
mapping raises the exact sampled-pixel match to 82% in its initial viewport;
unresolved banks remain neutral until the canonical frame validates them.
FE8's complete linked SMS list is still used to extend Pokémblem's Trap
Rework decorations without ROM-specific trap IDs or graphics addresses.

An existing Fire Emblem Archanea checkpoint was used as a regression case. It
selected the standard offset 11 with an 84% match and rendered its 34×15 map
and 21 units. Non-tactical screens continue to fall back to the canonical mGBA
frame.

## Reproduce the diagnostic

Use the launcher for normal play. For a deterministic terrain-only capture:

```sh
build/fe8-mgba-sdl.app/Contents/MacOS/fe8-mgba-sdl \
  --rom "/path/to/Pokemblem.GBA" \
  --state "/path/to/quick-state.ss" \
  --auto-continue \
  --capture-terrain /tmp/pokemblem-terrain.bmp \
  --capture /tmp/pokemblem-composited.bmp \
  --capture-after 150
```

Expected log lines include `Terrain palette: learned` and `Extended renderer
active`. The terrain-only output may contain neutral regions that have not yet
appeared in the native frame; it must never contain a best-effort wrong palette.
The composited output additionally includes the authoritative mGBA frame,
menus and overlays, extended actors, and world-space trap effects.

## Remaining edge cases

The canonical mGBA frame remains authoritative for dialogue, menus, transient
OAM particles, and moving-unit (MU) animations. Standing units and trap effects
outside that frame are decoded from FE8's linked SMS state. Any Pokémblem scene
that replaces those structures will safely fall back, but needs its own
validated provider before its off-screen transient particles can be extended.
