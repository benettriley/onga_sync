#!/usr/bin/env bash
# Release notes from bundles/*/VERSION and dist/catalog.json.   release_notes.sh <headline>
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
. "$ROOT/suite.sh"

echo "$1"
echo
for p in "${PLUGINS[@]}"; do
    load_plugin "$p"
    [[ -f "bundles/$p/VERSION" ]] && echo "- $BUNDLE $(cat "bundles/$p/VERSION")"
done
[[ -f syncapp/VERSION ]] && echo "- ONGA Sync app $(cat syncapp/VERSION)"
cat <<'EOF'

**New Mac:** open the DMG, double-click Install ONGA Sync.pkg, then open ONGA Sync and sign in.
**Already have ONGA Sync:** it picks this release up on its next check (or click CHECK NOW).
**No internet on the studio Mac:** ONGA-Suite-*.pkg installs every plug-in in one go.

Apple Silicon + Intel, macOS 11+. Until the installers are notarised, right-click a .pkg and choose Open the first time.
EOF
