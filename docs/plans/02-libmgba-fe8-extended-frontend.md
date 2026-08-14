# Plan 2: FE8-specific embedded-libmGBA frontend

## Goal

Build a native macOS application around `libmgba`, specialized for the verified
US retail FE8 ROM. mGBA remains responsible for CPU, memory, PPU, audio, timing,
saves, and the canonical 240×160 frame. The host adds a larger presentation and
mouse controls.

This is an emulator-based native app, not a recompilation.

## Architecture

```text
ROM + saves -> libmGBA -> exact 240x160 frame -----+
                    |                              |
                    +-> read-only FE8 memory view  +-> composited native window
                              |                    |
                              +-> exterior terrain +-> mouse target -> GBA keys
```

The center of the window is always mGBA's framebuffer. The exterior renderer
reads known FE8U structures:

- map dimensions and row pointers;
- camera and player cursor;
- logical 16×16 metatile indices;
- tileset configuration;
- 4bpp VRAM graphics and palettes;
- fog state and, later, unit/sprite state.

It reconstructs only pixels outside the 240×160 center. This avoids changing the
game's PPU behavior and gives an exact seam reference.

## Work plan

### Phase 1: frontend shell

- Embed a pinned mGBA core in a small SDL2 or Cocoa application.
- Load a user-selected ROM, save, and optional mGBA save state.
- Render and scale the canonical framebuffer with keyboard/controller input.
- Keep ROM and save data external to the repository and app bundle.

### Phase 2: retail FE8 profile

- Identify the supported ROM by game code, revision, and hash.
- Read and validate retail FE8U symbol addresses.
- Disable enhancements safely outside a tactical map or when validation fails.
- Expose diagnostic overlays for map size, camera, cursor, and pointers.

### Phase 3: exterior terrain

- Decode FE8's metatile map, tile flips, palettes, and fog from emulated memory.
- Draw a 480×320 logical canvas centered around the canonical frame.
- Clip at map bounds and hide the extension during menus, cutscenes, battles, and
  other unsupported modes.
- Compare the reconstructed center against mGBA pixel-by-pixel while developing,
  even though the shipped compositor uses mGBA's center.

### Phase 4: units and effects

- Add host-side unit sprites, cursor, ranges, map sprites, weather, and simple
  effects outside the center.
- Let mGBA remain authoritative in the center and for all gameplay state.
- Prefer omission to inventing state when an effect is not yet decoded.

### Phase 5: mouse controls

- Transform a mouse position into world pixels using the emulated camera.
- Convert world pixels to an FE8 map tile.
- Queue ordinary D-pad presses until FE8's cursor reaches the target.
- Translate click to `A`, right-click to `B`, and wheel/auxiliary buttons only
  after testing their game semantics.
- Cancel queued movement if menus open, the map scrolls unexpectedly, or the
  cursor stops advancing.

### Phase 6: macOS polish

- Add native menus, drag-and-drop ROM loading, controller configuration, save
  management, fullscreen, integer scaling, and signed/notarized packaging.

## Compatibility boundary

This design intentionally targets one known retail build. A hack that preserves
all relevant addresses may work accidentally, but Plan 2 does not claim ROM-hack
support. Unknown ROMs still run normally in mGBA with enhancements disabled.

## Estimate

A terrain-only prototype is a days-to-weeks project. A robust frontend with
units, effects, UI transitions, audio, packaging, and full-game testing is a
several-month project. The most difficult part is faithfully extending dynamic
visuals outside the hardware viewport, not embedding mGBA itself.
