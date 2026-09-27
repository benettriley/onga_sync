#include "Catalog.h"

namespace onga::sync
{
juce::URL resolveUrl (const juce::URL& base, const juce::String& ref)
{
    if (ref.isEmpty())
        return {};
    if (ref.contains ("://"))
        return juce::URL (ref);

    if (base.isLocalFile())
        return juce::URL (base.getLocalFile().getSiblingFile (ref));

    const auto s = base.toString (false);
    return juce::URL (s.upToLastOccurrenceOf ("/", true, false) + juce::URL::addEscapeChars (ref, false));
}

static Download parseDownload (const juce::var& v, const juce::URL& base)
{
    Download d;
    if (! v.isObject())
        return d;
    d.url = resolveUrl (base, v["url"].toString());
    d.sha256 = v["sha256"].toString().toLowerCase();
    d.size = (juce::int64) v["size"];
    return d;
}

static juce::StringArray parseStrings (const juce::var& v)
{
    juce::StringArray out;
    if (auto* a = v.getArray())
        for (auto& s : *a)
            out.add (s.toString());
    return out;
}

juce::Result Catalog::parse (const juce::String& json, const juce::URL& base, Catalog& out)
{
    juce::var root;
    if (auto r = juce::JSON::parse (json, root); r.failed())
        return juce::Result::fail ("The catalog is not valid JSON: " + r.getErrorMessage());
    if (! root.isObject())
        return juce::Result::fail ("The catalog is empty.");

    const int schema = root["schema"];
    if (schema != kSchema)
        return juce::Result::fail ("This catalog needs a newer ONGA Sync (schema " + juce::String (schema) + ").");

    Catalog c;
    c.suite = root["suite"].toString();
    c.syncVersion = root["sync"]["version"].toString();
    c.sync = parseDownload (root["sync"], base);

    if (auto* list = root["packages"].getArray())
    {
        for (auto& v : *list)
        {
            Package p;
            p.id = v["id"].toString();
            p.name = v["name"].toString();
            p.bundle = v["bundle"].toString();
            p.version = v["version"].toString();
            p.type = v["type"].toString();
            p.blurb = v["blurb"].toString();
            p.plugin = parseDownload (v["plugin"], base);
            p.app = parseDownload (v["app"], base);
            p.oldNames = parseStrings (v["oldNames"]);
            p.receipts = parseStrings (v["receipts"]);

            if (p.name.isEmpty()) p.name = p.id;
            if (p.bundle.isEmpty()) p.bundle = p.id;

            if (p.id.isEmpty() || p.version.isEmpty() || ! p.plugin.isValid())
                return juce::Result::fail ("The catalog has an incomplete entry (" + p.id + ").");
            c.packages.push_back (std::move (p));
        }
    }

    out = std::move (c);
    return juce::Result::ok();
}

const Package* Catalog::find (const juce::String& id) const
{
    for (auto& p : packages)
        if (p.id == id)
            return &p;
    return nullptr;
}
} // namespace onga::sync
