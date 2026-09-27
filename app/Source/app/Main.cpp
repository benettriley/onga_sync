#include "Engine.h"
#include "../core/Account.h"
#include "../ui/MainPanel.h"

#ifndef ONGA_SYNC_VERSION
 #define ONGA_SYNC_VERSION "0.0.0"
#endif

namespace onga::sync
{
/** Settings and the account: ~/Library/Application Support/ONGA/Sync on a Mac. */
static juce::File dataDirectory()
{
    const auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    return base.getChildFile ("Application Support/ONGA/Sync");
   #else
    return base.getChildFile ("ONGA/Sync");
   #endif
}

/** Downloads in flight: ~/Library/Caches/com.ongatools.sync. */
static juce::File cacheDirectory()
{
   #if JUCE_MAC
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Caches/com.ongatools.sync");
   #else
    return dataDirectory().getChildFile ("cache");
   #endif
}

/** Scales the 820 x 580 panel as a unit, like every ONGA editor. */
class ScaledContent final : public juce::Component
{
public:
    ScaledContent (Engine& e, AccountManager& a) : panel (e, a)
    {
        addAndMakeVisible (panel);
        setSize (ui::metrics::panelW, ui::metrics::panelH);
    }

    void resized() override
    {
        panel.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) ui::metrics::panelW));
    }

private:
    MainPanel panel;
};

class SyncWindow final : public juce::DocumentWindow
{
public:
    SyncWindow (Engine& e, AccountManager& a)
        : juce::DocumentWindow ("ONGA Sync", ui::surfaces::face.base, closeButton | minimiseButton)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (new ScaledContent (e, a), true);
        setResizable (true, false);
        const int w = ui::metrics::panelW, h = ui::metrics::panelH;
        getConstrainer()->setFixedAspectRatio ((double) w / (double) h);
        setResizeLimits (w * 3 / 4, h * 3 / 4, w * 2, h * 2);
        centreWithSize (w, h);
        setVisible (true);
    }

    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
};

//==============================================================================
class SyncApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "ONGA Sync"; }
    const juce::String getApplicationVersion() override { return ONGA_SYNC_VERSION; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String&) override
    {
        const auto data = dataDirectory();
        data.createDirectory();

        accounts = std::make_unique<AccountManager> (data.getChildFile ("account.json"), std::make_unique<NotLiveYet>());
        engine = std::make_unique<Engine> (data, cacheDirectory(), ONGA_SYNC_VERSION);
        engine->onSelfUpdated = [this] { offerRelaunch(); };
        window = std::make_unique<SyncWindow> (*engine, *accounts);

        // ONGA_SYNC_SNAPSHOT=/path/shot.png: render the window at 2x once the catalog
        // has loaded, then quit. For screenshots in CI and docs.
        const auto shot = juce::SystemStats::getEnvironmentVariable ("ONGA_SYNC_SNAPSHOT", {});
        if (shot.isNotEmpty())
            juce::Timer::callAfterDelay (2500, [this, shot]
            {
                auto* content = window->getContentComponent();
                const auto img = content->createComponentSnapshot (content->getLocalBounds(), true, 2.0f);
                juce::File file (shot);
                file.deleteFile();
                if (juce::FileOutputStream out (file); out.openedOk())
                    juce::PNGImageFormat().writeImageToStream (img, out);
                quit();
            });
    }

    void shutdown() override
    {
        window = nullptr;
        engine = nullptr;
        accounts = nullptr;
    }

    void systemRequestedQuit() override
    {
        if (engine != nullptr && engine->job().running)
        {
            juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                              .withIconType (juce::MessageBoxIconType::NoIcon)
                                              .withTitle ("STILL WORKING")
                                              .withMessage ("ONGA Sync is in the middle of a job. Quit anyway?")
                                              .withButton ("QUIT")
                                              .withButton ("KEEP GOING"),
                                          [] (int r) { if (r == 1) juce::JUCEApplication::quit(); });
            return;
        }
        quit();
    }

    void anotherInstanceStarted (const juce::String&) override
    {
        if (window != nullptr)
            window->toFront (true);
    }

private:
    void offerRelaunch()
    {
        juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                          .withIconType (juce::MessageBoxIconType::NoIcon)
                                          .withTitle ("ONGA SYNC UPDATED")
                                          .withMessage ("Restart ONGA Sync to use the new version.")
                                          .withButton ("RESTART")
                                          .withButton ("LATER"),
                                      [] (int r)
                                      {
                                          if (r != 1)
                                              return;
                                         #if JUCE_MAC
                                          const auto app = juce::File::getSpecialLocation (juce::File::currentApplicationFile);
                                          juce::ChildProcess relaunch;
                                          relaunch.start (juce::StringArray { "/bin/sh", "-c", "sleep 1; open \"$0\"", app.getFullPathName() }, 0);
                                         #endif
                                          juce::JUCEApplication::quit();
                                      });
    }

    std::unique_ptr<AccountManager> accounts;
    std::unique_ptr<Engine> engine;
    std::unique_ptr<SyncWindow> window;
};
} // namespace onga::sync

START_JUCE_APPLICATION (onga::sync::SyncApplication)
