#!/usr/bin/env bash
#
# Builds one plug-in as universal (arm64 + x86_64) bundles.
#
#   scripts/build_plugin.sh <plugin> <source-dir> <out-dir>
#
# Leaves <out-dir>/<plugin>/ holding <plugin>.component, <plugin>.vst3, <plugin>.app (when
# the plug-in has one) and VERSION (read from the plug-in's CMake project()).
set -euo pipefail

[[ "$(uname -s)" == "Darwin" ]] || { echo "error: macOS only." >&2; exit 1; }
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
. "$ROOT/suite.sh"

name="$1"; src="$(cd "$2" && pwd)"; out="$3"
load_plugin "$name"

build="$src/build-sync"
targets=("${TARGET}_AU" "${TARGET}_VST3")
[[ "$APP" == 1 ]] && targets+=("${TARGET}_Standalone")

echo "==> configuring $name ($REPO)"
cmake -S "$src" -B "$build" \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
      ${CMAKE_ARGS[@]+"${CMAKE_ARGS[@]}"}

echo "==> building ${targets[*]}"
cmake --build "$build" --config Release --parallel "$(sysctl -n hw.ncpu)" --target "${targets[@]}"

art="$build/${TARGET}_artefacts/Release"
dest="$out/$name"
rm -rf "$dest"; mkdir -p "$dest"

bundles=("AU/$name.component" "VST3/$name.vst3")
[[ "$APP" == 1 ]] && bundles+=("Standalone/$name.app")

for b in "${bundles[@]}"; do
    if [[ ! -d "$art/$b" ]]; then
        echo "error: $art/$b is missing. Is $TAG from before the lowercase rename?" >&2
        ls "$art"/*/ >&2 || true
        exit 1
    fi
    ditto "$art/$b" "$dest/$(basename "$b")"
done

# Every binary must carry both slices, or half the Macs out there can't load it.
for b in "$dest"/*.component "$dest"/*.vst3 "$dest"/*.app; do
    [[ -e "$b" ]] || continue
    exe="$b/Contents/MacOS/$name"
    archs="$(lipo -archs "$exe")"
    [[ "$archs" == *arm64* && "$archs" == *x86_64* ]] || { echo "error: $exe is '$archs', not universal" >&2; exit 1; }
done

version="$(sed -nE 's/^project\([^ ]+ VERSION ([0-9.]+).*/\1/p' "$src/CMakeLists.txt" | head -1)"
: "${version:?could not read the version from $src/CMakeLists.txt}"
echo "$version" > "$dest/VERSION"

echo "==> $name $version: $(ls "$dest" | tr '\n' ' ')"
