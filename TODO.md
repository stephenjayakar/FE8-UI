# TODO

## Inventory manager

- [x] Add a paused, mouse-first host inventory for Preparations with instant
  cross-unit item swaps, empty-slot moves, and one-step undo.
- [x] Decode hack-specific character, class, and item names from the live ROM,
  render chibi portraits, expose item stats, and support the Supply convoy.
- [x] Make the manager available anywhere a valid FE8 roster exists, including
  native inventory-management contexts outside Preparations.

## ROM library and launcher

- [x] Accept `.gba` ROMs dropped onto the macOS library window.
- [x] Read the GBA header and content hash before adding an imported ROM.
- [x] Persist a multi-game list with title, path, game code, and progress status.
- [x] Launch or resume games through the libmGBA frontend.
- [x] Isolate cartridge saves and F5 quick states by ROM SHA-1.
- [x] Migrate a same-name `.sav` beside the ROM on first launch.
- [x] Remove games from the library without deleting ROMs or saves.
- [x] Validate the Nintendo logo, fixed byte, and header checksum; preserve
  non-fatal ROM-hack metadata discrepancies as visible warnings.
- [x] Show compatibility class and last-played metadata.
- [x] Provide clear import, missing-file, unreadable-file, changed-ROM, launch,
  save-migration, and child-process errors.
- [x] Allow a moved ROM to be reconnected only when its SHA-1 matches the
  original entry.
- [x] Retain a per-game launch log and surface a bounded tail on failure.
- Store a security-scoped bookmark or another durable macOS-safe reference;
  do not copy or redistribute ROM contents without an explicit user action.
- Add a cover/icon placeholder.
- Add automated tests for library persistence, duplicate-row updates, missing
  bookmarks, path relocation, and launch argument construction. Header parsing,
  SHA-1 identity, warning policy, and fatal validation have unit coverage.

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
- [x] Add an About panel that exposes the MIT license and bundled third-party
  notices.
