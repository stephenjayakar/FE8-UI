# Voxel performance and camera-facing units

The `voxel-live` renderer now keeps buildings and trees volumetric, but draws
units as **flat camera-facing billboards** using the running ROM's standing
sprite pixels. The feet stay on their map tile; the image stays upright and
front-facing when orbiting. Palette colors, alpha cutouts, standing animation,
native hidden flags, terrain depth occlusion, and unit click targets are retained.
There is no per-pixel cube extrusion for units. This supersedes the sprite-relief
description of the original voxel prototype.

## Performance changes

- The interactive renderer returns only the bounded scene (at most 1920x1080).
  OpenGL on macOS, or SDL's accelerated renderer elsewhere, scales that texture
  and composites the separate full-drawable-resolution HUD. No full-Retina scene
  allocation, CPU enlargement, or full-screen CPU alpha composition happens in
  the interactive loop. Explicit screenshots still expand/composite on demand.
- Scenery and its depth are retained through ground-only animation. An atlas of
  the visible ground's projection updates water/plains without rasterizing every
  tree and building again. Object-footprint pixels, terrain semantics, chapter,
  fog, and map dimensions invalidate scenery whenever its appearance may change.
- Identical scene frames reuse the completed raster while still checking live
  ROM graphics and palettes. Sprite animation, cursor/ranges, camera changes,
  resize, and state invalidation redraw the appropriate content.
- Substrate selection and ground lighting are computed in map space, not once
  per screen pixel during camera movement. Sprite decoding resolves a palette
  once and reads each packed pair of pixels once.
- The presenter skips uploading/drawing the hidden GBA canvas and its CRT passes
  while the voxel scene covers the display. Native fallback retains that path.

The underlying terrain rasterizer is still CPU-based. Cold map generation and
continuous orbiting can cost more than a stationary scene. This is not a claim
of a full GPU 3D renderer, nor a guaranteed 60 fps on every M1 configuration.
No emulation frames are dropped or gameplay data changed by these optimizations.

## Reproduce measurements

Configure/build tests with the normal CMake build. A local ROM and a compatible
emulator state are required for the optional renderer benchmark:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DFE8_BUILD_TESTS=ON
cmake --build build --target bench_voxel
build/tests/bench_voxel game.gba tactical-map.ss 3456 2234 120 animated current
```

Use `static`, `animated`, or `orbit` as the mode. Output reports cold time, mean,
median and 95th percentile renderer CPU milliseconds, plus cache counters. The
benchmark excludes emulation, native HUD extraction, upload/swap, and pacing.
A before/after comparison must use the same ROM, state, dimensions, mode, frame
count, and build configuration. `--perf-stats` in the application separately
reports cumulative stage time and whole-client throughput; it now includes the
voxel stage rather than leaving that cost unattributed.

## Regression coverage

`test_voxel_renderer` checks camera-facing aspect/foot anchoring, unlit palette,
click targeting, native animation/hidden sprites, high-DPI coordinates, zero CPU
upscales on the interactive path, immutable cached scenes across captures,
ground-animation cache output against forced full regeneration, changed building
materials, and the full 128-unit compatibility path. Existing ROM tests verify
that rendering does not mutate serialized emulator state.

`test_video_scene` exercises the **actual platform presenter** with synthetic
pixels, reading back immediately before swap. It checks scene orientation,
nearest-neighbor scaling, straight-alpha HUD blending, resizing textures/windows,
and repeated switching between native and scene presentation. On headless Linux
run it under Xvfb; macOS uses its native OpenGL backend. It needs no ROM and can
optionally save a PPM by taking an output filename.

The existing activation/binding fixes remain. The configured Voxel Renderer
hotkey and Settings toggle still control the mode. Native menus, movement,
combat, dialogue and unsupported scenes retain the original fallback boundary.
