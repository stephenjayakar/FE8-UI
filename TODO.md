# TODO

## ROM library and launcher

- [x] Accept `.gba` ROMs dropped onto the macOS library window.
- [x] Read the GBA header and content hash before adding an imported ROM.
- [x] Persist a multi-game list with title, path, game code, and progress status.
- [x] Launch or resume games through the libmGBA frontend.
- [x] Isolate cartridge saves and F5 quick states by ROM SHA-1.
- [x] Migrate a same-name `.sav` beside the ROM on first launch.
- [x] Remove games from the library without deleting ROMs or saves.
- [ ] Validate the GBA header checksum in addition to its size and metadata.
- Store a security-scoped bookmark or another durable macOS-safe reference;
  do not copy or redistribute ROM contents without an explicit user action.
- Add compatibility profile, cover/icon placeholder, and last-played metadata.
- Provide clear errors for moved, deleted, unreadable, or unsupported files.
- Add automated tests for import validation, persistence, duplicate ROMs,
  missing bookmarks, and launch argument construction.

## Compatibility

- [x] Provide Retina-correct mouse cursor movement, A/B clicks, and host panning
  on the `mouse` branch, with safe cancellation after rejected native input.
- Add versioned profiles for FE8-derived ROM hacks that relocate renderer data.
- Maintain regression checkpoints for retail FE8U and Sacred Echoes.
- Render transient units, effects, weather, and range overlays beyond the
  canonical 240×160 frame where compatible profiles make that safe.

## macOS application packaging

- Package the frontend as a signed `.app` with an application icon.
- [x] Add optional global CRT and scanline presets through mGBA's shader pipeline.
- Add per-game shader overrides.
- Add native Open Recent, multiple named state slots, controller configuration,
  and per-game settings. Quick save/load and state file pickers are implemented.
- [x] Add global bindable Speed Up, Quick Save, and Quick Load hotkeys.
- Add an About panel that exposes the MIT license and third-party notices.
