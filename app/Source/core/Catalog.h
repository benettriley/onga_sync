#pragma once

#include <juce_core/juce_core.h>

#include <vector>

/*
    The catalog: what ONGA Sync can install. CI writes catalog.json next to the packages
    on every release (scripts/assemble.sh); the app fetches it, compares it with what is
    on the Mac and offers installs, updates and uninstalls.

        {
          "schema": 1,
          "suite": "2026.1",
          "sync":  { "version": "0.1.0", "url": "ONGA-Sync-0.1.0.pkg", "sha256": "...", "size": 123 },
          "packages": [
            { "id": "bloom", "name": "bloom", "bundle": "bloom", "version": "0.1.0",
              "type": "effect", "blurb": "...",
              "plugin": { "url": "bloom-0.1.0.pkg", "sha256": "...", "size": 123 },
              "app":    null,
              "oldNames": [ "ONGA BLOOM" ],
              "receipts": [ "com.ongatools.bloom.install", "com.onga.bloom.pkg" ] }
          ]
        }

    A relative url is relative to the catalog's own address, so one catalog works from a
    GitHub release, a test pre-release or a folder on disk.
*/
namespace onga::sync
{
struct Download
{
    juce::URL url;
    juce::String sha256;
    juce::int64 size = 0;

    bool isValid() const { return ! url.isEmpty(); }
};

struct Package
{
    juce::String id;          // stable key, e.g. "bloom", "wizard"
    juce::String name;        // shown in the library
    juce::String bundle;      // bundle file name without extension, e.g. "The Wizard"
    juce::String version;
    juce::String type;        // "effect" | "instrument"
    juce::String blurb;
    Download plugin, app;     // app is optional (a standalone build)
    juce::StringArray oldNames;   // earlier bundle names, removed on uninstall
    juce::StringArray receipts;   // installer receipts to forget on uninstall

    bool hasApp() const { return app.isValid(); }
};

struct Catalog
{
    static constexpr int kSchema = 1;

    juce::String suite;
    juce::String syncVersion;
    Download sync;
    std::vector<Package> packages;

    /** Parses catalog JSON. `base` is where the catalog came from (resolves relative urls). */
    static juce::Result parse (const juce::String& json, const juce::URL& base, Catalog& out);

    const Package* find (const juce::String& id) const;
};

/** Resolves a url from the catalog against the catalog's own address. */
juce::URL resolveUrl (const juce::URL& base, const juce::String& ref);
} // namespace onga::sync
