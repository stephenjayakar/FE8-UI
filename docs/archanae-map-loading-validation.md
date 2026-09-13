# Archanae missing-mountain validation

## Reproduced case

The supplied `Fire Emblem Archanea.GBA` has SHA-1
`220d1d6b5f56c9e25eb666a7f3dd7c486a759417`. Its opening chapter is a
34x15 map. Ordinary A/Start input reaches a stable player phase after 4,440
emulated frames, with cursor `(26,6)` and native camera `(240,0)`.

An empty renderer cache at this checkpoint reproduces the same mountain-shaped
black patch as the reported screenshot. This is the same map and defect rebuilt
from the ROM, not the user's exact save state or UI/window configuration.

## Controlled full-application comparison

Both Linux SDL/libmGBA binaries loaded the identical checkpoint. Neither run
received cursor input, auto-continue input, a zoom change, or a renderer toggle.
The patched binary used renderer commit `94f15ea6cc063078159421eac7c9463432798fc9`.
The baseline differed only in `extended_map_renderer.c`, whose Git blob was
verified to match pre-fix main exactly:
`87bf97ab14b3adf83ff98ff4dca3354c4f491ecb`.

| Capture frame | Original missing tiles | Fixed missing tiles | Cursor, both runs |
| --- | ---: | ---: | --- |
| 120 | 24 | 0 | 26,6 |
| 600 | 24 | 0 | 26,6 |

Each missing tile is 16x16: 6,144 terrain pixels were restored. The lossless
terrain captures are identical everywhere outside those original holes.
Both full-app logs report extended rendering active and 20 map sprites.

## Independent native-frame oracle

After the no-input comparison, a separate replay used ordinary Left/Up input
to bring the mountains into the native GBA viewport while keeping the cursor
above them. Native camera `(144,0)` exposes every previously missing pixel.
All **6,144 / 6,144** restored pixels match mGBA's actual framebuffer exactly,
without exclusions or a hand-painted expected image.

The optional `archanae_map_loading` integration test repeats this from boot.
It checks every stationary frame from 2 through 600, verifies that the learner
has not falsely confirmed the unseen bank, and hashes emulated RAM, VRAM, and
palette memory before/after each learning/render pass to detect writes. A later
native camera visit independently confirms source bank 4 maps to destination
bank 10, agreeing with the earlier provisional inference.

The same real-ROM regression linked against the exact pre-fix renderer fails:

```text
frame 2: 6144 / 6144 mountain pixels remain blank
Assertion `missing == 0' failed.
```

All **34 configured local CTest cases passed**, including the new real-ROM
case and the existing Archanae inventory/effective-stat integrations.

## Reproduce locally

```sh
cmake -S . -B build -DFE8_ARCHANAE_ROM="/path/to/Fire Emblem Archanea.GBA"
cmake --build build --parallel
ctest --test-dir build -R '^archanae_map_loading$' --output-on-failure

# Optional local checkpoint and raw PPMs; the output directory must exist.
build/tests/test_archanae_map_loading \
  "/path/to/Fire Emblem Archanea.GBA" /tmp/archanae-check

# Repeat with each original/fixed full-app binary and with frame 600.
xvfb-run -a env SDL_AUDIODRIVER=dummy build/fe8-mgba-sdl \
  --rom "/path/to/Fire Emblem Archanea.GBA" \
  --state /tmp/archanae-check.ss --mute \
  --capture /tmp/archanae-frame.bmp \
  --capture-terrain /tmp/archanae-terrain.bmp --capture-after 120
```

This verifies a cold renderer on a ready chapter-1 map. It is not an exhaustive
boot/fade, later-chapter, nonuniform-palette, or macOS-presentation validation.
An exploratory mid-intro checkpoint could leave the existing visual gate
inactive after the transition; that is separate from the reproduced missing
mountain tiles and was not repaired by this change. ROMs, states, memory dumps,
and game screenshots remain local and are not committed or uploaded to CI.
