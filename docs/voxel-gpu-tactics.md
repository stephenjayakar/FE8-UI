# GPU voxel rendering and in-scene tactics

## macOS option

In **Settings -> Settings... -> Voxel rendering**, choose **OpenGL (GPU)** or
**Software (CPU)**. OpenGL is the new default; existing voxel enable/key
preferences are unchanged. The selection persists across Library/game launches.
The game title and bottom status strip identify the backend actually in use.
Unsupported OpenGL contexts or failed GPU initialization use the software path.
A runtime GL draw failure logs the error and selects software for that session.

The pinned mGBA source and current upstream desktop backend use OpenGL; there is
no upstream native Metal backend to turn on. This implements a separate OpenGL
3.2 voxel rasterizer in the frontend, sharing the existing macOS context. It does
not replace the emulated GBA PPU or change the libmGBA version. On Linux, it can
share SDL's OpenGL renderer. Other SDL drivers retain the software voxel path.

Build normally with `sh build.sh` and relaunch the app after quitting old Library
and game processes. Keep Voxel Renderer and Extended Renderer enabled.
For direct executable launches **with a ROM**, the session override flags are:

```sh
build/fe8-mgba-sdl.app/Contents/MacOS/fe8-mgba-sdl \
  --rom /path/to/game.gba --voxel-gpu
# Or: --voxel-software
```

Use the saved Settings choice when launching through Library. The existing
`open build/fe8-mgba-sdl.app --args --voxel` still enables the saved backend;
backend-specific flags above are for direct game launches, not Library argv.

## What now runs on the GPU

The earlier path uploaded a CPU-rasterized scene. This path uploads cached
**world-space triangles**, the live ground texture, and a compact billboard
atlas. OpenGL performs camera transformation, triangle rasterization, depth
testing, texture sampling, alpha cutouts, and shadow blending. Orbiting changes
the camera matrix, not the scenery vertex buffer. Each unit image is one textured
quad, not hundreds of pixel boxes. Unchanged meshes/textures remain resident.

Building classes, palettes, footprint ownership, ground animation, lighting and
shadows still come from the same read-only scene generator. The 1920x1080 maximum
scene framebuffer is scaled to the drawable on the GPU. HUD and text remain at
native drawable resolution. Transparent sprite pixels do not write depth. Picking
uses the same projection, live sprite alpha, and cached triangles on the CPU;
there is no per-frame GPU readback or render-thread synchronization for picking.
Explicit screenshots read the actual presented GPU framebuffer, including HUD.

CPU work remains: emulation, validated memory/PPU decoding, scene classification
on changes, HUD extraction, and dynamic packet preparation. This is not an
all-GPU emulator, a Metal implementation, or a guaranteed M1 frame rate.

## Selection, movement, and action menus stay in 3D

Selecting an allied unit, previewing movement/range tiles, confirming a reachable
destination, walking there, and opening/cancelling the action menu no longer
switch to the native 2D camera. The ROM still decides legal movement and drives
all animation and input. Movement/range outlines are projected onto the map.
The actual native action menu, including its hand cursor and original text, is
composited beside the projected destination instead of a 240x160 camera position.
The wheel moves the native action-menu selection; left/right click retain A/B.
A committed walk ignores new world-hover/click paths until its animation ends.

FE8 hides the standing unit while a moving-unit (MU) process owns its image.
The renderer validates the active Unit, MU, reciprocal configuration slot, and
AP animation descriptor, then reads its current position, OBJ tiles and palette.
This also works outside the original GBA viewport. There are no hard-coded
chapter/unit coordinates or guessed synthetic movement. Native hidden/dead,
rescued, fog-hidden, other-faction and unsupported animation cases are rejected.

Cancellation briefly uses a scripted return MU without a Unit pointer. That is
accepted only through the previously verified owner plus the same script/AP and
reciprocal config chain. If AP resets before the PPU retires the sprite, only an
exact matching live OAM image at that owner's position is allowed for the handoff.
Wait has a similar one-frame handoff before the grey standing handle appears;
it requires the same acted unit and committed unit-map position. No old
framebuffer is frozen and no hidden-state bits are cleared.

The tactical HUD extractor recognizes selected/range frames and opening/closing
native action menus. It continues comparing the current extracted PPU layers
against the actual emulator frame. Unknown UI, unsupported OBJ/animation formats,
fades, dialogue, combat, enemy phase and inventory retain native fallback.
This patch does not implement a complete 3D battle renderer, all special skills,
or arbitrary ROM-hack menus. The original 2D HUD extraction path is unchanged.

## Validation and profiling

- `test_voxel_renderer`: GPU packet/cache/atlas changes, camera independence of
  static geometry, alpha-aware picking, CPU/GPU switching, native active-owner
  chains and rejected dead/hidden/rescued/unsupported actors.
- `test_video_scene`: actual platform OpenGL pixel readback for geometry/depth,
  alpha holes, cached frames, changed uploads, resizing, native/CPU/GPU switching,
  invalid-frame recovery, and HUD composition. Linux requires a graphical session
  (run under Xvfb); macOS uses the existing native OpenGL backend.
- `test_native_hud`: selection, handless opening/closing menus, immutable input,
  and rejection of unknown UI, mismatched frames and effects.
- `test_macos_settings`: actual Cocoa OpenGL/software selection and persistence,
  in addition to the existing V-binding and Library tests.
- `test_voxel_interaction_rom`: every-frame selection, movement preview, committed
  walking, destination, action menu and cancellation on a user-supplied ROM.
  Both CPU and GPU builders must remain live and account for all actors. A fixed
  world landmark must not move, and rendering must leave serialized emulation
  state byte-identical. Set FE8_ARCHANAE_ROM / FE8_SACRED_ECHOES_ROM locally.

The optional benchmark now accepts a final `software` or `gpu` argument:

```sh
build/tests/bench_voxel game.gba state.ss 3456 2234 240 orbit comparison software
build/tests/bench_voxel game.gba state.ss 3456 2234 240 orbit comparison gpu
```

This reports **CPU raster time versus GPU packet-preparation time**, NOT GPU
completion or total client frame rate. Emulation, HUD, uploads, GPU rasterization,
vsync and pacing are excluded. Compare equal ROM/state/mode/dimensions/builds.
The application's `--perf-stats` separately reports all client stages, including
GPU submission/presentation. Local llvmpipe timings are not M1 GPU measurements.

No ROMs, saves, extracted graphics, fonts, or credentials are committed. Gameplay
screenshots shared in conversation are actual Linux OpenGL-client captures.
The included macOS CI recipe covers the native build, Cocoa controls, and
actual OpenGL compositor. It has not been run for this local patch; neither
macOS gameplay nor M1 performance is claimed.
