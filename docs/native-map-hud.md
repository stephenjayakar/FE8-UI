# Anchored native map HUD

The extended map can pan and zoom without moving or shrinking the supported
native information windows. This is a read-only compositor, not a replacement
menu implementation: portraits, text, icons, numbers, colors and the menu hand
come from the running ROM. FE8 still owns selection, actions and game state.

## Presentation and controls

On a validated tactical map the unit card is anchored at the upper left,
objectives at the upper right, and terrain information at the lower right.
The native action menu opens beside the selected unit (on the other side when
necessary), stays stationary while choosing options, and is clamped to the
window. The three information windows no longer swap sides with the GBA cursor.

The HUD is drawn in drawable pixels after game scaling and, on OpenGL, after
the CRT shader chain. Mouse-wheel zoom affects the map only. **+ / =** and **-**
resize the visible map HUD in ten-percentage-point steps from 80% to 200%; **0**
restores 150%. Keypad equivalents work. Explicit GBA/hotkey rebindings take
precedence, and modifier shortcuts are not intercepted. The preference lasts
for the current launch. Armory keeps its own existing size controls.

The baseline is the unzoomed 480x320 presentation: at a 960x640 drawable the
150% default draws a native HUD pixel at 3x, regardless of map zoom. Resizing
adapts this scale and fits the panels inside the window; Retina drawables use
their real pixel dimensions. Fixed information panels consume map hover and
left-click input instead of moving the game cursor behind them. Native menu
A/B input remains unchanged. Leaving the window cancels mouse map travel.

`--native-ui` retains the previous, undetached native presentation for the
whole launch. `--hud-scale 80..200` selects the initial HUD size. Disabling the
extended renderer also disables detachment.

## Scope and safety

This first pass recognizes the regular FE8 tactical BG layout and rectangular
unit, terrain, objective and single action-menu windows. Both supplied Archanae
and Sacred Echoes ROMs were exercised. It is deliberately **not** a generalized
replacement for every ROM-hack menu.

Dialogue, battle forecasts, movement selection, native full-screen menus,
Armory, unknown window shapes, unsupported PPU effects, and incomplete/sliding
native-window transitions retain their original presentation. The existing
extended-renderer state machine still decides whether that presentation is
centered or has a frozen map backdrop. Once the supported tactical layout
returns, the HUD is reacquired automatically. There is no cached stale unit
card substituted for an unsupported or transitioning screen.

`native_hud.c` reads public, raw PPU state through the project's byte-reader
interface: BG0/BG1 graphics and recognized foreground OBJ sprites are UI;
BG2/BG3 and map objects stay in world space. Pixels covered by a detached
window are reconstructed from the current lower layers, including units and
native cursor corners, rather than filled with bare terrain or a previous
frame. Translucent window backgrounds remain translucent at their new location.

Before stripping anything, the reconstruction must agree with at least 98% of
sampled native UI pixels (eight RGB levels of tolerance, also accommodating
16-bit core output). Unknown foreground sprites, affine/mosaic/8bpp/window
modes and unsupported blending fail closed to the complete canonical frame.
I/O reads use `mCore.rawRead8`: write-only scroll/blend registers return open-bus
values through ordinary bus reads. No ROM bytes, emulated RAM, CPU/PPU state,
cartridge saves or input-selection structures are modified.

`native_hud_host.c` handles the independent layout, pointer transform, sizing
shortcuts and unfiltered diagnostic capture. Both SDL and OpenGL backends
present a separate straight-alpha RGBA overlay at drawable resolution. The
OpenGL pass runs after the map's filters; game zoom never downsamples the HUD.
Allocation/extraction failures leave the native frame untouched; a backend
presentation failure is reported through the existing video-error path.

## Validation

Run the ordinary suite (no ROM required for the synthetic HUD tests):

```sh
./scripts/test.sh -G Ninja
```

For optional local real-ROM checks, configure the existing private ROM inputs:

```sh
cmake -S . -B build \
  -DFE8_ARCHANAE_ROM="/path/to/Fire Emblem Archanea.GBA" \
  -DFE8_SACRED_ECHOES_ROM="/path/to/Fire Emblem Sacred Echoes.GBA"
cmake --build build
ctest --test-dir build --output-on-failure
```

`native_hud` covers extraction, unsupported layouts and PPU effects, palette
mismatch, alpha, hidden-unit recovery, native cursor separation, unknown OBJ
rejection, corner anchoring, action-menu latching, small/odd/HiDPI dimensions,
clipping and padded-stride guards. `native_hud_host` covers zoom-independent
hit testing, letterboxing, scale shortcuts, rebinding priority, capture and
cursor clipping. The synthetic extraction test also passed ASan/UBSan locally.

The two optional `native_hud_*` ROM tests boot through normal button input,
check 180 idle frames, enter and navigate native action menus for 90 frames,
cancel and reacquire the HUD, and verify byte-identical emulator save states
before/after extraction. ROMs and states are neither committed nor emitted as
CI artifacts.

Local validation: all 40 configured tests passed with both supplied ROMs.
Interactive SDL/Xvfb checks covered wheel zoom, +/-/0, F6 off/on, menu selection
and cancellation. Actual window captures were taken with both SDL and the
OpenGL 3.2 backend on Linux/Mesa. The OpenGL backend was compiled against the
pinned mGBA sources; a native macOS/Retina runtime was not available locally.

`--capture` includes the HUD at drawable resolution when it is active. Like the
existing canvas capture, this diagnostic CPU image is unfiltered; use an OS
window screenshot to capture CRT output. No ROM, save, font or third-party
binary assets are included in this change.
