# Linux workspace build and capture

The frontend has a non-Apple SDL video backend, but Linux requires the SDL2 and zlib development packages plus the pinned mGBA submodule. The macOS Cocoa menu, CoreText host text, library picker, and settings integration are replaced by the existing non-Apple stubs; emulation, the extended renderer, input, audio, and deterministic frame capture remain available.

## Ubuntu or Debian prerequisites

```sh
sudo apt-get update
sudo apt-get install --yes --no-install-recommends \
  build-essential cmake ninja-build pkg-config libsdl2-dev zlib1g-dev
```

Use a recursive checkout, or initialize only the required dependency:

```sh
git submodule update --init --depth 1 third_party/mgba
```

Then run the checked-in workspace build:

```sh
./scripts/build-linux-workspace.sh
```

That configures a `RelWithDebInfo` build, compiles the frontend and pinned mGBA core, and runs the complete CTest suite. Set `BUILD_DIR`, `BUILD_TYPE`, `BUILD_JOBS`, or `RUN_TESTS=0` to override the defaults.

## Offline or isolated workspace

The `Linux workspace build` GitHub Actions workflow publishes two useful artifacts in addition to the executable:

- `fe8-ui-source-with-mgba` contains the frontend source and the exact pinned mGBA checkout, so no Git access is required.
- `fe8-linux-sdk` contains relocatable SDL2 development headers, the link-time library, and `sdl2.pc`.

After unpacking both artifacts, point pkg-config at the SDK and run the same script:

```sh
export PKG_CONFIG_PATH="$PWD/fe8-linux-sdk/lib/pkgconfig"
./scripts/build-linux-workspace.sh
```

The target system still needs the normal SDL2 runtime library when launching the executable.

## Run and capture a frame

A desktop launch uses SDL normally:

```sh
./build-linux/fe8-mgba-sdl --rom /path/to/game.gba --mute
```

For a deterministic headless frame on a machine without X11 or Wayland:

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  ./build-linux/fe8-mgba-sdl \
  --rom /path/to/game.gba \
  --mute \
  --capture frame.bmp \
  --capture-after 600 \
  --auto-continue
```

If the installed SDL build does not provide the dummy video driver, run the same command under `xvfb-run -a` instead. Captures are written as uncompressed BMP files and can be converted losslessly to PNG with any standard image tool.

Do not add ROM images, save data, or captured proprietary assets to the repository or CI artifacts.

## Validated configuration

The branch workflow and isolated-workspace validation use Ubuntu 24.04, GCC, CMake, Ninja, SDL2, zlib, and mGBA revision `afd6f14eaf8bd35214ed3fb9dc69a92bfc3877a9`. The validation includes a clean configure/build, all frontend unit tests, command-line error handling, duplicate deterministic captures, long-running captures, and AddressSanitizer/UndefinedBehaviorSanitizer builds.
