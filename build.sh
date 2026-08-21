#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

"$repo_dir/scripts/bootstrap.sh"
exec "$repo_dir/scripts/build.sh" "$@"
