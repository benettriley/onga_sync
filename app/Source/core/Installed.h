#pragma once

#include "Catalog.h"

/*
    What's on this Mac: for each catalog package, which bundles exist and what version
    they report (CFBundleShortVersionString). ONGA installs system-wide; per-user copies
    (left by source builds) are noted so the app can say why a host sees two.
*/
namespace onga::sync
{
struct Locations
{
    juce::File components, vst3, apps;             // /Library/Audio/Plug-Ins/..., /Applications
    juce::File userComponents, userVst3;           // ~/Library/Audio/Plug-Ins/...

    /** The real folders on this Mac. */
    static Locations system();
    /** The same layout under a folder (for tests). */
    static Locations under (const juce::File& root, const juce::File& home);
};

struct InstallState
{
    juce::String version;       // from the AU, else the VST3; empty when not installed
    bool au = false, vst3 = false, app = false;
    bool userCopies = false;    // a copy in ~/Library as well
    juce::String appVersion;

    bool installed() const { return au || vst3; }
};

/** Looks for a package's bundles (current name only; old names are uninstall targets). */
InstallState scan (const Package& p, const Locations& where);

/** CFBundleShortVersionString (else CFBundleVersion) from a bundle's Info.plist. */
juce::String readBundleVersion (const juce::File& bundle);

/** -1, 0 or 1. Missing parts count as 0, so "1.2" == "1.2.0". */
int compareVersions (const juce::String& a, const juce::String& b);
} // namespace onga::sync
