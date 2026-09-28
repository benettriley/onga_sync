#!/usr/bin/env bash
#
# Assembles an ONGA Sync release from bundles built by build_plugin.sh and the app.
#
#   scripts/assemble.sh <bundles-dir> <out-dir> [<app-dir>]
#
# <bundles-dir>/<plugin>/ holds <BUNDLE>.component, <BUNDLE>.vst3, maybe <BUNDLE>.app, and
# VERSION. <app-dir> holds "ONGA Sync.app" and VERSION. Writes to <out-dir>:
#   packages/catalog.json          what the app reads: versions, files, sizes, SHA-256
#   packages/<id>-<v>.pkg          one plug-in (AU + VST3), what the app downloads
#   packages/<id>-app-<v>.pkg      its standalone app
#   packages/ONGA-Sync-<v>.pkg     the app itself (also its self-update)
#   Install ONGA Sync.pkg          installs the app
#   Install ONGA Suite.pkg         offline: every plug-in in one go, one checkbox each
#   Uninstall ONGA.pkg             moves every ONGA bundle and the app to the Trash
#   ONGA-Sync-<v>.dmg              the app installer, the uninstaller and READ ME.txt
#
# catalog.json names its files relative to itself: publish packages/* side by side (the
# release does), or point the app at packages/ on disk.
#
# Signing is off until there's a Developer ID. Without these, bundles are ad-hoc signed
# (enough for Apple Silicon hosts to load them) and the pkgs are unsigned:
#   ONGA_SIGN_IDENTITY       "Developer ID Application: Name (TEAMID)"  signs bundles + DMG
#   ONGA_INSTALLER_IDENTITY  "Developer ID Installer: Name (TEAMID)"    signs the pkgs
#   ONGA_NOTARIZE=1 with NOTARY_APPLE_ID, NOTARY_TEAM_ID, NOTARY_PASSWORD  notarises + staples
set -euo pipefail

[[ "$(uname -s)" == "Darwin" ]] || { echo "error: macOS only (pkgbuild, productbuild, hdiutil)." >&2; exit 1; }
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
. "$ROOT/suite.sh"

BUNDLES="$(cd "$1" && pwd)"
OUT="$2"; mkdir -p "$OUT"; OUT="$(cd "$OUT" && pwd)"
APPDIR="${3:-}"; [[ -n "$APPDIR" ]] && APPDIR="$(cd "$APPDIR" && pwd)"
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/pkgs" "$WORK/resources" "$WORK/dmg"
PACKAGES="$OUT/packages"; rm -rf "$PACKAGES"; mkdir -p "$PACKAGES"

SIGN_IDENTITY="${ONGA_SIGN_IDENTITY:--}"
INSTALLER_IDENTITY="${ONGA_INSTALLER_IDENTITY:-}"

# A value for a sourced sh file: single-quoted, with any single quote escaped.
sq() { local v="${1//\'/\'\\\'\'}"; printf "'%s'" "$v"; }
# Joins the remaining arguments with newlines.
lines() { local IFS=$'\n'; echo "$*"; }
xml() { local v="${1//&/&amp;}"; v="${v//</&lt;}"; v="${v//>/&gt;}"; v="${v//\"/&quot;}"; printf '%s' "$v"; }
# A JSON string, and a JSON array of strings.
js() { local v="${1//\\/\\\\}"; v="${v//\"/\\\"}"; printf '"%s"' "$v"; }
jsa() { local out="" x; for x in "$@"; do out+="${out:+, }$(js "$x")"; done; printf '[%s]' "$out"; }
# {"url","sha256","size"} for a file in packages/.
jfile() { printf '{ "url": %s, "sha256": "%s", "size": %s }' "$(js "$(basename "$1")")" \
              "$(shasum -a 256 "$1" | cut -d' ' -f1)" "$(stat -f%z "$1")"; }

sign_bundle() {
    local args=(--force --sign "$SIGN_IDENTITY")
    if [[ "$SIGN_IDENTITY" == "-" ]]; then
        args+=(--timestamp=none)
    else
        args+=(--timestamp --options runtime)
        [[ "$1" == *.app ]] && args+=(--entitlements "$ROOT/packaging/app.entitlements")
    fi
    codesign "${args[@]}" "$1"
    codesign --verify --strict "$1"
}

# pkgbuild marks bundles relocatable by default: Installer then looks the bundle ID up
# and writes over any copy it finds elsewhere (a build folder, say) instead of the path
# the package names. Turn that off for every bundle.
component_pkg() {  # <root> <identifier> <version> <scripts-dir> <out.pkg>
    local plist="$WORK/$(basename "$5" .pkg).plist" i=0
    pkgbuild --analyze --root "$1" "$plist" >/dev/null
    while /usr/libexec/PlistBuddy -c "Print :$i" "$plist" >/dev/null 2>&1; do
        # The key only exists for app bundles; plug-in bundles need it added.
        /usr/libexec/PlistBuddy -c "Set :$i:BundleIsRelocatable false" "$plist" >/dev/null 2>&1 \
            || /usr/libexec/PlistBuddy -c "Add :$i:BundleIsRelocatable bool false" "$plist"
        i=$((i + 1))
    done
    (( i > 0 )) || { echo "error: pkgbuild found no bundles under $1" >&2; exit 1; }
    pkgbuild --root "$1" --component-plist "$plist" --identifier "$2" --version "$3" \
             --scripts "$4" --install-location / "$5" >/dev/null
}

sign_args=()
[[ -n "$INSTALLER_IDENTITY" ]] && sign_args=(--sign "$INSTALLER_IDENTITY")

# A component pkg on its own, as a product archive `installer -pkg` takes (and signs).
product_pkg() {  # <out.pkg> <component.pkg>...
    local out="$1"; shift
    local args=()
    for c in "$@"; do args+=(--package "$c"); done
    productbuild "${args[@]}" ${sign_args[@]+"${sign_args[@]}"} "$out" >/dev/null
}

write_target() {  # <dir> <kind> <name> <old names...> -- <receipts...>
    local dir="$1" kind="$2" name="$3"; shift 3
    local olds=() receipts=()
    while (( $# )) && [[ "$1" != "--" ]]; do olds+=("$1"); shift; done
    (( $# )) && shift
    receipts=("$@")
    mkdir -p "$dir"
    cp "$ROOT/packaging/scripts/preinstall" "$ROOT/packaging/scripts/postinstall" "$dir/"
    {
        echo "KIND=$kind"
        echo "NAME=$(sq "$name")"
        echo "OLD_NAMES=$(sq "$(lines ${olds[@]+"${olds[@]}"})")"
        echo "RECEIPTS=$(sq "$(lines ${receipts[@]+"${receipts[@]}"})")"
    } > "$dir/target"
}

choices=""; outline=""; pkgrefs=""; rows=""; readme=""
app_refs=""; app_names=()
all_names=(); all_receipts=()
entries=()

for p in "${PLUGINS[@]}"; do
    load_plugin "$p"
    src="$BUNDLES/$p"
    [[ -f "$src/VERSION" ]] || { echo "error: $src/VERSION missing -- run build_plugin.sh $p first" >&2; exit 1; }
    version="$(cat "$src/VERSION")"
    echo "==> $p $version"

    root="$WORK/root-$p"
    mkdir -p "$root/Library/Audio/Plug-Ins/Components" "$root/Library/Audio/Plug-Ins/VST3"
    ditto "$src/$BUNDLE.component" "$root/Library/Audio/Plug-Ins/Components/$BUNDLE.component"
    ditto "$src/$BUNDLE.vst3" "$root/Library/Audio/Plug-Ins/VST3/$BUNDLE.vst3"
    sign_bundle "$root/Library/Audio/Plug-Ins/Components/$BUNDLE.component"
    sign_bundle "$root/Library/Audio/Plug-Ins/VST3/$BUNDLE.vst3"

    id="com.ongatools.$p.install"
    write_target "$WORK/scripts-$p" plugin "$BUNDLE" ${OLD_NAMES[@]+"${OLD_NAMES[@]}"} -- ${OLD_RECEIPTS[@]+"${OLD_RECEIPTS[@]}"}
    component_pkg "$root" "$id" "$version" "$WORK/scripts-$p" "$WORK/pkgs/$p.pkg"
    plugin_pkg="$PACKAGES/$p-$version.pkg"
    product_pkg "$plugin_pkg" "$WORK/pkgs/$p.pkg"
    receipts=("$id")

    outline+="        <line choice=\"$p\"/>"$'\n'
    choices+="    <choice id=\"$p\" title=\"$(xml "$BUNDLE") $version\" description=\"$(xml "$BLURB") Audio Unit and VST3.\">"$'\n'
    choices+="        <pkg-ref id=\"$id\"/>"$'\n'"    </choice>"$'\n'
    pkgrefs+="    <pkg-ref id=\"$id\" version=\"$version\" onConclusion=\"none\">$p.pkg</pkg-ref>"$'\n'
    rows+="    <tr><td class=\"name\">$(xml "$BUNDLE")</td><td class=\"ver\">$version</td><td>$(xml "$BLURB")</td></tr>"$'\n'
    readme+="$(printf '  %-12s %-8s %s' "$BUNDLE" "$version" "$BLURB")"$'\n'

    all_names+=("$BUNDLE" ${OLD_NAMES[@]+"${OLD_NAMES[@]}"})
    all_receipts+=("$id" ${OLD_RECEIPTS[@]+"${OLD_RECEIPTS[@]}"})

    app_json=null
    if [[ "$APP" == 1 ]]; then
        aroot="$WORK/root-$p-app"
        mkdir -p "$aroot/Applications"
        ditto "$src/$BUNDLE.app" "$aroot/Applications/$BUNDLE.app"
        sign_bundle "$aroot/Applications/$BUNDLE.app"
        aid="$id.app"
        write_target "$WORK/scripts-$p-app" app "$BUNDLE" ${OLD_NAMES[@]+"${OLD_NAMES[@]}"}
        component_pkg "$aroot" "$aid" "$version" "$WORK/scripts-$p-app" "$WORK/pkgs/$p-app.pkg"
        product_pkg "$PACKAGES/$p-app-$version.pkg" "$WORK/pkgs/$p-app.pkg"
        app_json="@@$PACKAGES/$p-app-$version.pkg@@"
        app_refs+="        <pkg-ref id=\"$aid\"/>"$'\n'
        pkgrefs+="    <pkg-ref id=\"$aid\" version=\"$version\" onConclusion=\"none\">$p-app.pkg</pkg-ref>"$'\n'
        app_names+=("$BUNDLE")
        all_receipts+=("$aid")
        receipts+=("$aid")
    fi
    receipts+=(${OLD_RECEIPTS[@]+"${OLD_RECEIPTS[@]}"})

    entries+=("    { \"id\": $(js "$p"), \"name\": $(js "$BUNDLE"), \"bundle\": $(js "$BUNDLE"), \"version\": $(js "$version"),
      \"type\": $(js "$TYPE"), \"blurb\": $(js "$BLURB"),
      \"plugin\": @@$plugin_pkg@@,
      \"app\": $app_json,
      \"oldNames\": $(jsa ${OLD_NAMES[@]+"${OLD_NAMES[@]}"}),
      \"receipts\": $(jsa "${receipts[@]}") }")
done

apps_list="$(IFS=,; echo "${app_names[*]}" | sed 's/,/, /g')"
cat > "$WORK/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>ONGA Suite $SUITE_VERSION</title>
    <organization>com.ongatools</organization>
    <!-- System-wide: every host and every user account sees /Library/Audio/Plug-Ins. -->
    <domains enable_anywhere="false" enable_currentUserHome="false" enable_localSystem="true"/>
    <options customize="always" require-scripts="true" hostArchitectures="arm64,x86_64"/>
    <volume-check>
        <allowed-os-versions><os-version min="11.0"/></allowed-os-versions>
    </volume-check>
    <welcome file="welcome.html" mime-type="text/html"/>
    <conclusion file="conclusion.html" mime-type="text/html"/>
    <choices-outline>
$outline        <line choice="apps"/>
    </choices-outline>
$choices    <choice id="apps" title="Standalone apps" start_selected="false"
            description="Run $apps_list without a DAW. Installed to /Applications.">
$app_refs    </choice>
$pkgrefs</installer-gui-script>
XML

# Installer pages and READ ME, with the version and plug-in list filled in.
ROWS="$rows" VERSION="$SUITE_VERSION" perl -pe 's/\@PLUGIN_ROWS\@\n?/$ENV{ROWS}/; s/\@VERSION\@/$ENV{VERSION}/g' \
    "$ROOT/packaging/resources/welcome.html" > "$WORK/resources/welcome.html"
cp "$ROOT/packaging/resources/conclusion.html" "$WORK/resources/"
LINES="$readme" VERSION="$SUITE_VERSION" perl -pe 's/\@PLUGIN_LINES\@\n?/$ENV{LINES}/; s/\@VERSION\@/$ENV{VERSION}/g' \
    "$ROOT/packaging/READ ME.txt" > "$WORK/dmg/READ ME.txt"

SUITE_PKG="$OUT/Install ONGA Suite.pkg"
SYNC_PKG="$OUT/Install ONGA Sync.pkg"
UNINSTALL_PKG="$OUT/Uninstall ONGA.pkg"
rm -f "$SUITE_PKG" "$SYNC_PKG" "$UNINSTALL_PKG"

echo "==> productbuild: $SUITE_PKG"
productbuild --distribution "$WORK/distribution.xml" --resources "$WORK/resources" \
             --package-path "$WORK/pkgs" ${sign_args[@]+"${sign_args[@]}"} "$SUITE_PKG"

# ------------------------------------------------------------------ the app
sync_json=null; SYNC_VERSION=""
if [[ -n "$APPDIR" ]]; then
    SYNC_VERSION="$(cat "$APPDIR/VERSION")"
    echo "==> ONGA Sync $SYNC_VERSION"
    sroot="$WORK/root-sync"
    mkdir -p "$sroot/Applications"
    ditto "$APPDIR/ONGA Sync.app" "$sroot/Applications/ONGA Sync.app"
    sign_bundle "$sroot/Applications/ONGA Sync.app"
    write_target "$WORK/scripts-sync" app "ONGA Sync"
    component_pkg "$sroot" "$SUITE_ID.app" "$SYNC_VERSION" "$WORK/scripts-sync" "$WORK/sync-app.pkg"

    mkdir -p "$WORK/sync-resources"
    SV="$SYNC_VERSION" perl -pe 's/\@VERSION\@/$ENV{SV}/g' "$ROOT/packaging/resources/sync-welcome.html" \
        > "$WORK/sync-resources/welcome.html"
    cp "$ROOT/packaging/resources/sync-conclusion.html" "$WORK/sync-resources/conclusion.html"
    cat > "$WORK/sync-distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>ONGA Sync $SYNC_VERSION</title>
    <organization>com.ongatools</organization>
    <domains enable_anywhere="false" enable_currentUserHome="false" enable_localSystem="true"/>
    <options customize="never" require-scripts="true" hostArchitectures="arm64,x86_64"/>
    <volume-check>
        <allowed-os-versions><os-version min="11.0"/></allowed-os-versions>
    </volume-check>
    <welcome file="welcome.html" mime-type="text/html"/>
    <conclusion file="conclusion.html" mime-type="text/html"/>
    <choices-outline><line choice="sync"/></choices-outline>
    <choice id="sync" title="ONGA Sync"><pkg-ref id="$SUITE_ID.app"/></choice>
    <pkg-ref id="$SUITE_ID.app" version="$SYNC_VERSION" onConclusion="none">sync-app.pkg</pkg-ref>
</installer-gui-script>
XML
    echo "==> productbuild: $SYNC_PKG"
    productbuild --distribution "$WORK/sync-distribution.xml" --resources "$WORK/sync-resources" \
                 --package-path "$WORK" ${sign_args[@]+"${sign_args[@]}"} "$SYNC_PKG"

    all_names+=("ONGA Sync")
    all_receipts+=("$SUITE_ID.app")
fi

echo "==> uninstaller"
mkdir -p "$WORK/uninstall"
cp "$ROOT/packaging/uninstall/postinstall" "$WORK/uninstall/"
all_receipts+=("$SUITE_ID.uninstall")
{
    echo "NAMES=$(sq "$(lines "${all_names[@]}")")"
    echo "RECEIPTS=$(sq "$(lines "${all_receipts[@]}")")"
} > "$WORK/uninstall/names"
pkgbuild --nopayload --scripts "$WORK/uninstall" --identifier "$SUITE_ID.uninstall" \
         --version "$SUITE_VERSION" "$WORK/uninstall.pkg" >/dev/null
productbuild --package "$WORK/uninstall.pkg" ${sign_args[@]+"${sign_args[@]}"} "$UNINSTALL_PKG"

notarize() {
    xcrun notarytool submit "$1" --apple-id "$NOTARY_APPLE_ID" --team-id "$NOTARY_TEAM_ID" \
          --password "$NOTARY_PASSWORD" --wait
    xcrun stapler staple "$1"
}
if [[ "${ONGA_NOTARIZE:-0}" == 1 ]]; then
    [[ -n "$INSTALLER_IDENTITY" ]] || { echo "error: notarising needs ONGA_INSTALLER_IDENTITY" >&2; exit 1; }
    for f in "$SUITE_PKG" "$UNINSTALL_PKG" "$PACKAGES"/*.pkg; do notarize "$f"; done
    [[ -n "$APPDIR" ]] && notarize "$SYNC_PKG"
fi

if [[ -n "$APPDIR" ]]; then
    ditto "$SYNC_PKG" "$PACKAGES/ONGA-Sync-$SYNC_VERSION.pkg"
    sync_json="$(jfile "$PACKAGES/ONGA-Sync-$SYNC_VERSION.pkg" | sed "s/^{ /{ \"version\": $(js "$SYNC_VERSION"), /")"
fi

echo "==> catalog.json"
{
    echo "{"
    echo "  \"schema\": 1,"
    echo "  \"suite\": $(js "$SUITE_VERSION"),"
    echo "  \"sync\": $sync_json,"
    echo "  \"packages\": ["
    for i in "${!entries[@]}"; do
        e="${entries[$i]}"
        # Hash the files now, after any notarising and stapling changed them.
        while [[ "$e" =~ @@([^@]+)@@ ]]; do e="${e/"${BASH_REMATCH[0]}"/$(jfile "${BASH_REMATCH[1]}")}"; done
        (( i + 1 < ${#entries[@]} )) && echo "$e," || echo "$e"
    done
    echo "  ]"
    echo "}"
} > "$PACKAGES/catalog.json"
python3 -m json.tool "$PACKAGES/catalog.json" >/dev/null || { echo "error: catalog.json is not valid JSON" >&2; exit 1; }

echo "==> disk image"
DMG_VERSION="${SYNC_VERSION:-$SUITE_VERSION}"
DMG="$OUT/ONGA-Sync-$DMG_VERSION.dmg"
if [[ -n "$APPDIR" ]]; then
    ditto "$SYNC_PKG" "$WORK/dmg/Install ONGA Sync.pkg"
else
    ditto "$SUITE_PKG" "$WORK/dmg/Install ONGA Suite.pkg"
fi
ditto "$UNINSTALL_PKG" "$WORK/dmg/Uninstall ONGA.pkg"
rm -f "$DMG"
hdiutil create -volname "ONGA Sync $DMG_VERSION" -srcfolder "$WORK/dmg" -fs HFS+ -format UDZO "$DMG" >/dev/null
if [[ "$SIGN_IDENTITY" != "-" ]]; then
    codesign --force --sign "$SIGN_IDENTITY" --timestamp "$DMG"
    [[ "${ONGA_NOTARIZE:-0}" == 1 ]] && notarize "$DMG"
fi

echo
echo "App installer:   $SYNC_PKG"
echo "Suite (offline): $SUITE_PKG"
echo "Uninstaller:     $UNINSTALL_PKG"
echo "Catalog:         $PACKAGES/catalog.json"
echo "Disk image:      $DMG"
