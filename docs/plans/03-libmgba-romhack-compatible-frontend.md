# Plan 3: ROM-hack-compatible embedded-libmGBA frontend

## Goal

Keep the reliable execution model from Plan 2, but make enhancements usable with
FE8U ROM hacks. Every ROM continues to run like a normal game through mGBA. The
large-map renderer and mouse input activate only when a compatibility profile can
locate and validate the required FE8 structures.

This is the architecture used by the current prototype.

## Core rule

Enhancements are optional and fail closed:

- mGBA is always authoritative for execution, saves, timing, audio, and the
  original framebuffer.
- The extension reads game memory; it does not patch ROM code or teleport the
  cursor.
- If detection is uncertain, the app shows the ordinary mGBA frame and normal
  controls rather than risking corruption.

## Compatibility layers

### Tier A: exact profile

A hash identifies a known ROM and supplies exact addresses. This is the safest
mode and the initial target for retail FE8U plus specifically tested hacks.

### Tier B: structural profile

The ROM is recognized as FE8U-derived, and a versioned profile describes symbols
that may have moved. Profiles may be shipped as data files so hack maintainers
can contribute support without rebuilding the frontend.

### Tier C: conservative discovery

Optional signatures locate selected structures, followed by strict runtime
validation: plausible dimensions, GBA-region pointers, valid row tables, stable
camera/cursor coordinates, and renderable tiles. Discovery must never be the only
evidence for a write. The initial prototype need not enable this tier.

### Fallback

Unknown or invalid ROMs run as an ordinary embedded-mGBA session. Extended pixels
and mouse pathing are disabled, with a visible compatibility reason.

## Profile schema

Each profile should record:

- identity: SHA-1/SHA-256, game code, revision, title, and profile version;
- addresses: map state, dimensions, row-pointer globals, tileset config, VRAM and
  palette bases, cursor, camera, and optional unit/sprite data;
- validation ranges and feature flags;
- known patch families or hacks tested against the profile;
- provenance and frontend version constraints.

Profiles contain no ROM data. User ROMs, patches, saves, and save states remain
outside the project.

## Work plan

### Phase 1: safe libmGBA baseline

- Build a native SDL2/Cocoa shell around a pinned libmGBA release.
- Support `--rom`, `--save`, and `--state`; preserve ordinary keyboard/controller
  play and the exact centered 240×160 image.
- Verify retail FE8U and at least one supplied FE8-derived hack boot normally.

### Phase 2: profile and diagnostics

- Implement callback-based guest-memory reads independent of mGBA internals.
- Add the verified retail FE8U profile and strict pointer/bounds validation.
- Report profile match, current map state, and why enhancements are inactive.
- Add synthetic tests for valid, malformed, and relocated structures.

### Phase 3: terrain extension

- Decode logical metatiles, four 8×8 subtiles, flip bits, 4bpp graphics, palette
  banks, and fog into a host RGBA surface.
- Center the 240×160 mGBA frame over a 480×320 terrain canvas.
- Render only during a validated tactical-map state; otherwise letterbox normally.
- Add pixel-diff diagnostics for the reconstructed center seam.

### Phase 4: mouse pathing

- Hit-test the whole extended canvas using emulated camera coordinates.
- Maintain a target tile and send paced D-pad taps through libmGBA.
- Observe the real cursor after every tap; re-plan or cancel if the game rejects
  movement, opens a menu, scrolls, or starts an event.
- Send `A` only for an explicit click. Never write cursor coordinates directly;
  cancel and wait for fresh pointer motion after repeatedly rejected input.

### Phase 5: richer exterior composition

- Add map sprites and the player cursor using profile-gated readers.
- Add movement/range/fog overlays when their row maps validate.
- Handle map animations and weather incrementally.
- Keep unsupported layers absent outside the center; never approximate gameplay
  information in a misleading way.

### Phase 6: hack profiles and tooling

- Generate a ROM fingerprint report without uploading or copying ROM contents.
- Provide a profile test harness driven by user-owned ROMs and save states.
- Test hacks that relocate data, expand tables, change tilesets, or use Skill
  System conventions.
- Document a contributor workflow for adding profiles and regression fixtures.

### Phase 7: distribution

- Package a universal signed/notarized `.app` without ROMs, BIOS files, patches,
  or copyrighted game assets.
- Store saves per ROM hash to avoid collisions between hacks.
- Expose a compatibility panel and a one-click “run without enhancements” mode.

## What “works with ROM hacks” means

- Every valid GBA ROM supported by mGBA can run in fallback mode.
- A hack receives extended rendering only when its memory layout matches a tested
  profile or passes conservative discovery and validation.
- Gameplay modifications remain the hack's responsibility; the frontend does not
  assume chapter data, units, classes, terrain IDs, or event content match retail.
- Patches that replace the map renderer or radically change tileset storage may
  require a custom decoder plug-in, not just new addresses.

## Prototype acceptance criteria

1. Retail FE8U loads from an external path and an mGBA state reaches a map.
2. A 480×320 window shows authentic terrain beyond the centered 240×160 frame on
   a map larger than the hardware viewport.
3. Clicking an exterior tile moves the real FE8 cursor via visible D-pad steps.
4. At least one FE8U ROM hack boots; unsupported enhancement state is clear and
   ordinary play remains intact.
5. Invalid pointers or an unknown ROM cannot cause guest-memory writes or crashes.

## Estimate

The safe baseline and retail terrain prototype are days-to-weeks. Mouse pathing
and one or two exact hack profiles add weeks. Broad compatibility is an ongoing
profile-and-regression effort, while complete exterior reproduction of every
custom engine effect may be impossible without hack-specific adapters.
