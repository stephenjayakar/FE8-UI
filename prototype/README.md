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

The first configure downloads mGBA at commit `afd6f14eaf8bd35214ed3fb9dc69a92bfc3877a9`. The mGBA build is restricted to the static GBA core; the SDL2 frontend in `src/main.c` is intentionally owned by this prototype. The newer pin is deliberate: the supplied save state was produced by an mGBA 0.11 build and cannot be restored by the older 0.10.3 state decoder. On macOS the result is `build/prototype/fe8-mgba-sdl.app`.

## Run

On macOS, open the application without arguments to show the native game
library:

```sh
open build/prototype/fe8-mgba-sdl.app
```

Drop one or more `.gba` files anywhere in the library window, or use **Add
ROMs…**. **Play** starts from that game's cartridge save; **Resume State** loads
its most recent F5 checkpoint. Games are keyed by the ROM's SHA-1, so retail
FE8 and ROM hacks always receive separate storage under:

```text
~/Library/Application Support/FE8 Extended Frontend/Games/<ROM SHA-1>/
├── cartridge.sav
└── quick-state.ss
```

If a same-name `.sav` exists beside an imported ROM, it is copied into isolated
storage on first launch. Removing a row only removes the library reference; it
does not delete the ROM or that game's saves.

**Settings → Settings…** is available from both the library and a running game.
Audio, VSync, extended rendering, video shader, and every keyboard binding are
global and apply to all imported games.

The Settings window also has a global **Hotkeys** section. Hold **Space** to run
at 4× speed; audio and VSync resume automatically when it is released. **F5**
quick-saves and **F8** quick-loads the current game's isolated state by default.
All three hotkeys can be rebound by clicking their binding and pressing a key.

The command-line interface remains available for diagnostics and direct launch:

```sh
build/prototype/fe8-mgba-sdl.app/Contents/MacOS/fe8-mgba-sdl \
  --rom /path/to/fireemblem8.gba \
  [--state /path/to/state.ss] \
  [--save /path/to/fireemblem8.sav] \
  [--no-extensions]
```

`--rom` is required in command-line mode. `--state` restores an mGBA named state
after the ROM is reset. It also understands the older PNG-bundled state supplied
with this test set by safely extracting its `gbAs` core payload in memory.
`--save` opens or creates a cartridge save through mGBA before reset. Use a copy
when testing: mGBA may write normal game progress back to this file.
`--no-extensions` leaves the 480×320 canvas letterboxed and runs as an ordinary
frontend.

The logical canvas is 480×320. The window starts maximized, preserves that 3:2
aspect ratio with letterboxing as needed, and uses nearest-neighbor scaling to
preserve pixel art. Escape or the window close button exits.
Emulation is paced from libmGBA's GBA timing (59.728 fps), independently of a
60 Hz or 120 Hz display refresh rate.

Audio is enabled by default and converted from libmGBA's native stream to the
CoreAudio device format through SDL. Presentation uses VSync while retaining
GBA-clock pacing. Open **Settings → Settings…** in the macOS menu bar (or press
Command-comma) to toggle audio, VSync, or the extended renderer and select a
video preset. **Off** is the default sharp, unfiltered presentation; **CRT (TV
Mode)** adds mGBA's subtle horizontal blend and scanlines; **Scanlines** keeps
the pixels sharp while darkening alternating lines. The shader processes the
entire 480×320 host canvas, including the extended map, and can be changed live.

To rebind a GBA button, click its current binding and press the desired key;
Escape cancels capture. Standalone modifier keys—including left/right Shift,
Control, Option, and Command—can be assigned. Settings persist globally through
macOS user defaults.

The macOS **State** menu provides **Quick Save State**, **Quick Load State**,
**Save State As…**, and **Load State…**. When launched with `--quick-state
/path/to/checkpoint.ss`, the configured Quick Save and Quick Load hotkeys use
that checkpoint. Library launches supply each game's isolated checkpoint
automatically.

On the `mouse` branch, pointer motion over a validated tactical map moves FE8's
map cursor. Left-click moves to the latched tile and presses A; right-click
presses B. Pointer coordinates are converted through the actual window,
high-DPI drawable, letterboxed 480×320 canvas, and current extended-map
viewport, so Retina scaling does not alter tile selection. Cursor movement uses
FE8's normal D-pad path while holding B for the game's native fast-cursor mode,
so its camera and cursor animation stay synchronized without the sluggish
default travel speed. B is released before an A confirmation, and right-click
forces a fresh B press so cancellation remains reliable. If FE8 rejects the
same step repeatedly, mouse pathing cancels safely and waits for fresh pointer
motion; it never writes or teleports cursor state. The
authoritative mGBA frame, host cursor, terrain, units, and hit-testing all share
the same movable viewport origin, preventing cursor offsets and edge seams.
Mouse map input is disabled during dialogue and cutscenes, while left/right
click continue to act as A/B for native UI.

Mouse controls are enabled by default and can be toggled globally under
**Settings → Settings… → Enable mouse controls**. When enabled, the game canvas
uses a large blue-and-gold tactician pointer with an exact tip hotspot. Disabling
the option immediately cancels pending mouse input and restores the normal macOS
pointer; the preference also applies to games launched later from the library.

Hold Shift and drag with the left button to pan the extended map. A Shift-click
without dragging recenters the host viewport on that tile. Panning changes only
the host view and does not mutate FE8's camera.

Keyboard bindings:

| GBA input | Keyboard |
| --- | --- |
| A / B | Z / X |
| D-pad | Arrow keys |
| Start / Select | Return / Backspace |
| L / R | A / S |

## ROM-hack compatibility

Retail FE8U and FE8U-derived hacks with the standard `BE8E` header are allowed
to attempt the retail-layout profile. Every pointer, dimension, row table, and
cursor coordinate is validated each frame before the extension activates.

This was tested end-to-end on a retail FE8U 28×24 tactical map. A visual
compatibility check compares host-decoded terrain with the canonical frame
before trusting an FE8U-family profile. Hacks that relocate globals, replace
tileset storage, or change the renderer continue to run through mGBA while the
enhancement safely falls back instead of displaying corrupt terrain.

Sacred Echoes v1.1.2 was tested from boot into a 20×14 tactical map. It boots,
loads its cartridge save, produces audio, and activates extended terrain during
genuine tactical presentation. Dialogue and cutscenes may leave valid map
structures resident while repurposing VRAM; the per-frame visual gate falls
back to the canonical GBA view for those scenes and automatically restores the
extension when player-phase map rendering resumes.

The prototype draws terrain, fog, standing unit map sprites, and a host cursor
across the extended canvas. The canonical mGBA frame is always composited last
over the center, so FE8 menus, selected units, map animations, range/movement
overlays, and transient moving-unit animations remain visible and authoritative.

## Diagnostic capture options

These options exist for repeatable local testing and do not touch ROM data:

```sh
--capture /tmp/frame.bmp --capture-after 120
--auto-continue
--seek-large-map
--state-out /tmp/large-map.ss
```

`--auto-continue` sends a small scripted set of normal A/Start inputs.
`--seek-large-map` waits for a validated map larger than 15×10 (or stops after a
bounded timeout) before capturing. When `--state-out` is supplied, that large,
interactive checkpoint is also saved for repeatable tests.

## Renderer architecture

`fe8_profile.c` recognizes and validates FE8 memory. `extended_map_renderer.c`
decodes the full logical metatile map, tile flips, 4bpp graphics, palettes, and
fog. `extended_unit_renderer.c` reconstructs standing sprites from validated
unit handles, OBJ VRAM, and OBJ palettes. `main.c` handles the host camera, HUD
composition, frame pacing, and keyboard-to-GBA input. On macOS,
`host_video_gl.c` presents that canvas using mGBA's `mGLES2Context` and shader
pass implementation; other platforms retain the SDL renderer fallback. The
modules communicate through callback-based byte readers rather than private
mGBA structs.

The core is configured with `setVideoBuffer()` and queried through
`baseVideoSize()`. `mColor` is converted at this boundary so the host upload
format remains explicit and portable across mGBA's 16-bit and 32-bit builds.

The three alternative project plans are preserved in [`../docs/plans/`](../docs/plans/README.md).
The complete build and validation recipe is in
[`../docs/prototype-testing.md`](../docs/prototype-testing.md).
