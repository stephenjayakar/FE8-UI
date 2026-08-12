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

The logical canvas is 480×320 and the initial window is 960×640. Nearest-neighbor
scaling preserves pixel art. Escape or the window close button exits.

Keyboard bindings:

| GBA input | Keyboard |
| --- | --- |
| A / B | Z / X |
| D-pad | Arrow keys |
| Start / Select | Return / Backspace |
| L / R | A / S |

Mouse controls on a validated tactical map:

- Left-click sets a map-tile target. The frontend observes the real FE8 cursor
  and sends paced D-pad taps until it reaches that tile.
- Double-click does the same and then sends `A`.
- Right-click sends `B` and cancels queued movement.
- Movement is cancelled automatically if the tactical-map snapshot becomes
  invalid. Cursor coordinates are never written directly.

## ROM-hack compatibility

Retail FE8U and FE8U-derived hacks with the standard `BE8E` header are allowed
to attempt the retail-layout profile. Every pointer, dimension, row table, and
cursor coordinate is validated each frame before the extension activates.

This was tested with the local retail ROM/state and with *Sacred Stones
Reforged*, where a 22×28 map rendered outside the hardware frame. Hacks that
relocate globals, replace tileset storage, or change the renderer need a separate
profile. They still run through mGBA; only the enhancement falls back.

The prototype currently draws terrain and fog outside the center. Unit sprites,
cursor art, weather, map animations, range/movement overlays, and UI remain
canonical inside mGBA's 240×160 frame and are not yet reproduced outside it.

## Diagnostic capture options

These options exist for repeatable local testing and do not touch ROM data:

```sh
--capture /tmp/frame.bmp --capture-after 120
--mouse-target 7,5
--auto-continue
--seek-large-map
```

`--mouse-target` exercises the same D-pad path controller used by clicks.
`--auto-continue` sends a small scripted set of normal A/Start inputs.
`--seek-large-map` waits for a validated map larger than 15×10 (or stops after a
bounded timeout) before capturing.

## Renderer architecture

`fe8_profile.c` recognizes and validates FE8 memory. `extended_map_renderer.c`
decodes the full logical metatile map, tile flips, 4bpp graphics, palettes, and
fog. `main.c` composites the canonical frame and converts mouse targets to GBA
keys. The modules communicate through callback-based byte readers rather than
private mGBA structs.

The core is configured with `setVideoBuffer()` and queried through
`baseVideoSize()`. `mColor` is converted at this boundary so the SDL texture
format remains explicit and portable across mGBA's 16-bit and 32-bit builds.

The three alternative project plans are preserved in [`../docs/plans/`](../docs/plans/README.md).
