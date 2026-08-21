#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build_dir=${BUILD_DIR:-"$repo_root/build-linux"}
build_type=${BUILD_TYPE:-RelWithDebInfo}
run_tests=${RUN_TESTS:-1}

for tool in cmake pkg-config; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        printf 'error: required build tool is missing: %s\n' "$tool" >&2
        exit 2
    fi
done

if ! pkg-config --exists sdl2; then
    cat >&2 <<'EOF'
error: SDL2 development files were not found by pkg-config.
On Ubuntu/Debian install: libsdl2-dev
For an offline build, set PKG_CONFIG_PATH to the bundled SDK's lib/pkgconfig directory.
EOF
    exit 2
fi

if [[ ! -f "$repo_root/third_party/mgba/CMakeLists.txt" ]]; then
    if command -v git >/dev/null 2>&1 && git -C "$repo_root" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        printf 'Initializing the pinned mGBA submodule...\n'
        git -C "$repo_root" submodule update --init --depth 1 third_party/mgba
    else
        cat >&2 <<'EOF'
error: the pinned mGBA source is missing at third_party/mgba.
Use a recursive checkout, run `git submodule update --init third_party/mgba`,
or unpack the CI source artifact that includes the submodule.
EOF
        exit 2
    fi
fi

cmake_args=(
    -S "$repo_root"
    -B "$build_dir"
    -DCMAKE_BUILD_TYPE="$build_type"
    -DFE8_BUILD_TESTS=ON
)
if command -v ninja >/dev/null 2>&1; then
    cmake_args+=( -G Ninja )
fi

cmake "${cmake_args[@]}"
cmake --build "$build_dir" --parallel "${BUILD_JOBS:-2}"

if [[ "$run_tests" != 0 ]]; then
    ctest --test-dir "$build_dir" --output-on-failure
fi

printf '\nBuilt: %s/fe8-mgba-sdl\n' "$build_dir"
printf 'Capture example:\n'
printf '  SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy %q --rom /path/to/game.gba --mute --capture frame.bmp --capture-after 600 --auto-continue\n' "$build_dir/fe8-mgba-sdl"
