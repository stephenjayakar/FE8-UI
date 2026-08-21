#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
with_reference=false

for argument in "$@"; do
    case "$argument" in
        --with-reference)
            with_reference=true
            ;;
        *)
            echo "usage: $0 [--with-reference]" >&2
            exit 2
            ;;
    esac
done

ensure_submodule() {
    local path=$1
    local marker=$2
    local label=$3

    if [[ -e "$repo_dir/$marker" ]]; then
        echo "$label source is already present: $path"
        return
    fi

    if ! git -C "$repo_dir" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        echo "$label source is missing at $path, and this directory is not a Git checkout." >&2
        echo "Use a source package that includes submodules or clone the repository, then retry." >&2
        exit 1
    fi

    git -C "$repo_dir" submodule sync -- "$path"
    git -C "$repo_dir" submodule update --init "$path"
}

ensure_submodule third_party/mgba third_party/mgba/CMakeLists.txt mGBA

if $with_reference; then
    ensure_submodule reference/fireemblem8u reference/fireemblem8u/README.md \
        "FE8 decomp reference"
fi

echo "Dependencies are ready."
