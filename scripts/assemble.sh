#!/usr/bin/env bash
#
# Assembles the ONGA Sync suite installer from bundles built by build_plugin.sh.
#
#   scripts/assemble.sh <bundles-dir> <out-dir>
#
# <bundles-dir>/<plugin>/ holds <plugin>.component, <plugin>.vst3, maybe <plugin>.app,
# and VERSION. Writes to <out-dir>:
#   Install ONGA Sync.pkg     one checkbox per plug-in, plus one for the standalone apps
#   Uninstall ONGA Sync.pkg   moves every ONGA bundle, current and old names, to the Trash
#   ONGA-Sync-<version>.dmg   both of the above and READ ME.txt
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
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/pkgs" "$WORK/resources" "$WORK/dmg"

SIGN_IDENTITY="${ONGA_SIGN_IDENTITY:--}"
INSTALLER_IDENTITY="${ONGA_INSTALLER_IDENTITY:-}"

# A value for a sourced sh file: single-quoted, with any single quote escaped.
sq() { local v="${1//\'/\'\\\'\'}"; printf "'%s'" "$v"; }
# Joins the remaining arguments with newlines.
lines() { local IFS=$'\n'; echo "$*"; }
xml() { local v="${1//&/&amp;}"; v="${v//</&lt;}"; v="${v//>/&gt;}"; v="${v//\"/&quot;}"; printf '%s' "$v"; }

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

for p in "${PLUGINS[@]}"; do
    load_plugin "$p"
    src="$BUNDLES/$p"
    [[ -f "$src/VERSION" ]] || { echo "error: $src/VERSION missing -- run build_plugin.sh $p first" >&2; exit 1; }
    version="$(cat "$src/VERSION")"
    echo "==> $p $version"

    root="$WORK/root-$p"
    mkdir -p "$root/Library/Audio/Plug-Ins/Components" "$root/Library/Audio/Plug-Ins/VST3"
    ditto "$src/$p.component" "$root/Library/Audio/Plug-Ins/Components/$p.component"
    ditto "$src/$p.vst3" "$root/Library/Audio/Plug-Ins/VST3/$p.vst3"
    sign_bundle "$root/Library/Audio/Plug-Ins/Components/$p.component"
    sign_bundle "$root/Library/Audio/Plug-Ins/VST3/$p.vst3"

    id="com.ongatools.$p.install"
    write_target "$WORK/scripts-$p" plugin "$p" ${OLD_NAMES[@]+"${OLD_NAMES[@]}"} -- ${OLD_RECEIPTS[@]+"${OLD_RECEIPTS[@]}"}
    component_pkg "$root" "$id" "$version" "$WORK/scripts-$p" "$WORK/pkgs/$p.pkg"

    outline+="        <line choice=\"$p\"/>"$'\n'
    choices+="    <choice id=\"$p\" title=\"$(xml "$p") $version\" description=\"$(xml "$BLURB") Audio Unit and VST3.\">"$'\n'
    choices+="        <pkg-ref id=\"$id\"/>"$'\n'"    </choice>"$'\n'
    pkgrefs+="    <pkg-ref id=\"$id\" version=\"$version\" onConclusion=\"none\">$p.pkg</pkg-ref>"$'\n'
    rows+="    <tr><td class=\"name\">$(xml "$p")</td><td class=\"ver\">$version</td><td>$(xml "$BLURB")</td></tr>"$'\n'
    readme+="$(printf '  %-12s %-8s %s' "$p" "$version" "$BLURB")"$'\n'

    all_names+=("$p" ${OLD_NAMES[@]+"${OLD_NAMES[@]}"})
    all_receipts+=("$id" ${OLD_RECEIPTS[@]+"${OLD_RECEIPTS[@]}"})

    if [[ "$APP" == 1 ]]; then
        aroot="$WORK/root-$p-app"
        mkdir -p "$aroot/Applications"
        ditto "$src/$p.app" "$aroot/Applications/$p.app"
        sign_bundle "$aroot/Applications/$p.app"
        aid="$id.app"
        write_target "$WORK/scripts-$p-app" app "$p" ${OLD_NAMES[@]+"${OLD_NAMES[@]}"}
        component_pkg "$aroot" "$aid" "$version" "$WORK/scripts-$p-app" "$WORK/pkgs/$p-app.pkg"
        app_refs+="        <pkg-ref id=\"$aid\"/>"$'\n'
        pkgrefs+="    <pkg-ref id=\"$aid\" version=\"$version\" onConclusion=\"none\">$p-app.pkg</pkg-ref>"$'\n'
        app_names+=("$p")
        all_receipts+=("$aid")
    fi
done

apps_list="$(IFS=,; echo "${app_names[*]}" | sed 's/,/, /g')"
cat > "$WORK/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>$SUITE_NAME $SUITE_VERSION</title>
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

sign_args=()
[[ -n "$INSTALLER_IDENTITY" ]] && sign_args=(--sign "$INSTALLER_IDENTITY")

INSTALL_PKG="$OUT/Install ONGA Sync.pkg"
UNINSTALL_PKG="$OUT/Uninstall ONGA Sync.pkg"
rm -f "$INSTALL_PKG" "$UNINSTALL_PKG"

echo "==> productbuild: $INSTALL_PKG"
productbuild --distribution "$WORK/distribution.xml" --resources "$WORK/resources" \
             --package-path "$WORK/pkgs" ${sign_args[@]+"${sign_args[@]}"} "$INSTALL_PKG"

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
    notarize "$INSTALL_PKG"
    notarize "$UNINSTALL_PKG"
fi

echo "==> disk image"
DMG="$OUT/ONGA-Sync-$SUITE_VERSION.dmg"
ditto "$INSTALL_PKG" "$WORK/dmg/Install ONGA Sync.pkg"
ditto "$UNINSTALL_PKG" "$WORK/dmg/Uninstall ONGA Sync.pkg"
rm -f "$DMG"
hdiutil create -volname "$SUITE_NAME $SUITE_VERSION" -srcfolder "$WORK/dmg" -fs HFS+ -format UDZO "$DMG" >/dev/null
if [[ "$SIGN_IDENTITY" != "-" ]]; then
    codesign --force --sign "$SIGN_IDENTITY" --timestamp "$DMG"
    [[ "${ONGA_NOTARIZE:-0}" == 1 ]] && notarize "$DMG"
fi

echo
echo "Installer:   $INSTALL_PKG"
echo "Uninstaller: $UNINSTALL_PKG"
echo "Disk image:  $DMG"
