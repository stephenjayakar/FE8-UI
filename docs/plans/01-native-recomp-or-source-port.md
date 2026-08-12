# Plan 1: genuine native recomp or source port

## Goal

Produce a real macOS build in which FE8 gameplay code executes as native
arm64/x86-64 instructions, without an emulator as the primary runtime.

Two routes qualify:

1. Ahead-of-time translate the retail ROM's ARM/Thumb code and bind it to a GBA
   compatibility runtime.
2. Compile reconstructed C from this repository with Clang and translate or
   replace the remaining assembly.

The source-port route is easier to debug; the static-recomp route more directly
preserves the exact retail binary. Both still require a substantial GBA hardware
compatibility layer.

## Architecture

- Host-compiled FE8 systems and event code.
- Address-space adapter for ROM pointers, EWRAM, IWRAM, VRAM, palettes, OAM, and
  memory-mapped registers.
- Replacement scheduler for VBlank, HBlank, timers, DMA, interrupts, and BIOS
  calls.
- Renderer translating GBA backgrounds, windows, sprites, blending, and palettes
  to Metal or SDL.
- Audio implementation for Direct Sound/FIFO and legacy PSG channels.
- Native filesystem saves, controller input, mouse input, menus, and packaging.
- A compatibility escape hatch for untranslated assembly during migration, if a
  hybrid transition is acceptable.

## Work plan

### Phase 0: reproducible baseline

- Rebuild the matching FE8U ROM from the decomp and verify its hash.
- Produce symbol maps, relocation metadata, and an inventory of all assembly.
- Build deterministic replay tests from known save states and input logs.

### Phase 1: host address model

- Reserve or translate the GBA address ranges.
- Replace integer-to-pointer assumptions with explicit guest-address helpers.
- Audit function pointers, event-script pointers, packed structs, overlays, and
  linker-defined data.
- Implement endian-safe unaligned loads with GBA semantics where required.

### Phase 2: hardware runtime

- Implement BIOS calls, DMA, timers, interrupt dispatch, keypad state, SRAM/Flash,
  and frame pacing.
- Initially reuse a proven software GBA PPU/APU implementation to reduce risk,
  while keeping game code host-compiled.
- Establish frame-by-frame comparisons against mGBA.

### Phase 3: translate game code

- Compile reconstructed C one subsystem at a time.
- Translate handwritten ARM/Thumb routines or wrap them behind tested C
  replacements.
- Preserve exact data layouts and validate behavior with replay traces.

### Phase 4: native extensions

- Replace the fixed viewport assumptions in map rendering and UI layout.
- Add mouse hit-testing at the map-command layer.
- Add native menus, save management, resolution settings, and Metal output.

### Phase 5: packaging and verification

- Build universal macOS binaries, sign/notarize the app, and keep copyrighted ROM
  data out of source distributions.
- Regression-test every chapter, route, battle animation, event, save type, and
  suspend/resume path.

## Extended rendering and mouse controls

Because the game code is available to modify, map drawing can eventually render
directly into a larger host surface. Cursor coordinates can be updated through
the native version of FE8's own cursor routines, with camera and UI code taught
about the larger viewport. This gives the cleanest final result but touches many
fixed-screen assumptions.

## Main risks

- Reconstructed source is not designed as portable application code.
- Fixed guest addresses and ROM pointers are pervasive.
- Exact PPU timing and event ordering affect gameplay and presentation.
- Remaining assembly and self-referential data complicate translation.
- A source-port-derived executable will not automatically support arbitrary ROM
  hacks; hacks must be rebuilt against the port or translated separately.

## Estimate

This is a multi-person, multi-year-quality effort for broad game correctness. A
narrow proof of concept could appear in months, but “plays the whole game like
the ROM” is the expensive part. It is the only plan here that should be called a
true recompilation or native source port.

