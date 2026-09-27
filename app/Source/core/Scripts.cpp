#include "Scripts.h"

namespace onga::sync
{
UserContext UserContext::current()
{
    return { juce::SystemStats::getLogonName(), juce::File::getSpecialLocation (juce::File::userHomeDirectory) };
}

juce::String shellQuote (const juce::String& s)
{
    return "'" + s.replace ("'", "'\\''") + "'";
}

juce::String installScript (const juce::Array<juce::File>& pkgs)
{
    juce::String s;
    s << "#!/bin/sh\n"
      << "set -e\n";
    for (auto& f : pkgs)
        s << "/usr/sbin/installer -pkg " << shellQuote (f.getFullPathName()) << " -target /\n";
    s << "killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true\n";
    return s;
}

juce::String uninstallScript (const Package& p, const UserContext& user, const juce::String& stamp)
{
    juce::StringArray names;
    names.add (p.bundle);
    names.addArray (p.oldNames);
    names.removeDuplicates (false);

    const auto trash = user.home.getChildFile (".Trash").getChildFile ("ONGA uninstalled " + p.name + " " + stamp);

    juce::String s;
    s << "#!/bin/sh\n"
      << "set -u\n"
      << "trash=" << shellQuote (trash.getFullPathName()) << "\n"
      << "# On case-insensitive APFS two names can be one path: the first move wins.\n"
      << "to_trash() {\n"
      << "  if [ -e \"$1\" ]; then mkdir -p \"$trash/$2\" && mv \"$1\" \"$trash/$2/\" && echo \"moved $1 to the Trash\"; fi\n"
      << "}\n";

    const auto home = user.home.getFullPathName();
    for (auto& n : names)
    {
        s << "to_trash " << shellQuote ("/Library/Audio/Plug-Ins/Components/" + n + ".component") << " system\n"
          << "to_trash " << shellQuote ("/Library/Audio/Plug-Ins/VST3/" + n + ".vst3") << " system\n"
          << "to_trash " << shellQuote ("/Applications/" + n + ".app") << " system\n"
          << "to_trash " << shellQuote (home + "/Library/Audio/Plug-Ins/Components/" + n + ".component") << " user\n"
          << "to_trash " << shellQuote (home + "/Library/Audio/Plug-Ins/VST3/" + n + ".vst3") << " user\n";
    }

    s << "[ -d \"$trash\" ] && chown -R " << shellQuote (user.userName) << " \"$trash\" || true\n";
    for (auto& r : p.receipts)
        s << "pkgutil --forget " << shellQuote (r) << " >/dev/null 2>&1 || true\n";
    s << "killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true\n"
      << "exit 0\n";
    return s;
}

juce::Result runAsAdmin (const juce::String& script, juce::String& output)
{
   #if JUCE_MAC
    auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("onga-sync", ".sh");
    if (! file.replaceWithText (script))
        return juce::Result::fail ("Couldn't write a temporary script.");

    // The path goes in as an argument, so nothing in it is ever parsed as AppleScript.
    juce::ChildProcess proc;
    const juce::StringArray cmd {
        "/usr/bin/osascript",
        "-e", "on run argv",
        "-e", "do shell script \"/bin/sh \" & quoted form of (item 1 of argv) & \" 2>&1\" with administrator privileges",
        "-e", "end run",
        file.getFullPathName()
    };
    if (! proc.start (cmd, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
    {
        file.deleteFile();
        return juce::Result::fail ("Couldn't start osascript.");
    }
    output = proc.readAllProcessOutput();
    proc.waitForProcessToFinish (-1);
    const auto code = proc.getExitCode();
    file.deleteFile();

    if (code == 0)
        return juce::Result::ok();
    if (output.contains ("-128") || output.containsIgnoreCase ("User canceled"))
        return juce::Result::fail ("cancelled");
    return juce::Result::fail (output.trim().isNotEmpty() ? output.trim() : "The installer stopped with code " + juce::String (code) + ".");
   #else
    juce::ignoreUnused (script);
    output = {};
    return juce::Result::fail ("Installing needs macOS.");
   #endif
}
} // namespace onga::sync
