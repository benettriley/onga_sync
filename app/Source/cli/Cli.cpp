/*
    OngaSyncCli: the app's core, headless. CI uses it to test exactly what ships:

        OngaSyncCli test                                 unit tests (any OS)
        OngaSyncCli check <catalog.json> [--expect-current]
                                                           what's installed vs the catalog
        OngaSyncCli verify <catalog.json>                download every package through
                                                           the app's downloader + checksum
        OngaSyncCli install-script <pkg>...              the script the app runs as root
        OngaSyncCli uninstall-script <catalog.json> <id>
*/
#include "../core/Account.h"
#include "../core/Catalog.h"
#include "../core/Download.h"
#include "../core/Installed.h"
#include "../core/Scripts.h"

using namespace onga::sync;

namespace
{
int fail (const juce::String& msg)
{
    std::cerr << "error: " << msg << std::endl;
    return 1;
}

juce::Result loadCatalog (const juce::String& path, Catalog& cat)
{
    const juce::File f = juce::File::getCurrentWorkingDirectory().getChildFile (path);
    if (! f.existsAsFile())
        return juce::Result::fail ("no catalog at " + f.getFullPathName());
    return Catalog::parse (f.loadFileAsString(), juce::URL (f), cat);
}

/** When run through sudo, the Trash that matters is the invoking user's. */
UserContext invokingUser()
{
    const auto sudoUser = juce::SystemStats::getEnvironmentVariable ("SUDO_USER", {});
    if (sudoUser.isEmpty())
        return UserContext::current();
    return { sudoUser, juce::File ("/Users/" + sudoUser) };
}

//==============================================================================
int check (const Catalog& cat, bool expectCurrent)
{
    int stale = 0;
    const auto where = Locations::system();
    for (auto& p : cat.packages)
    {
        const auto s = scan (p, where);
        const bool current = s.installed() && compareVersions (s.version, p.version) == 0;
        std::cout << juce::String (p.id).paddedRight (' ', 12)
                  << (s.installed() ? s.version : juce::String ("-")).paddedRight (' ', 10)
                  << p.version.paddedRight (' ', 10)
                  << (s.au ? "AU " : "   ") << (s.vst3 ? "VST3 " : "     ") << (s.app ? "APP " : "    ")
                  << (current ? "current" : (s.installed() ? "UPDATE" : "missing")) << std::endl;
        if (! current)
            ++stale;
    }
    if (expectCurrent && stale > 0)
        return fail (juce::String (stale) + " package(s) are not at the catalog version");
    return 0;
}

int verify (const Catalog& cat)
{
    const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("onga-sync-verify");
    std::vector<std::pair<juce::String, Download>> all;
    if (cat.sync.isValid()) all.push_back ({ "ONGA Sync", cat.sync });
    for (auto& p : cat.packages)
    {
        all.push_back ({ p.id, p.plugin });
        if (p.hasApp()) all.push_back ({ p.id + " app", p.app });
    }
    for (auto& [name, d] : all)
    {
        const auto dest = tmp.getChildFile ("download.pkg");
        if (auto r = download (d, dest, {}); r.failed())
            return fail (name + ": " + r.getErrorMessage());
        std::cout << "ok  " << name << "  " << d.size << " bytes  sha256 " << d.sha256.substring (0, 12) << std::endl;
    }
    tmp.deleteRecursively();
    return 0;
}

//==============================================================================
int failures = 0;
void expect (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  ok    " : "  FAIL  ") << what << std::endl;
    if (! ok) ++failures;
}

int runTests()
{
    std::cout << "versions" << std::endl;
    expect (compareVersions ("1.2.0", "1.2") == 0, "1.2.0 == 1.2");
    expect (compareVersions ("1.10.0", "1.9.3") > 0, "1.10.0 > 1.9.3");
    expect (compareVersions ("0.1.0", "1.0.0") < 0, "0.1.0 < 1.0.0");
    expect (compareVersions ("", "0.0.1") < 0, "missing < anything");

    std::cout << "catalog" << std::endl;
    const juce::String json = R"({
        "schema": 1, "suite": "2026.1",
        "sync": { "version": "0.2.0", "url": "ONGA-Sync-0.2.0.pkg", "sha256": "AB", "size": 5 },
        "packages": [
          { "id": "wizard", "name": "The Wizard", "bundle": "The Wizard", "version": "1.1.6", "type": "instrument",
            "plugin": { "url": "wizard-1.1.6.pkg", "sha256": "cd", "size": 10 },
            "app": { "url": "https://example.com/x/wizard-app-1.1.6.pkg" },
            "oldNames": [], "receipts": [ "com.ongatools.wizard.install" ] },
          { "id": "bloom", "version": "0.1.0", "type": "effect", "plugin": { "url": "bloom 0.1.0.pkg" }, "app": null }
        ] })";
    Catalog cat;
    const auto r = Catalog::parse (json, juce::URL ("https://github.com/o/r/releases/latest/download/catalog.json"), cat);
    expect (r.wasOk(), "parses (" + r.getErrorMessage() + ")");
    expect (cat.packages.size() == 2, "two packages");
    expect (cat.sync.url.toString (false) == "https://github.com/o/r/releases/latest/download/ONGA-Sync-0.2.0.pkg", "relative sync url");
    expect (cat.sync.sha256 == "ab", "sha256 lowercased");
    if (auto* w = cat.find ("wizard"))
    {
        expect (w->bundle == "The Wizard", "bundle name with a space");
        expect (w->hasApp() && w->app.url.getDomain() == "example.com", "absolute app url kept");
    }
    if (auto* b = cat.find ("bloom"))
    {
        expect (b->name == "bloom" && b->bundle == "bloom", "name and bundle default to id");
        expect (! b->hasApp(), "null app");
        expect (b->plugin.url.toString (false).endsWith ("bloom%200.1.0.pkg"), "relative url escaped");
    }
    Catalog bad;
    expect (Catalog::parse (R"({"schema": 2})", {}, bad).failed(), "rejects a newer schema");
    expect (Catalog::parse (R"({"schema": 1, "packages": [ { "id": "x" } ]})", {}, bad).failed(), "rejects an incomplete entry");

    auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("onga-sync-test", "");
    tmp.createDirectory();
    const auto localCat = tmp.getChildFile ("rel/catalog.json");
    expect (resolveUrl (juce::URL (localCat), "a b.pkg").getLocalFile() == tmp.getChildFile ("rel/a b.pkg"), "relative to a local catalog");

    std::cout << "scan" << std::endl;
    const auto root = tmp.getChildFile ("root"), home = tmp.getChildFile ("home");
    const auto where = Locations::under (root, home);
    auto makeBundle = [] (const juce::File& b, const juce::String& v)
    {
        b.getChildFile ("Contents").createDirectory();
        b.getChildFile ("Contents/Info.plist").replaceWithText (
            "<?xml version=\"1.0\"?><plist version=\"1.0\"><dict><key>CFBundleName</key><string>x</string>"
            "<key>CFBundleShortVersionString</key><string>" + v + "</string><key>CFBundleVersion</key><string>9</string></dict></plist>");
    };
    Package wiz = *cat.find ("wizard");
    expect (! scan (wiz, where).installed(), "nothing installed");
    makeBundle (where.vst3.getChildFile ("The Wizard.vst3"), "1.1.5");
    auto s = scan (wiz, where);
    expect (s.installed() && ! s.au && s.vst3 && s.version == "1.1.5", "VST3 only, version from its plist");
    makeBundle (where.components.getChildFile ("The Wizard.component"), "1.1.6");
    makeBundle (home.getChildFile ("Library/Audio/Plug-Ins/VST3/The Wizard.vst3"), "0.9");
    s = scan (wiz, where);
    expect (s.au && s.version == "1.1.6", "AU version wins");
    expect (s.userCopies, "per-user copy noticed");

    std::cout << "scripts" << std::endl;
    expect (shellQuote ("it's") == "'it'\\''s'", "shell quoting");
    const auto inst = installScript ({ juce::File ("/tmp/a b.pkg") });
    expect (inst.contains ("installer -pkg '/tmp/a b.pkg' -target /"), "install script quotes paths");
    Package vox;
    vox.id = "voxmaster"; vox.name = "voxmaster"; vox.bundle = "voxmaster";
    vox.oldNames = { "Voxmaster", "Vox Master 3000" };
    vox.receipts = { "com.ongatools.voxmaster.install" };
    const auto un = uninstallScript (vox, { "ben", juce::File ("/Users/ben") }, "2026-01-01 000000");
    expect (un.contains ("to_trash '/Library/Audio/Plug-Ins/VST3/Vox Master 3000.vst3' system"), "old names trashed");
    expect (un.contains ("to_trash '/Users/ben/Library/Audio/Plug-Ins/Components/voxmaster.component' user"), "per-user copies trashed");
    expect (un.contains ("pkgutil --forget 'com.ongatools.voxmaster.install'"), "receipts forgotten");
    expect (un.contains ("chown -R 'ben'"), "Trash handed back to the user");
    expect (! un.contains ("rm -"), "nothing deleted outright");

    std::cout << "accounts" << std::endl;
    auto dev = Account::developer();
    expect (dev.signedIn() && dev.licenseFor ("anything") == LicenseState::developer, "developer licensed for everything");
    Account a;
    a.userId = "u1"; a.provider = Provider::google;
    a.licenses.push_back ({ "bloom", "perpetual", {} });
    a.licenses.push_back ({ "panna", "trial", juce::Time::getCurrentTime() + juce::RelativeTime::days (3) });
    a.licenses.push_back ({ "mageq", "subscription", juce::Time::getCurrentTime() - juce::RelativeTime::days (1) });
    expect (a.licenseFor ("bloom") == LicenseState::licensed, "perpetual");
    expect (a.licenseFor ("panna") == LicenseState::trial, "trial");
    expect (a.licenseFor ("mageq") == LicenseState::none, "expired subscription");
    expect (a.licenseFor ("wizard") == LicenseState::none, "unlicensed");
    const auto back = Account::fromVar (juce::JSON::parse (juce::JSON::toString (a.toVar())));
    expect (back.userId == "u1" && back.provider == Provider::google && back.licenses.size() == 3
            && back.licenseFor ("panna") == LicenseState::trial, "round-trips through JSON");

    {
        const auto store = tmp.getChildFile ("account.json");
        AccountManager m (store, std::make_unique<NotLiveYet>());
        expect (! m.signedIn(), "starts signed out");
        m.signInDeveloper();
        expect (m.signedIn() == AccountManager::devBypassAvailable(), "dev bypass only in dev builds");
        AccountManager again (store, std::make_unique<NotLiveYet>());
        expect (again.signedIn() == AccountManager::devBypassAvailable(), "session survives a relaunch");
        again.signOut();
        expect (! again.signedIn() && ! store.exists(), "sign out forgets the session");
    }

    tmp.deleteRecursively();
    std::cout << (failures == 0 ? "all tests passed" : juce::String (failures) + " FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;   // MessageManager for the account code
    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add (juce::String::fromUTF8 (argv[i]));

    const auto cmd = args[0];
    if (cmd == "test")
        return runTests();

    if (cmd == "install-script" && args.size() >= 2)
    {
        juce::Array<juce::File> pkgs;
        for (int i = 1; i < args.size(); ++i)
            pkgs.add (juce::File::getCurrentWorkingDirectory().getChildFile (args[i]));
        std::cout << installScript (pkgs);
        return 0;
    }

    if ((cmd == "check" || cmd == "verify" || cmd == "uninstall-script") && args.size() >= 2)
    {
        Catalog cat;
        if (auto r = loadCatalog (args[1], cat); r.failed())
            return fail (r.getErrorMessage());

        if (cmd == "check")
            return check (cat, args.contains ("--expect-current"));
        if (cmd == "verify")
            return verify (cat);

        auto* p = cat.find (args[2]);
        if (p == nullptr)
            return fail ("no package '" + args[2] + "' in the catalog");
        std::cout << uninstallScript (*p, invokingUser(), juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H%M%S"));
        return 0;
    }

    std::cerr << "usage: OngaSyncCli test | check <catalog> [--expect-current] | verify <catalog>\n"
                 "       | install-script <pkg>... | uninstall-script <catalog> <id>" << std::endl;
    return 2;
}
