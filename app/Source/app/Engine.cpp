#include "Engine.h"

#include "../core/Download.h"
#include "../core/Scripts.h"

#ifndef ONGA_SYNC_CATALOG_URL
 #define ONGA_SYNC_CATALOG_URL "https://github.com/benettriley/onga_sync/releases/latest/download/catalog.json"
#endif

namespace onga::sync
{
Settings Settings::load (const juce::File& f)
{
    Settings s;
    const auto v = juce::JSON::parse (f);
    s.catalogUrl = v["catalogUrl"].toString();
    s.installApps = (bool) v["installApps"];
    return s;
}

void Settings::save (const juce::File& f) const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("catalogUrl", catalogUrl);
    o->setProperty ("installApps", installApps);
    f.getParentDirectory().createDirectory();
    f.replaceWithText (juce::JSON::toString (juce::var (o)));
}

//==============================================================================
Engine::Engine (juce::File data, juce::File cache, juce::String appVersion)
    : dataDir (std::move (data)), cacheDir (std::move (cache)),
      settingsFile (dataDir.getChildFile ("settings.json")), version (std::move (appVersion))
{
    prefs = Settings::load (settingsFile);
}

Engine::~Engine()
{
    cancelRequested = true;
    pool.removeAllJobs (true, 10000);
}

juce::String Engine::defaultCatalogUrl() const { return ONGA_SYNC_CATALOG_URL; }

juce::URL Engine::catalogUrl() const
{
    const auto s = prefs.catalogUrl.trim();
    if (s.isEmpty())
        return juce::URL (defaultCatalogUrl());
    if (s.startsWithChar ('/') || s.startsWithChar ('~'))
        return juce::URL (juce::File (s));
    return juce::URL (s);
}

void Engine::post (std::function<void()> fn)
{
    juce::MessageManager::callAsync ([weak = juce::WeakReference<Engine> (this), fn = std::move (fn)]
    {
        if (weak != nullptr)
            fn();
    });
}

//==============================================================================
void Engine::refresh()
{
    if (checking)
        return;
    checking = true;
    sendChangeMessage();

    const auto url = catalogUrl();
    pool.addJob ([this, url]
    {
        juce::String text;
        Catalog parsed;
        auto r = fetchText (url, text);
        if (r.wasOk())
            r = Catalog::parse (text, url, parsed);

        post ([this, r, parsed = std::move (parsed)]() mutable
        {
            checking = false;
            checkedAt = juce::Time::getCurrentTime();
            if (r.wasOk())
            {
                cat = std::move (parsed);
                loaded = true;
                error = {};
            }
            else
            {
                error = r.getErrorMessage();
            }
            rescan();
        });
    });
}

void Engine::rescan()
{
    states.clear();
    const auto where = Locations::system();
    for (auto& p : cat.packages)
        states[p.id] = scan (p, where);
    sendChangeMessage();
}

InstallState Engine::stateOf (const juce::String& id) const
{
    auto it = states.find (id);
    return it == states.end() ? InstallState {} : it->second;
}

Status Engine::statusOf (const juce::String& id) const
{
    auto* p = cat.find (id);
    auto it = states.find (id);
    if (p == nullptr || it == states.end())
        return Status::unknown;
    if (! it->second.installed())
        return Status::notInstalled;
    const int c = compareVersions (it->second.version, p->version);
    return c < 0 ? Status::updateAvailable : (c == 0 ? Status::upToDate : Status::newer);
}

juce::StringArray Engine::updatable() const
{
    juce::StringArray out;
    for (auto& p : cat.packages)
        if (statusOf (p.id) == Status::updateAvailable)
            out.add (p.id);
    return out;
}

juce::StringArray Engine::notInstalled() const
{
    juce::StringArray out;
    for (auto& p : cat.packages)
        if (statusOf (p.id) == Status::notInstalled)
            out.add (p.id);
    return out;
}

bool Engine::selfUpdateAvailable() const
{
    return loaded && cat.sync.isValid() && compareVersions (cat.syncVersion, version) > 0;
}

//==============================================================================
void Engine::startJob (const juce::String& title, const juce::StringArray& ids,
                       std::function<juce::Result()> work, std::function<void (juce::Result)> after)
{
    if (current.running)
        return;
    current = {};
    current.running = true;
    current.title = title;
    current.ids = ids;
    cancelRequested = false;
    sendChangeMessage();

    pool.addJob ([this, work = std::move (work), after = std::move (after)]
    {
        const auto r = work();
        post ([this, r, after]
        {
            const auto finished = current.title;
            current = {};
            rescan();
            if (after)
                after (r);

            if (onJobFinished)
            {
                if (r.wasOk())                             onJobFinished ("DONE: " + finished, false);
                else if (r.getErrorMessage() == "cancelled") onJobFinished ("CANCELLED", false);
                else                                       onJobFinished (r.getErrorMessage(), true);
            }
        });
    });
}

juce::Result Engine::downloadAll (const std::vector<Item>& items, juce::Array<juce::File>& files)
{
    const auto dir = cacheDir.getChildFile ("downloads");
    for (auto& it : items)
    {
        post ([this, id = it.id] { current.currentId = id; current.progress = 0.0; sendChangeMessage(); });

        auto dest = dir.getChildFile (it.file);
        double lastSent = -1.0;
        auto r = download (it.d, dest, [this, &lastSent] (juce::int64 done, juce::int64 total)
        {
            if (total > 0)
            {
                const double p = (double) done / (double) total;
                if (p - lastSent >= 0.02 || p >= 1.0)
                {
                    lastSent = p;
                    post ([this, p] { current.progress = p; sendChangeMessage(); });
                }
            }
            return ! cancelRequested.load();
        });
        if (r.failed())
        {
            for (auto& f : files) f.deleteFile();
            return r;
        }
        files.add (dest);
    }
    post ([this] { current.currentId = {}; current.progress = -1.0; sendChangeMessage(); });
    return juce::Result::ok();
}

static juce::Result runInstall (const juce::Array<juce::File>& files)
{
    juce::String out;
    auto r = runAsAdmin (installScript (files), out);
    for (auto& f : files)
        f.deleteFile();
    return r;
}

void Engine::install (const juce::StringArray& ids)
{
    std::vector<Item> items;
    for (auto& id : ids)
    {
        auto* p = cat.find (id);
        if (p == nullptr)
            continue;
        items.push_back ({ p->id, p->plugin, p->id + "-" + p->version + ".pkg" });
        if (prefs.installApps && p->hasApp())
            items.push_back ({ p->id, p->app, p->id + "-app-" + p->version + ".pkg" });
    }
    if (items.empty())
        return;

    const auto title = ids.size() == 1 ? (statusOf (ids[0]) == Status::updateAvailable ? "UPDATING " : "INSTALLING ")
                                             + cat.find (ids[0])->name.toUpperCase()
                                       : "INSTALLING " + juce::String (ids.size()) + " PLUG-INS";
    startJob (title, ids, [this, items]
    {
        juce::Array<juce::File> files;
        auto r = downloadAll (items, files);
        return r.failed() ? r : runInstall (files);
    });
}

void Engine::uninstall (const juce::String& id)
{
    auto* p = cat.find (id);
    if (p == nullptr)
        return;
    const auto script = uninstallScript (*p, UserContext::current(),
                                         juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H%M%S"));
    startJob ("UNINSTALLING " + p->name.toUpperCase(), { id }, [script]
    {
        juce::String out;
        return runAsAdmin (script, out);
    });
}

void Engine::updateSelf()
{
    if (! selfUpdateAvailable())
        return;
    std::vector<Item> items { { "sync", cat.sync, "ONGA-Sync-" + cat.syncVersion + ".pkg" } };
    startJob ("UPDATING ONGA SYNC", {}, [this, items]
    {
        juce::Array<juce::File> files;
        auto r = downloadAll (items, files);
        return r.failed() ? r : runInstall (files);
    },
    [this] (juce::Result r)
    {
        if (r.wasOk() && onSelfUpdated)
            onSelfUpdated();
    });
}
} // namespace onga::sync
