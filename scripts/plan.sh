#!/usr/bin/env bash
# Prints the CI build matrix and the suite version as GitHub Actions outputs.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
. "$ROOT/suite.sh"

entries=()
for p in "${PLUGINS[@]}"; do
    load_plugin "$p"
    # A plug-in pinned to a commit (no tags yet) ignores ref_override.
    [[ "$TAG" =~ ^[0-9a-f]{40}$ ]] && commit=true || commit=false
    entries+=("{\"plugin\":\"$p\",\"repo\":\"$REPO\",\"tag\":\"$TAG\",\"commit\":$commit}")
done
( IFS=,; echo "matrix={\"include\":[${entries[*]}]}" )
echo "version=$SUITE_VERSION"
