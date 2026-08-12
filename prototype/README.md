# FE8 embedded mGBA + SDL2 prototype

This directory contains the initial desktop shell for running the Fire Emblem: The Sacred Stones GBA ROM through the public mGBA core API. It builds the mGBA core from the pinned `0.10.3` source revision and supplies a small SDL2 frontend.

## Requirements

- CMake 3.20 or newer
- A C11 compiler
- SDL2 development files
- A legally obtained FE8 ROM (`fireemblem8.gba` or another `.gba` path)
- Git, for CMake's `FetchContent` download

On Apple Silicon, install the native arm64 SDL2 package (for example, Homebrew's `sdl2`) and configure with `-DCMAKE_OSX_ARCHITECTURES=arm64` if the architecture is not already inferred by the toolchain.

## Configure and build

From the repository root:

```sh
cmake -S prototype -B build/prototype \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build/prototype --target fe8-mgba-sdl
```

If SDL2 is installed outside the default prefix, add its prefix while configuring, for example:

```sh
cmake -S prototype -B build/prototype \
  -DCMAKE_PREFIX_PATH=/opt/homebrew \
  -DCMAKE_OSX_ARCHITECTURES=arm64
```

The first configure downloads mGBA at commit `1c61b54208ca6266129d0f2394c04bd8c44f98c5` (the 0.10.3 release commit). The mGBA build is restricted to the static GBA core; the SDL2 frontend in `src/main.c` is intentionally owned by this prototype.

## Run

```sh
build/prototype/fe8-mgba-sdl \
  --rom /path/to/fireemblem8.gba \
  [--state /path/to/state.ss] \
  [--save /path/to/fireemblem8.sav]
```

`--rom` is required. `--state` restores an mGBA named state after the ROM is reset. `--save` opens the cartridge save file through mGBA's save-data API before reset, so SRAM/Flash/EEPROM-backed progress can be used by the emulation.

The initial window is 480x320 and remains resizable. The exact 240x160 GBA framebuffer is uploaded with nearest-neighbor scaling and centered against a black background; smaller window sizes keep a one-timescale image. Escape or the window close button exits.

Keyboard bindings:

| GBA input | Keyboard |
| --- | --- |
| A / B | Z / X |
| D-pad | Arrow keys |
| Start / Select | Return / Backspace |
| L / R | A / S |

## Renderer hook

`present_frame()` is the deliberate exterior-renderer seam. It receives a stable RGBA conversion of the exact mGBA framebuffer and currently performs the SDL texture upload and centered presentation. An external renderer can replace that function without changing ROM loading, state restore, input, or frame scheduling.

The core is configured with `setVideoBuffer()` and queried through `desiredVideoDimensions()` rather than relying on private mGBA internals. `color_t` is converted at this boundary so the SDL texture format remains explicit and portable across mGBA's 16-bit and 32-bit framebuffer builds.
