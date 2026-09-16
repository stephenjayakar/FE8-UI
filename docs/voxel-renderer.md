# Live voxel renderer (experimental)

This is an opt-in presentation mode in the native FE8 frontend, not the
standalone Voxel Lab scene. The loaded ROM and libmGBA remain authoritative.
There are no authored map coordinates, bundled game assets, model downloads,
API keys, or external inference services.

## Run

Build normally, launch a supported ROM, enable the extended renderer, and press
**F7** on an idle tactical map. On keyboards that reserve function keys, use
**Fn-F7**. The toggle is session-local and defaults off. Custom game/hotkey
bindings take priority over the prototype shortcuts.

Command-line launch also accepts:

```sh
./build/fe8-mgba-sdl --rom /path/to/game.gba --voxel
```

On macOS, the executable is inside
`build/fe8-mgba-sdl.app/Contents/MacOS/fe8-mgba-sdl`.

| Control | Action |
| --- | --- |
| F7 | Toggle voxel/original presentation |
| [ / ] | Orbit left/right |
| Mouse wheel | Zoom (bounded) |
| Shift + left drag | Pan the presentation camera |
| C | Focus the real game cursor |
| Home | Restore the overview camera |
| Existing game bindings | Normal emulator input, unchanged |

The map cursor uses the inverse of the rendered camera. Clicking the raised
pixels of a unit selects its owning tile rather than the tile behind its head.
Screen-to-drawable conversion uses the existing high-DPI scaling path. Geometry
is rendered into the same full-resolution overlay backend as the native HUD;
there is no second window or browser bridge.

## What is generated on the fly

**Units:** the current standing map-sprite list supplies positions, tile indices,
shapes and palette banks. Native hidden-sprite flags are honored. The renderer decodes 4bpp OBJ VRAM and the ROM's live
RGB555 palette, omits transparent pixels, and generates rounded voxel relief
from alpha-distance thickness. Animation and palette changes invalidate the
corresponding cached columns. Thin weapons stay thin; thicker silhouettes gain
more depth. Units are not substituted with generic class miniatures.

**Buildings and trees:** the existing terrain decoder supplies the original map
texture, and the live terrain grid supplies object types and approximate
footprints. A procedural grammar makes stepped roofs, timber frames, masonry,
crenellations, trunks and blocky canopies. Materials are sampled from the ROM's
current map palette. There are no chapter-specific object placements. FE8-style
terrain semantics and the `0x2C`/`0x2E` footprint conventions observed in these
ROMs are heuristic inputs, not a universal object-segmentation model.

**Ground:** the original decoded map stays flat. Where a lifted object would
otherwise have a flat duplicate, its substrate is inferred from nearby plains.
Unknown terrain types remain textured ground. Directional face lighting,
contact shadows, depth testing and a thin diorama edge make the volume legible.

This is local procedural generation, **not AI image-to-3D reconstruction**. The
ROM does not provide the hidden sides of a building or character. The inferred
depth is not recovered original geometry. In particular, units are voxelized
sprite reliefs, not the hand-sculpted, fully volumetric characters from the
standalone concept. Unusual terrain IDs, adjacent roofs and nonstandard palette
art may need a profile override or authored asset to look correct.

## Safe integration boundary

The mode activates only while the existing visual-profile and native-HUD gates
accept an idle tactical view. Selection/movement ranges, action menus, dialogue,
combat, transitions, inventory, unsupported HUD layouts and allocation failures
use the original presentation. Returning to an eligible map automatically
restores the voxel view and its camera. **This is not yet an all-scenes 3D
replacement:** the active moving-unit (MU) renderer and animated native effects
still require a separate implementation.

The renderer accepts only a read callback and immutable snapshots. It does not
write emulated RAM, ROM, saves or input state. Actual clicks still travel through
the existing D-pad/A/B controller, rather than teleporting units or writing the
cursor. Camera changes, state loads and presentation transitions clear stale
pointer targets. Normal emulator saves and Armory behavior remain unchanged.

Terrain generation is invalidated by map dimensions, chapter, metatiles,
terrain/fog data, tile graphics, palettes and learned palette mapping. A
128-entry, content-verified LRU cache stores sprite columns. Repeated identical
frames reuse the static terrain/depth backdrop. ROM changes destroy the view;
state loads invalidate it. Output is bounded to 4096 x 2160; larger drawables
currently fall back to the original renderer rather than allocate unbounded
buffers. Rendering is CPU-based and synchronous; cold rebuilds can hitch.

## Validation

Validated locally with the supplied Archanae and Sacred Echoes ROMs on Linux
using the actual SDL frontend. Both were booted into tactical maps; screenshots
were captured from the live application at 1440 x 900. F7, orbit, zoom, native
selection/cancel, restoration and game-directional input were exercised through
X11 events, not a simulated HTML scene.

The configured test suite passed **44/44 tests**, including both real-ROM voxel
contracts and the ROM-independent renderer test. AddressSanitizer and
UndefinedBehaviorSanitizer passed the renderer test. The real-ROM contract
compares serialized emulator state byte-for-byte before and after rendering and
camera/cache operations; it also checks native sprite/cache use, all visible
tiles at four camera orientations, invalid dimensions and regeneration. The
synthetic contract covers six orientations, raised-unit picking, transparency,
palette changes, cache reuse, resize and invalid camera inputs.

Measured renderer-only CPU time in this workspace, 1440 x 960, frozen emulator
state, 30 identical warm frames:

| ROM | First render | Warm mean | Live standing sprites |
| --- | ---: | ---: | ---: |
| Archanae | 41.09 ms | 3.18 ms | 21 |
| Sacred Echoes | 46.07 ms | 2.22 ms | 12 |

These are not whole-application frame-rate measurements or a 60 fps guarantee.
Animation, terrain changes, camera movement and display upload have additional
cost. macOS and Windows use the shared C/overlay implementation but were not
runtime-tested here.

To reproduce with your own local ROMs:

```sh
cmake -S . -B build -G Ninja \
  -DFE8_ARCHANAE_ROM=/path/to/Archanae.gba \
  -DFE8_SACRED_ECHOES_ROM=/path/to/SacredEchoes.gba
cmake --build build
ctest --test-dir build --output-on-failure

# Optional local state and decoded-frame captures; never commit these outputs.
./build/tests/test_voxel_rom /path/to/Archanae.gba /tmp/archanae-voxel
```

No ROMs, saves, decoded textures, or screenshots containing ROM assets are
included in the source branch. Screenshots shared in the conversation are
validation artifacts, not bundled game assets.
