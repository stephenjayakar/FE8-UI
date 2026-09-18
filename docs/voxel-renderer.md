# Live voxel renderer (experimental)

> Updated scene behavior and drag controls: [Persistent 3D session](voxel-persistent-session.md).
> The older full-window fallback boundaries below are superseded by that audit.

This is an opt-in presentation mode in the native FE8 frontend, not the
standalone Voxel Lab scene. The loaded ROM and libmGBA remain authoritative.
There are no authored map coordinates, bundled game assets, model downloads,
API keys, or external inference services.

## Run

Build normally. On macOS, **Settings → Voxel Renderer** toggles the mode
without a shortcut; the Settings window also has **Enable voxel renderer**.
Under **Settings → Settings… → Hotkeys**, click **Voxel Renderer**, then press
**V** (or another key). **F7** is the default binding. Letter capture works in
the library before SDL video is initialized and in the running game. Bindings
are refreshed when returning from the separate library process. macOS remembers
the enabled mode and binding for subsequent launches; the portable Linux frontend
keeps these session-local.

Keep **Extended Renderer** enabled and return to an idle tactical map. The game
window title distinguishes **Voxels: live** from an enabled mode waiting for a
supported map/HUD, native selection/menu/combat, or a generation/allocation error.
Safety checks remain in place; enabling voxels does not hide unsupported UI.

On macOS the library startup flag is forwarded to Play and Resume:

```sh
sh build.sh && open build/fe8-mgba-sdl.app --args --voxel
```

Quit any older library/game processes before rebuilding and reopening.

Command-line launch also accepts:

```sh
./build/fe8-mgba-sdl --rom /path/to/game.gba --voxel
```

On macOS, the executable is inside
`build/fe8-mgba-sdl.app/Contents/MacOS/fe8-mgba-sdl`.

| Control | Action |
| --- | --- |
| Configured Voxel Renderer hotkey (F7 by default) | Toggle voxel/original presentation |
| [ / ] | Orbit left/right |
| Mouse wheel | Zoom (bounded) |
| Shift + left drag | Pan the presentation camera |
| C | Focus the real game cursor |
| Home | Restore the overview camera |
| Existing game bindings | Normal emulator input, unchanged |

The map cursor uses the inverse of the rendered camera. Clicking the raised
pixels of a unit selects its owning tile rather than the tile behind its head.
Screen-to-drawable conversion uses the existing high-DPI scaling path. The software scene
is bounded to 1920×1080 and scaled to the drawable; native HUD and text remain at
full drawable resolution. Retina/5K/6K displays no longer trip a 2160-pixel-height
rejection, and picking/panning continue to use drawable coordinates. Output
allocation is capped at 8192×4320 pixels (landscape or portrait), with either
dimension at most 16384. Geometry uses the same overlay backend as the native HUD;
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
state loads invalidate it. The software scene is bounded independently of output
size, as described above. Rendering is CPU-based and synchronous; cold rebuilds
can hitch.

## Validation

The activation/Retina fix was built and tested on Linux and native macOS before
publishing functional commit `892ae54434e20dd13c7aaf83ebc13adca98715b1`.

- **45/45 local Linux tests passed**, including the supplied Archanae and Sacred
  Echoes ROMs. The renderer test also passed AddressSanitizer and
  UndefinedBehaviorSanitizer, including the high-resolution allocation cases.
- **37/37 native macOS tests passed**, including real AppKit binding-button/key
  events before SDL video initialization and through the SDL event loop.
  The test checks V binding, saved preferences, conflict removal, cancellation,
  menu/checkbox state, and Library Play/Resume arguments. Preference storage is
  isolated from the user's settings. Native Settings screenshots were captured.
- The actual Linux application reproduced the old native-only fallback at
  **5120×2880**. The patched application rendered Archanae voxels at that size
  and Sacred Echoes at **3456×2234**. Actual keyboard events also checked voxel
  off/on, the disabled-Extended-Renderer status, restoration, quick-state loading,
  and normal application exit.

The real-ROM contract compares serialized emulator state byte-for-byte before
and after rendering and camera/cache operations; it also checks sprite/cache
use, visible tiles at four camera orientations, and regeneration. The synthetic
contract covers six orientations, raised-unit picking, transparency, palette
changes, cache reuse, resize, high-DPI picking/panning, and invalid camera inputs.

Letter-to-scancode lookup before SDL video initialization varies with SDL
version: the older Linux SDK returned Unknown, while the macOS CI version had a
default map. Binding now uses Cocoa's physical virtual-key positions directly
rather than depending on either behavior.

These checks are not a whole-application frame-rate measurement or a 60 fps
guarantee. Animation, terrain changes, camera movement and display upload add
cost. **The ROM gameplay captures are Linux captures.** macOS validation covers
the native application build, the renderer contracts, and actual Settings UI;
end-to-end macOS ROM gameplay and Windows runtime validation remain separate.

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

## Activation regression coverage

`test_macos_settings` exercises the actual AppKit binding button and key event
monitor both before SDL initialization (Library) and through `SDL_PollEvent`
(Game). It checks V capture, persistence in an isolated preferences suite,
Escape cancellation, conflict removal, binding refresh, checkbox/menu state,
scrollable control bounds, and Play/Resume argument construction. Passing an
output directory saves native Settings screenshots; no ROM is needed.

`test_voxel_renderer` covers Retina-sized, 5K, and portrait output, full-sized
buffers, bounded scene buffers, scaled picking/panning, and recovery from rejected
allocations. `test_voxel_presentation` checks all fallback states and restoration.
Real-ROM tests and captures remain local; ROMs and saves are never uploaded.
