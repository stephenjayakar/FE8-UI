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
and fog banks are learned independently. Directly learned banks and previously
validated tiles take precedence over the provisional offset described below.
Without either sufficient offset evidence or a validated tile, unresolved banks
still draw the neutral background. The mapping and tile cache are cleared when
the chapter, map dimensions, tile rows, tileset configuration, ROM, or state
changes. This is runtime structure detection rather than a filename/hash
exception.

## Off-screen terrain at map load

Learning only inside the native frame left dark, metatile-shaped holes in the
extended view whenever a palette appeared exclusively off screen. Moving the
cursor scrolled that palette into the native view and made the holes disappear;
waiting at the original camera position could not resolve them.

For each normal/fog layer independently, rendering now derives a provisional
`(source + offset) & 15` mapping when at least two confirmed source banks agree
on an offset and **every** confirmed bank in that layer agrees. This fills
unseen terrain on uniform-offset maps without a camera visit. No retail offset
is assumed by default, and zero/one confirmed banks do not enable inference.

The inferred mapping exists only in a local render copy. It never sets the
learner's confirmed bits, changes its two-frame confirmation process, or writes
inferred pixels to the validated tile cache. A metatile with even one inferred
quadrant is not cacheable. Existing validated tiles outrank inference, and
palette/VRAM animation is read anew for provisionally rendered tiles each frame.
A newly confirmed nonuniform bank disables inference for that layer immediately;
directly learned custom banks remain authoritative.

This is a provisional uniform-layout assumption, not proof about an unseen
bank. A custom hack can initially look uniform and later contradict it; an
unseen custom bank may temporarily have the provisional palette until observed.
Known nonuniform layouts retain per-bank learning and neutral unresolved tiles.

`tests/test_extended_map_renderer.c` covers a stationary native view with two
visible banks and a third bank exclusively outside it. It checks all 16 offsets,
both fog layers, with and without a tile cache (64 combinations), plus sparse
and conflicting evidence, palette animation, mixed-quadrant tiles, resets,
custom-bank relearning, and validated-cache precedence. These are synthetic
memory/frame fixtures, not new ROM-checkpoint validation.

The renderer tests can also run without SDL or mGBA:

```sh
cc -std=c99 -Wall -Wextra -Werror -pedantic -O2 -Iapp/src \
  tests/test_extended_map_renderer.c app/src/extended_map_renderer.c \
  -o /tmp/test_extended_map_renderer
/tmp/test_extended_map_renderer
```

## Previously verified checkpoint

The current Pokémblem checkpoint exposes a 24×40 overworld. The adaptive
mapping raises the exact sampled-pixel match to 82% in its initial viewport;
the original per-bank implementation left unresolved banks neutral until the
canonical frame validated them. This checkpoint has not been rerun for the
provisional-offset change.
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
active`. On a uniform-offset map, off-screen terrain should fill once two
source banks have been confirmed, without moving the cursor. Nonuniform layouts
may still contain neutral unresolved regions; a newly learned conflicting bank
must withdraw provisional rendering rather than leave cached incorrect colors.
The composited output additionally includes the authoritative mGBA frame,
menus and overlays, extended actors, and world-space trap effects.

## Remaining edge cases

The canonical mGBA frame remains authoritative for dialogue, menus, transient
OAM particles, and moving-unit (MU) animations. Standing units and trap effects
outside that frame are decoded from FE8's linked SMS state. Any Pokémblem scene
that replaces those structures will safely fall back, but needs its own
validated provider before its off-screen transient particles can be extended.
