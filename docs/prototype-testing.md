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

## Open a known map

Use either a cartridge save (`.sav`) or an mGBA state (`.ss`/`.ss1`):

```sh
build/prototype/fe8-mgba-sdl \
  --rom "/path/to/Fire Emblem The Sacred Stones.GBA" \
  --state "/path/to/map.ss"
```

The window starts maximized and scales the 480×320 logical canvas to the
largest 3:2 image that fits the display. Black bars are expected when the
display has a different aspect ratio.

## Verify a large map and mouse pathing

Given a save that can enter a tactical map, this finds a validated map larger
than the original 15×10 viewport and records a repeatable checkpoint:

```sh
build/prototype/fe8-mgba-sdl \
  --rom "/path/to/Fire Emblem The Sacred Stones.GBA" \
  --save "/path/to/a-copy-of-save.sav" \
  --auto-continue --seek-large-map \
  --capture /tmp/fe8-large-map.bmp \
  --state-out /tmp/fe8-large-map.ss
```

Then exercise the same acknowledged D-pad controller used by a mouse click:

```sh
build/prototype/fe8-mgba-sdl \
  --rom "/path/to/Fire Emblem The Sacred Stones.GBA" \
  --state /tmp/fe8-large-map.ss \
  --mouse-click 18,6 \
  --capture /tmp/fe8-mouse-test.bmp \
  --capture-after 180
```

Success is reported as `extended=yes`, a map larger than 15×10, and a final
cursor position equal to the requested target. In the interactive window,
mouse movement moves the FE8 cursor, left-click moves and presses A, and
right-click presses B. Shift-click recenters the host view, Shift-drag pans it,
and `H` toggles the original FE8 HUD. Mouse input deliberately pauses while
FE8's input lock is active.

The startup `Display:` line is also a useful Retina check: the renderer output
may be twice the window size, but the reported window center should remain near
the logical canvas center, `240,160`.

To time the real frame limiter while capturing, add `--realtime`. A 120-frame
capture should take approximately 2.0 seconds at the reported 59.728 fps.
