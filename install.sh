#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
build_dir="$repo_dir/build"
use_sudo=auto

case "$(uname -s)" in
    Darwin)
        prefix=/Applications
        ;;
    *)
        prefix=/usr/local
        ;;
esac

usage() {
    cat <<EOF
usage: $0 [--prefix PATH] [--no-sudo] [--] [cmake configure options...]

Build and install FE8 Extended Frontend. The default install location is
/Applications on macOS and /usr/local on other platforms.

  --prefix PATH  Install below PATH instead of the platform default.
  --no-sudo      Do not use sudo when the install location is not writable.
  -h, --help     Show this help text.

Arguments after -- are passed to CMake while configuring.
EOF
}

cmake_args=()
while (($#)); do
    case "$1" in
        --prefix)
            if (($# < 2)); then
                echo "error: --prefix requires a path" >&2
                exit 2
            fi
            prefix=$2
            shift 2
            ;;
        --no-sudo)
            use_sudo=false
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        --)
            shift
            cmake_args=("$@")
            break
            ;;
        *)
            echo "error: unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

"$repo_dir/scripts/bootstrap.sh"

if ((${#cmake_args[@]})); then
    cmake -S "$repo_dir" -B "$build_dir" \
        "${cmake_args[@]}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DFE8_BUILD_TESTS=OFF \
        -DCMAKE_INSTALL_PREFIX="$prefix"
else
    cmake -S "$repo_dir" -B "$build_dir" \
        -DCMAKE_BUILD_TYPE=Release \
        -DFE8_BUILD_TESTS=OFF \
        -DCMAKE_INSTALL_PREFIX="$prefix"
fi
cmake --build "$build_dir" --target fe8-mgba-sdl

installed_with_sudo=false
if [[ -w "$prefix" ]] || [[ ! -e "$prefix" && -w "$(dirname "$prefix")" ]]; then
    cmake --install "$build_dir"
elif [[ "$use_sudo" == auto ]] && command -v sudo >/dev/null 2>&1; then
    sudo cmake --install "$build_dir"
    installed_with_sudo=true
else
    echo "error: install location is not writable: $prefix" >&2
    echo "Retry with --prefix PATH (for example, --prefix \"$HOME/.local\") or allow sudo." >&2
    exit 1
fi

if [[ "$(uname -s)" == Darwin ]]; then
    app_path="$prefix/fe8-mgba-sdl.app"
    if $installed_with_sudo; then
        sudo /usr/bin/codesign --force --sign - "$app_path"
    else
        /usr/bin/codesign --force --sign - "$app_path"
    fi
fi
