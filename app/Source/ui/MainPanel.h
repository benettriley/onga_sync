#pragma once

#include "LibraryScreen.h"
#include "SignInView.h"

namespace onga::sync
{
/** The ONGA Sync panel, laid out at 820 x 580 and scaled as a unit by the window.

      +--------------------------- ONGA SYNC ---------------------------+
      | [ SYNC logo ] | LIBRARY screen: every plug-in, installed/latest |
      | ACCOUNT plate |                                                 |
      | SUITE plate   | selected plug-in plate: install/update/uninstall|
      +-- ONGA TOOLS ------------------------ SYNC 0.1.0 · SUITE 2026.1 --+
*/
class MainPanel final : public juce::Component,
                        private juce::ChangeListener,
                        private juce::Timer
{
public:
    MainPanel (Engine&, AccountManager&);
    ~MainPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;   // periodic update check

    void accountChanged();
    void refreshControls();
    void primaryAction();
    void confirmUninstall();
    void editSource();
    void showNotice (const juce::String&, bool isError);

    const Package* selectedPackage() const;
    bool canInstall (const Package&) const;

    Engine& engine;
    AccountManager& accounts;
    ui::OngaLookAndFeel laf;

    ui::PixelLogo logo;
    LibraryScreen library;
    SignInView signInView;

    juce::TextButton signOut { "SIGN OUT" }, check { "CHECK NOW" }, updateAll { "UPDATE ALL" },
                     updateSync { "UPDATE ONGA SYNC" }, source { "SOURCE..." },
                     action { "INSTALL" }, remove { "UNINSTALL" }, cancel { "CANCEL" };
    juce::ToggleButton apps { "STANDALONE APPS" };

    juce::String notice;
    bool noticeIsError = false;
};
} // namespace onga::sync
