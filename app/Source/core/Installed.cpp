#include "Installed.h"

namespace onga::sync
{
Locations Locations::system()
{
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    return under (juce::File ("/"), home);
}

Locations Locations::under (const juce::File& root, const juce::File& home)
{
    Locations l;
    l.components = root.getChildFile ("Library/Audio/Plug-Ins/Components");
    l.vst3 = root.getChildFile ("Library/Audio/Plug-Ins/VST3");
    l.apps = root.getChildFile ("Applications");
    l.userComponents = home.getChildFile ("Library/Audio/Plug-Ins/Components");
    l.userVst3 = home.getChildFile ("Library/Audio/Plug-Ins/VST3");
    return l;
}

static juce::String versionFromPlistXml (const juce::XmlElement& plist)
{
    auto* dict = plist.getChildByName ("dict");
    if (dict == nullptr)
        return {};

    juce::String shortVersion, bundleVersion;
    for (auto* e = dict->getFirstChildElement(); e != nullptr; e = e->getNextElement())
    {
        if (! e->hasTagName ("key"))
            continue;
        auto* value = e->getNextElement();
        if (value == nullptr)
            break;
        const auto key = e->getAllSubText().trim();
        if (key == "CFBundleShortVersionString") shortVersion = value->getAllSubText().trim();
        if (key == "CFBundleVersion")            bundleVersion = value->getAllSubText().trim();
    }
    return shortVersion.isNotEmpty() ? shortVersion : bundleVersion;
}

juce::String readBundleVersion (const juce::File& bundle)
{
    const auto plistFile = bundle.getChildFile ("Contents/Info.plist");
    if (! plistFile.existsAsFile())
        return {};

    juce::String text = plistFile.loadFileAsString();

   #if JUCE_MAC
    // Binary plists (rare for plug-ins, but possible) go through plutil.
    if (text.startsWith ("bplist"))
    {
        juce::ChildProcess p;
        if (p.start (juce::StringArray { "/usr/bin/plutil", "-convert", "xml1", "-o", "-", plistFile.getFullPathName() }))
            text = p.readAllProcessOutput();
    }
   #endif

    if (auto xml = juce::XmlDocument::parse (text))
        return versionFromPlistXml (*xml);
    return {};
}

InstallState scan (const Package& p, const Locations& where)
{
    InstallState s;
    const auto au = where.components.getChildFile (p.bundle + ".component");
    const auto vst3 = where.vst3.getChildFile (p.bundle + ".vst3");
    const auto app = where.apps.getChildFile (p.bundle + ".app");

    s.au = au.isDirectory();
    s.vst3 = vst3.isDirectory();
    s.app = app.isDirectory();
    s.version = s.au ? readBundleVersion (au) : (s.vst3 ? readBundleVersion (vst3) : juce::String());
    if (s.app)
        s.appVersion = readBundleVersion (app);

    s.userCopies = where.userComponents.getChildFile (p.bundle + ".component").isDirectory()
                || where.userVst3.getChildFile (p.bundle + ".vst3").isDirectory();
    return s;
}

int compareVersions (const juce::String& a, const juce::String& b)
{
    const auto pa = juce::StringArray::fromTokens (a.trim(), ".", {});
    const auto pb = juce::StringArray::fromTokens (b.trim(), ".", {});
    for (int i = 0; i < juce::jmax (pa.size(), pb.size()); ++i)
    {
        const int x = pa[i].getIntValue(), y = pb[i].getIntValue();   // out of range -> "" -> 0
        if (x != y)
            return x < y ? -1 : 1;
    }
    return 0;
}
} // namespace onga::sync
