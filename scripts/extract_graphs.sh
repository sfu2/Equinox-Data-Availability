#!/usr/bin/env bash
set -euo pipefail

script_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(CDPATH= cd -- "$script_dir/.." && pwd)"

cd "$repo_root"

if [ -d graphs ]; then
  echo "graphs/ already exists; nothing to extract."
  exit 0
fi

if [ ! -f graphs.tar.xz ]; then
  echo "error: graphs.tar.xz is missing from $repo_root" >&2
  echo "Run 'git lfs pull' before extracting graph inputs." >&2
  exit 1
fi

tar -xJf graphs.tar.xz
