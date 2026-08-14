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

git -C "$repo_dir" submodule sync -- third_party/mgba
git -C "$repo_dir" submodule update --init third_party/mgba

if $with_reference; then
    git -C "$repo_dir" submodule sync -- reference/fireemblem8u
    git -C "$repo_dir" submodule update --init reference/fireemblem8u
fi

echo "Dependencies are ready."
