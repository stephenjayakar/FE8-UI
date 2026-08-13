# Testing the embedded-libmGBA prototype

These commands test the native macOS frontend. They do not rebuild or modify
the ROM. Use a legally obtained US FE8 ROM and copies of any cartridge saves.

## Build and unit tests

From the repository root:

```sh
cmake -S prototype -B build/prototype \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/opt/homebrew \
  -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build/prototype --target fe8-mgba-sdl -j4

cmake -S prototype/tests -B build/prototype-tests
cmake --build build/prototype-tests -j4
ctest --test-dir build/prototype-tests --output-on-failure
```

## Verify the macOS game library

Launch the app without command-line arguments:

```sh
open build/prototype/fe8-mgba-sdl.app
```

Drop retail FE8 and at least one ROM hack into the window. Confirm that both
rows survive an app restart and that **Play** starts the selected ROM. Their
SHA-1 directories under `~/Library/Application Support/FE8 Extended
Frontend/Games/` must differ. Press F5 during one game, return to the library,
and confirm only that game's **Resume State** button becomes available.

If a ROM has an adjacent same-name `.sav`, first launch should copy it to that
ROM's `cartridge.sav`. Removing the row must leave both the ROM and its hashed
save directory untouched. Moving or deleting the ROM should change the row to
`ROM missing` and disable launch actions.

## Open a known map

Use either a cartridge save (`.sav`) or an mGBA state (`.ss`/`.ss1`):

```sh
build/prototype/fe8-mgba-sdl.app/Contents/MacOS/fe8-mgba-sdl \
  --rom "/path/to/Fire Emblem The Sacred Stones.GBA" \
  --state "/path/to/map.ss"
```

The window starts maximized and scales the 480×320 logical canvas to the
largest 3:2 image that fits the display. Black bars are expected when the
display has a different aspect ratio.

## Verify a large map and canonical UI

Given a save that can enter a tactical map, this finds a validated map larger
than the original 15×10 viewport and records a repeatable checkpoint:

```sh
build/prototype/fe8-mgba-sdl.app/Contents/MacOS/fe8-mgba-sdl \
  --rom "/path/to/Fire Emblem The Sacred Stones.GBA" \
  --save "/path/to/a-copy-of-save.sav" \
  --auto-continue --seek-large-map \
  --capture /tmp/fe8-large-map.bmp \
  --state-out /tmp/fe8-large-map.ss
```

Open the recorded checkpoint interactively:

```sh
build/prototype/fe8-mgba-sdl.app/Contents/MacOS/fe8-mgba-sdl \
  --rom "/path/to/Fire Emblem The Sacred Stones.GBA" \
  --state /tmp/fe8-large-map.ss
```

Success is reported as `extended=yes` with a map larger than 15×10. Use the
keyboard to select a unit and open its action menu. The original 240×160 mGBA
frame should remain intact in the center, including menus, selected or moving
units, range overlays, and every other FE8 UI element. Extended terrain and
standing units should remain visible outside it.

Mouse controls and independent panning are deliberately absent from this
renderer-only branch. Their current implementation and tests are preserved on
the Git branch `mouse`.

To time the real frame limiter while capturing, add `--realtime`. A 120-frame
capture should take approximately 2.0 seconds at the reported 59.728 fps.

## Verify macOS settings

Build the macOS `.app`, launch a map, and open **Settings → Settings…** (or
press Command-comma). The window exposes persistent Audio, VSync, and Extended
Renderer toggles, a Video Shader selector, and a capture button for every GBA
input. Click a binding,
confirm that it changes to `Press a key…`, press a replacement key, and reopen
the app to verify that the new binding persisted. Repeat with standalone left
and right Shift and Control keys. Escape should cancel capture. The same global
values must appear when Settings is opened from the library before a ROM is
launched. Toggling an option during gameplay should print a `Settings applied:`
line without restarting emulation.

Exercise all three video presets during the same running map. **Off** must
remain sharp and unfiltered. **CRT (TV Mode)** must add alternating scanlines
and a subtle horizontal blend. **Scanlines** must retain the alternating lines
without that blur. Each effect must cover the complete 480×320 image rather
than only the centered GBA frame; menus, units, and extended terrain must remain
visible. Reopen the app and confirm the selected preset persisted globally.
Startup should identify `backend=mGLES2` and log the selected shader name.

Open the macOS **State** menu and verify all four actions: Quick Save State (F5),
Quick Load State (F8), Save State As…, and Load State…. Test against a temporary
`--quick-state` path. Saving must create a nonempty mGBA state, and loading it
must return to the captured frame without restarting the process. File-picker
actions must accept `.ss` and `.ss1` states.

For audio diagnostics, exit after several seconds of gameplay. The final
`Audio summary:` should report a nonzero frame count and nonzero peak. Startup
must report `VSync=enabled` when its setting is selected.

## Sacred Echoes regression

Sacred Echoes EN v1.1.2 (`SHA-1 57b8adbdeae1eeef61d54c6715a55182d50d8a1b`)
uses the standard FE8 map work buffers but frequently leaves them resident
during dialogue and cutscenes. It must boot, load its cartridge save, produce
nonzero audio, remain canonical during non-map presentation, and automatically
activate the extension on a genuine tactical frame:

```sh
build/prototype/fe8-mgba-sdl.app/Contents/MacOS/fe8-mgba-sdl \
  --rom "/path/to/Fire Emblem Sacred Echoes.GBA" \
  --save "/path/to/Fire Emblem Sacred Echoes.sav"
```

The log should report `Extended renderer active` after entering player phase.
Opening dialogue should safely pause the enhancement when VRAM no longer
matches the map reconstruction, and returning to the map should resume it.
Camera movement must not toggle the enhancement; the compatibility gate tracks
camera-coordinate changes and uses separate activation and fallback streaks to
absorb transient PPU timing.
