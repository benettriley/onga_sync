#pragma once

#include "Catalog.h"

/*
    The only things ONGA Sync does as root, as plain sh scripts. The app shows the macOS
    password prompt once per job and runs one script for the whole batch; CI runs the same
    scripts (via OngaSyncCli) with sudo, so what ships is what gets tested.
*/
namespace onga::sync
{
struct UserContext
{
    juce::String userName;   // owner of the Trash folder
    juce::File home;

    static UserContext current();
};

/** Installs each .pkg system-wide, then refreshes the Audio Unit cache. */
juce::String installScript (const juce::Array<juce::File>& pkgs);

/** Moves a package's bundles (current and old names, system-wide and per-user) to the
    user's Trash, forgets its receipts and refreshes the Audio Unit cache. Nothing is
    deleted outright: a mistake can be undone from the Trash. */
juce::String uninstallScript (const Package& p, const UserContext& user, const juce::String& stamp);

/** Single-quotes a value for sh. */
juce::String shellQuote (const juce::String& s);

/** Runs a script as root after the standard macOS administrator prompt. `output` gets
    what it printed. Fails with "cancelled" if the user dismisses the prompt. */
juce::Result runAsAdmin (const juce::String& script, juce::String& output);
} // namespace onga::sync
