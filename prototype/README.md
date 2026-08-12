# FE8 embedded mGBA extended-map prototype

This directory contains a working proof of concept for running *Fire Emblem: The Sacred Stones* and compatible FE8U ROM hacks through the public libmGBA API while drawing map terrain beyond the 240×160 GBA frame. It builds a pinned mGBA `0.11` development snapshot and supplies a small SDL2 frontend.

The extension is deliberately read-only. libmGBA remains authoritative for CPU,
memory, PPU, saves, and input. The host reads validated FE8 structures, decodes
the game's metatiles from emulated EWRAM/VRAM/palette memory, renders a 480×320
terrain canvas, and places mGBA's exact frame over its center.

## Requirements

- CMake 3.20 or newer
- A C11 compiler
- SDL2 development files
- A legally obtained FE8 ROM (`fireemblem8.gba` or another `.gba` path)
- Git, for CMake's `FetchContent` download

On Apple Silicon, install the native arm64 SDL2 package (for example, Homebrew's `sdl2`) and configure with `-DCMAKE_OSX_ARCHITECTURES=arm64` if the architecture is not already inferred by the toolchain.

## Configure and build

From the repository root:

```sh
cmake -S prototype -B build/prototype \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build/prototype --target fe8-mgba-sdl
```

If SDL2 is installed outside the default prefix, add its prefix while configuring, for example:

```sh
cmake -S prototype -B build/prototype \
  -DCMAKE_PREFIX_PATH=/opt/homebrew \
  -DCMAKE_OSX_ARCHITECTURES=arm64
```

The first configure downloads mGBA at commit `afd6f14eaf8bd35214ed3fb9dc69a92bfc3877a9`. The mGBA build is restricted to the static GBA core; the SDL2 frontend in `src/main.c` is intentionally owned by this prototype. The newer pin is deliberate: the supplied save state was produced by an mGBA 0.11 build and cannot be restored by the older 0.10.3 state decoder.

## Run

```sh
build/prototype/fe8-mgba-sdl \
  --rom /path/to/fireemblem8.gba \
  [--state /path/to/state.ss] \
  [--save /path/to/fireemblem8.sav] \
  [--no-extensions]
```

`--rom` is required. `--state` restores an mGBA named state after the ROM is
reset. It also understands the older PNG-bundled state supplied with this test
set by safely extracting its `gbAs` core payload in memory. `--save` opens a
cartridge save through mGBA before reset. Use a copy when testing: mGBA may write
normal game progress back to this file. `--no-extensions` leaves the 480×320
canvas letterboxed and runs as an ordinary frontend.

The logical canvas is 480×320. The window starts maximized, preserves that 3:2
aspect ratio with letterboxing as needed, and uses SDL's logical-coordinate
conversion so mouse input remains aligned on Retina displays. Nearest-neighbor
scaling preserves pixel art. Escape or the window close button exits.
Emulation is paced from libmGBA's GBA timing (59.728 fps), independently of a
60 Hz or 120 Hz display refresh rate.

Keyboard bindings:

| GBA input | Keyboard |
| --- | --- |
| A / B | Z / X |
| D-pad | Arrow keys |
| Start / Select | Return / Backspace |
| L / R | A / S |

Mouse controls on a validated tactical map:

- Moving the mouse over the map moves the real FE8 cursor with acknowledged,
  paced D-pad taps.
- Left-click moves to the map tile and sends `A`, allowing units and map tiles
  to be selected with a normal click.
- Right-click sends `B` and cancels queued movement.
- Shift-click recenters the extended view. Shift-drag pans it across maps that
  are larger than the host canvas without changing FE8's own camera.
- `H` toggles the original FE8 framebuffer/HUD. The clean extended view remains
  active after clicks; enable the native HUD explicitly when a menu is needed.
- Temporary FE8 input locks pause mouse movement instead of discarding it.
  Cursor coordinates are never written directly.

## ROM-hack compatibility

Retail FE8U and FE8U-derived hacks with the standard `BE8E` header are allowed
to attempt the retail-layout profile. Every pointer, dimension, row table, and
cursor coordinate is validated each frame before the extension activates.

This was tested end-to-end on a retail FE8U 28×24 tactical map. Several hacks
also pass the structural map checks, but hacks that relocate globals, replace
tileset storage, or change the renderer still need a separate visual profile.
They continue to run through mGBA; only the enhancement falls back.

The prototype draws terrain, fog, standing unit map sprites, and a host cursor
across the extended canvas. Weather, map animations, range/movement overlays,
and transient moving-unit animations still come from the canonical mGBA frame.
The original frame can always be restored with `H`.

## Diagnostic capture options

These options exist for repeatable local testing and do not touch ROM data:

```sh
--capture /tmp/frame.bmp --capture-after 120
--mouse-target 7,5
--mouse-click 7,5
--auto-continue
--seek-large-map
--state-out /tmp/large-map.ss
--pan-test
```

`--mouse-target` exercises the same D-pad path controller used by clicks.
`--mouse-click` also verifies the final `A` confirmation.
`--pan-test` injects a Shift-drag through SDL for a repeatable viewport test.
`--auto-continue` sends a small scripted set of normal A/Start inputs.
`--seek-large-map` waits for a validated map larger than 15×10 (or stops after a
bounded timeout) before capturing. When `--state-out` is supplied, that large,
interactive checkpoint is also saved for repeatable tests.

## Renderer architecture

`fe8_profile.c` recognizes and validates FE8 memory. `extended_map_renderer.c`
decodes the full logical metatile map, tile flips, 4bpp graphics, palettes, and
fog. `extended_unit_renderer.c` reconstructs standing sprites from validated
unit handles, OBJ VRAM, and OBJ palettes. `main.c` handles the host camera, HUD
composition, frame pacing, and mouse-to-GBA input. The modules communicate
through callback-based byte readers rather than private mGBA structs.

The core is configured with `setVideoBuffer()` and queried through
`baseVideoSize()`. `mColor` is converted at this boundary so the SDL texture
format remains explicit and portable across mGBA's 16-bit and 32-bit builds.

The three alternative project plans are preserved in [`../docs/plans/`](../docs/plans/README.md).
The complete build and validation recipe is in
[`../docs/prototype-testing.md`](../docs/prototype-testing.md).
