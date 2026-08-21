# Testing FE8 Extended Frontend

These commands test the portable SDL2 command-line frontend and the native
macOS application. They do not rebuild or modify the ROM. Use a legally
obtained US FE8 ROM and copies of any cartridge saves.

## Linux workspace build and capture

On Debian or Ubuntu, install the development dependencies and build the pinned
mGBA core plus frontend:

```sh
sudo apt-get update
sudo apt-get install --yes \
  build-essential cmake ninja-build pkg-config libsdl2-dev zlib1g-dev xvfb
./scripts/bootstrap.sh
./scripts/test.sh -G Ninja
```

For a deterministic headless image, keep the ROM local and run the frontend
under Xvfb:

```sh
SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=software \
  xvfb-run -a ./build/fe8-mgba-sdl \
  --rom "/path/to/Fire Emblem The Sacred Stones.GBA" \
  --mute --auto-continue \
  --capture /tmp/fe8-frame.bmp --capture-after 120
```

The `Linux workspace build` GitHub Actions workflow performs the same configure,
build, and assertion-enabled unit-test sequence with GCC, repeats the release
suite with Clang, and runs the tests under AddressSanitizer and
UndefinedBehaviorSanitizer. It publishes a Linux executable, a source archive
with the pinned mGBA tree, and a small SDL2 development bundle for isolated
workspaces that cannot reach package mirrors. After extracting the latter two,
point `PKG_CONFIG_PATH` at the bundle before configuring:

```sh
export PKG_CONFIG_PATH=/path/to/fe8-linux-sdk/lib/pkgconfig
./scripts/test.sh -G Ninja
```

The SDL2 runtime library must still be installed on the target machine. ROMs,
saves, and states are never included in CI artifacts.

## macOS build and unit tests

From the repository root:

```sh
./scripts/bootstrap.sh
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/opt/homebrew \
  -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build -j4
ctest --test-dir build --output-on-failure
```

## Verify the macOS game library

Launch the app without command-line arguments:

```sh
open build/fe8-mgba-sdl.app
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
build/fe8-mgba-sdl.app/Contents/MacOS/fe8-mgba-sdl \
  --rom "/path/to/Fire Emblem The Sacred Stones.GBA" \
  --state "/path/to/map.ss"
```

The window starts maximized and scales the 480×320 logical canvas to the
largest 3:2 image that fits the display. Black bars are expected when the
display has a different aspect ratio.

Press Escape during gameplay and confirm the application remains open. Quit
with Command-Q or the window close button.

## Verify a large map and canonical UI

Given a save that can enter a tactical map, this finds a validated map larger
than the original 15×10 viewport and records a repeatable checkpoint:

```sh
build/fe8-mgba-sdl.app/Contents/MacOS/fe8-mgba-sdl \
  --rom "/path/to/Fire Emblem The Sacred Stones.GBA" \
  --save "/path/to/a-copy-of-save.sav" \
  --auto-continue --seek-large-map \
  --capture /tmp/fe8-large-map.bmp \
  --state-out /tmp/fe8-large-map.ss
```

Open the recorded checkpoint interactively:

```sh
build/fe8-mgba-sdl.app/Contents/MacOS/fe8-mgba-sdl \
  --rom "/path/to/Fire Emblem The Sacred Stones.GBA" \
  --state /tmp/fe8-large-map.ss
```

Success is reported as `extended=yes` with a map larger than 15×10. Use the
keyboard to select a unit and open its action menu. The original 240×160 mGBA
frame should remain intact and aligned with the extended viewport, including
menus, selected or moving units, range overlays, and every other FE8 UI element.
Extended terrain and standing units should remain visible outside it.

On the `mouse` branch, move the native pointer across several known map tiles
and compare each `Mouse move:` log's window, canvas, tile, and FE8 cursor values.
Left-click must reach the logged tile and print `Mouse confirm: A`; right-click
must print `Mouse right-click: B queued` and visibly cancel the current action.
Repeat on a Retina display and after resizing the window. Black letterbox areas
must not produce map clicks. During dialogue or menus, map movement and cursor
recovery must remain disabled; left/right click should still act as A/B.

Hold Shift and drag to pan the host viewport, then Shift-click a tile to
recenter it. The extended terrain should move without corrupting FE8's camera,
and the log should report the new map origin. Slow pointer motion should use
normal FE8 cursor steps and wait for both the logical cursor and its displayed
animation to arrive. If a step is repeatedly ignored, the path must cancel and
wait for fresh pointer motion; the frontend must not write emulated cursor state.

Mouse-driven travel should hold B for FE8's native fast-cursor behavior in both
idle and selected-unit path modes. Verify that a destination click releases B
before emitting A. While a long path is still moving, right-click and confirm
the forced B release/new press cancels the selection rather than being mistaken
for the already-held acceleration button.

In **Settings → Settings…**, verify **Enable mouse controls** defaults on. The
game canvas should replace the macOS arrow with the large blue-and-gold pointer,
while the Settings window itself retains the normal system pointer. Turning the
option off must immediately restore the system pointer, cancel pending movement,
and make map/UI clicks inert; turning it back on must restore mouse behavior and
the themed pointer. Relaunch once to verify the global preference persists.

To time the real frame limiter while capturing, add `--realtime`. A 120-frame
capture should take approximately 2.0 seconds at the reported 59.728 fps.

## Verify macOS settings

Build the macOS `.app`, launch a map, and open **Settings → Settings…** (or
press Command-comma). The window exposes persistent Audio, VSync, and Extended
Renderer toggles, a Video Shader selector, and a capture button for every GBA
input. Click a binding,
confirm that it changes to `Press a key…`, press a replacement key, and reopen
the app to verify that the new binding persisted. Repeat with standalone left
and right Shift and Control keys, including binding Select to Shift; modifier
capture must not crash. Escape should cancel capture. The same global
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

Verify the Hotkeys section from both the library and a running game. Its
defaults are Space for Speed Up, F5 for Quick Save, and F8 for Quick Load. Hold
Speed Up and confirm the log reports `Speed Up: on (4x)` and then `off` on
release; emulation should accelerate while sound is temporarily muted. Rebind
all three entries, restart the app, and confirm the new global values persist.
Quick Save and Quick Load must operate on the selected ROM's isolated state.

Open the macOS **State** menu and verify all four actions: Quick Save State,
Quick Load State, Save State As…, and Load State…. Test against a temporary
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
build/fe8-mgba-sdl.app/Contents/MacOS/fe8-mgba-sdl \
  --rom "/path/to/Fire Emblem Sacred Echoes.GBA" \
  --save "/path/to/Fire Emblem Sacred Echoes.sav"
```

The log should report `Extended renderer active` after entering player phase.
Opening dialogue should safely pause the enhancement when VRAM no longer
matches the map reconstruction, and returning to the map should resume it.
Camera movement must not toggle the enhancement; the compatibility gate tracks
camera-coordinate changes and uses separate activation and fallback streaks to
absorb transient PPU timing.

For pre-battle inventory testing, launch with `--mute` and a copy of the save,
enter Preparations, and press **I**. Move an item into an empty slot on another
unit, close the panel, and verify the native item screen reflects the move.
Reopen the panel, press **U**, and verify the original inventories return. The
panel must reject **I** during dialogue and ordinary tactical play. Never use
the original cartridge save for write-path testing.
