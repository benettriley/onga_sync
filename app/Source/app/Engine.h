#pragma once

#include "../core/Catalog.h"
#include "../core/Installed.h"

#include <juce_events/juce_events.h>

#include <atomic>
#include <map>

/*
    The library engine: fetches the catalog, scans the Mac, and runs one job at a time
    (install / update / uninstall / self-update) on a background thread. The UI listens
    for change messages and reads the state; everything public is message-thread only.
*/
namespace onga::sync
{
enum class Status { unknown, notInstalled, upToDate, updateAvailable, newer };

struct Settings
{
    juce::String catalogUrl;
    bool installApps = false;   // install standalone apps alongside the plug-ins

    static Settings load (const juce::File&);
    void save (const juce::File&) const;
};

class Engine final : public juce::ChangeBroadcaster
{
public:
    Engine (juce::File dataDir, juce::File cacheDir, juce::String appVersion);
    ~Engine() override;

    //==============================================================================
    Settings& settings() noexcept { return prefs; }
    void saveSettings() { prefs.save (settingsFile); }
    juce::String defaultCatalogUrl() const;
    juce::URL catalogUrl() const;

    /** Fetch the catalog and rescan. */
    void refresh();
    /** Rescan the Mac only. */
    void rescan();

    bool isChecking() const noexcept { return checking; }
    bool hasCatalog() const noexcept { return loaded; }
    const Catalog& catalog() const noexcept { return cat; }
    juce::String catalogError() const { return error; }
    juce::Time lastChecked() const noexcept { return checkedAt; }

    InstallState stateOf (const juce::String& id) const;
    Status statusOf (const juce::String& id) const;
    juce::StringArray updatable() const;
    juce::StringArray notInstalled() const;

    juce::String appVersion() const { return version; }
    bool selfUpdateAvailable() const;

    //==============================================================================
    struct Job
    {
        bool running = false;
        juce::String title;         // "INSTALLING BLOOM"
        juce::String currentId;     // package being downloaded, if any
        double progress = -1.0;     // 0..1 while downloading, -1 otherwise
        juce::StringArray ids;      // packages this job touches
    };
    const Job& job() const noexcept { return current; }

    void install (const juce::StringArray& ids);   // install or update, apps per settings
    void uninstall (const juce::String& id);
    void updateSelf();
    void cancel() { cancelRequested = true; }

    /** Result of the last job: text for the status line, and whether it failed. */
    std::function<void (const juce::String& text, bool failed)> onJobFinished;
    /** Called after a self-update installs. The app offers a relaunch. */
    std::function<void()> onSelfUpdated;

private:
    struct Item { juce::String id; Download d; juce::String file; };
    void startJob (const juce::String& title, const juce::StringArray& ids, std::function<juce::Result()> work,
                   std::function<void (juce::Result)> after = {});
    juce::Result downloadAll (const std::vector<Item>&, juce::Array<juce::File>& files);
    void post (std::function<void()>);

    juce::File dataDir, cacheDir, settingsFile;
    juce::String version;
    Settings prefs;

    Catalog cat;
    bool loaded = false, checking = false;
    juce::String error;
    juce::Time checkedAt;
    std::map<juce::String, InstallState> states;

    Job current;
    std::atomic<bool> cancelRequested { false };
    juce::ThreadPool pool { 1 };

    JUCE_DECLARE_WEAK_REFERENCEABLE (Engine)
};
} // namespace onga::sync
