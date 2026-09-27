#include "MainPanel.h"

#include "SyncLogo.h"

namespace onga::sync
{
using namespace onga::ui;

namespace
{
    // Design-px layout (820 x 580).
    const juce::Rectangle<float> kLogo    { 14.0f, 40.0f, 200.0f, 96.0f };
    const juce::Rectangle<float> kAccount { 14.0f, 148.0f, 200.0f, 142.0f };
    const juce::Rectangle<float> kSuite   { 14.0f, 302.0f, 200.0f, 242.0f };
    const juce::Rectangle<float> kScreen  { 226.0f, 40.0f, 580.0f, 330.0f };
    const juce::Rectangle<float> kDetail  { 226.0f, 382.0f, 580.0f, 162.0f };
    const juce::Rectangle<float> kRight   { 226.0f, 40.0f, 580.0f, 504.0f };

    constexpr int kCheckHours = 6;

    juce::String licenceText (LicenseState s)
    {
        switch (s)
        {
            case LicenseState::developer: return "DEVELOPER";
            case LicenseState::licensed:  return "LICENSED";
            case LicenseState::trial:     return "TRIAL";
            case LicenseState::none:      break;
        }
        return "NO LICENCE";
    }
}

MainPanel::MainPanel (Engine& e, AccountManager& a)
    : engine (e), accounts (a), logo ("ONGA Sync", syncLogo()), library (e, a), signInView (a)
{
    setLookAndFeel (&laf);
    setSize (metrics::panelW, metrics::panelH);

    addAndMakeVisible (logo);
    addAndMakeVisible (library);
    addChildComponent (signInView);

    for (auto* b : { &signOut, &check, &updateAll, &updateSync, &action, &remove, &cancel })
        addAndMakeVisible (*b);
    addAndMakeVisible (apps);
    source.getProperties().set ("onga.flat", true);
    addChildComponent (source);
    source.setVisible (AccountManager::devBypassAvailable());

    signOut.onClick = [this] { accounts.signOut(); };
    check.onClick = [this] { engine.refresh(); };
    updateAll.onClick = [this]
    {
        juce::StringArray ids;
        for (auto& id : engine.updatable())
            if (auto* p = engine.catalog().find (id); p != nullptr && canInstall (*p))
                ids.add (id);
        engine.install (ids);
    };
    updateSync.onClick = [this] { engine.updateSelf(); };
    action.onClick = [this] { primaryAction(); };
    remove.onClick = [this] { confirmUninstall(); };
    cancel.onClick = [this] { engine.cancel(); };
    source.onClick = [this] { editSource(); };

    apps.setToggleState (engine.settings().installApps, juce::dontSendNotification);
    apps.onClick = [this]
    {
        engine.settings().installApps = apps.getToggleState();
        engine.saveSettings();
    };

    library.onSelectionChanged = [this] { refreshControls(); repaint(); };
    engine.onJobFinished = [this] (const juce::String& text, bool failed) { showNotice (text, failed); };

    engine.addChangeListener (this);
    accounts.addChangeListener (this);

    accountChanged();
    startTimer (kCheckHours * 60 * 60 * 1000);
}

MainPanel::~MainPanel()
{
    engine.onJobFinished = nullptr;
    engine.removeChangeListener (this);
    accounts.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

//==============================================================================
void MainPanel::changeListenerCallback (juce::ChangeBroadcaster* from)
{
    if (from == &accounts)
    {
        accountChanged();
        return;
    }

    // Keep a selection once the catalog arrives.
    if (library.getSelected().isEmpty() && ! engine.catalog().packages.empty())
        library.setSelected (engine.catalog().packages.front().id);

    refreshControls();
    library.repaint();
    repaint();
}

void MainPanel::timerCallback()
{
    if (accounts.signedIn() && ! engine.job().running)
        engine.refresh();
}

void MainPanel::accountChanged()
{
    const bool in = accounts.signedIn();
    signInView.setVisible (! in);
    library.setVisible (in);
    if (in && ! engine.hasCatalog() && ! engine.isChecking())
        engine.refresh();
    notice = {};
    refreshControls();
    repaint();
}

const Package* MainPanel::selectedPackage() const
{
    return engine.catalog().find (library.getSelected());
}

bool MainPanel::canInstall (const Package& p) const
{
    return accounts.licenseFor (p.id) != LicenseState::none;
}

void MainPanel::refreshControls()
{
    const bool in = accounts.signedIn();
    const bool running = engine.job().running;
    auto* p = selectedPackage();

    signOut.setVisible (in);
    signOut.setEnabled (! running);
    check.setEnabled (in && ! running && ! engine.isChecking());
    apps.setEnabled (in && ! running);

    int n = 0;
    for (auto& id : engine.updatable())
        if (auto* u = engine.catalog().find (id); u != nullptr && canInstall (*u))
            ++n;
    updateAll.setButtonText (n > 0 ? "UPDATE ALL (" + juce::String (n) + ")" : juce::String ("UPDATE ALL"));
    updateAll.setEnabled (in && ! running && n > 0);

    updateSync.setVisible (in && engine.selfUpdateAvailable());
    updateSync.setButtonText ("UPDATE SYNC TO " + engine.catalog().syncVersion);
    updateSync.setEnabled (! running);

    const bool detail = in && p != nullptr;
    action.setVisible (detail && ! running);
    remove.setVisible (detail);
    cancel.setVisible (in && running);

    if (detail)
    {
        const auto status = engine.statusOf (p->id);
        action.setButtonText (status == Status::notInstalled      ? juce::String ("INSTALL")
                              : status == Status::updateAvailable ? "UPDATE TO " + p->version
                                                                  : juce::String ("REINSTALL"));
        action.setEnabled (canInstall (*p));
        remove.setEnabled (! running && status != Status::notInstalled);
    }
}

void MainPanel::primaryAction()
{
    if (auto* p = selectedPackage(); p != nullptr && canInstall (*p))
    {
        notice = {};
        engine.install ({ p->id });
    }
}

void MainPanel::confirmUninstall()
{
    auto* p = selectedPackage();
    if (p == nullptr)
        return;

    const auto id = p->id;
    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::NoIcon)
                                      .withTitle ("UNINSTALL " + p->name.toUpperCase())
                                      .withMessage ("Moves the Audio Unit, VST3 and app to the Trash. Your presets and settings stay.")
                                      .withButton ("UNINSTALL")
                                      .withButton ("CANCEL")
                                      .withAssociatedComponent (this),
                                  [safe = juce::Component::SafePointer<MainPanel> (this), id] (int result)
                                  {
                                      if (safe != nullptr && result == 1)
                                          safe->engine.uninstall (id);
                                  });
}

void MainPanel::editSource()
{
    auto* w = new juce::AlertWindow ("CATALOG SOURCE",
                                     "Where ONGA Sync reads catalog.json: a URL (a test pre-release, say) or a folder path.",
                                     juce::MessageBoxIconType::NoIcon, this);
    w->setLookAndFeel (&laf);
    w->addTextEditor ("url", engine.settings().catalogUrl, "");
    w->addButton ("USE", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("DEFAULT", 2);
    w->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create (
        [safe = juce::Component::SafePointer<MainPanel> (this), w] (int result)
        {
            if (safe == nullptr || result == 0)
                return;
            auto text = result == 1 ? w->getTextEditorContents ("url").trim() : juce::String();
            if (text.endsWithChar ('/') || (text.isNotEmpty() && ! text.endsWithIgnoreCase (".json")))
                text = text.trimCharactersAtEnd ("/") + "/catalog.json";
            safe->engine.settings().catalogUrl = text;
            safe->engine.saveSettings();
            safe->engine.refresh();
        }), true);
}

void MainPanel::showNotice (const juce::String& text, bool isError)
{
    notice = text;
    noticeIsError = isError;
    repaint();
}

//==============================================================================
void MainPanel::paint (juce::Graphics& g)
{
    const auto all = getLocalBounds().toFloat();
    paintFaceplate (g, all);
    paintTitleBar (g, all.withHeight (metrics::titleH), "ONGA SYNC");

    const auto& acct = accounts.current();
    const auto& cat = engine.catalog();

    // Footer
    juce::String right = "SYNC " + engine.appVersion();
    if (engine.hasCatalog()) right << dot() << "SUITE " << cat.suite;
    if (acct.provider == Provider::developer) right << dot() << "DEV BYPASS";
    paintFooter (g, all.withTop (all.getBottom() - metrics::footerH), right);

    // Account
    drawShadow (g, kAccount);
    drawPlate (g, kAccount);
    drawLabel (g, "ACCOUNT", kAccount.reduced (10.0f, 8.0f).withHeight (14.0f));
    const auto text = kAccount.reduced (10.0f, 0.0f);
    if (acct.signedIn())
    {
        g.setColour (colours::ink);
        g.setFont (Fonts::get().bold (12.0f));
        g.drawText (acct.displayName.toUpperCase(), text.withY (kAccount.getY() + 28.0f).withHeight (16.0f), juce::Justification::centredLeft, true);
        g.setFont (Fonts::get().regular (type::readout));
        g.setColour (colours::subInk);
        g.drawText (acct.email, text.withY (kAccount.getY() + 46.0f).withHeight (14.0f), juce::Justification::centredLeft, true);
        const auto via = acct.provider == Provider::developer ? juce::String ("DEV BYPASS")
                                                              : "VIA " + providerName (acct.provider).toUpperCase();
        drawReadout (g, { text.getX(), kAccount.getY() + 68.0f, readoutWidth (via, type::readout, 8.0f, 90.0f), 18.0f }, via);
    }
    else
    {
        g.setColour (colours::subInk);
        g.setFont (Fonts::get().regular (type::readout));
        g.drawFittedText ("Signed out. Sign in to see your plug-ins and licences.",
                          text.withY (kAccount.getY() + 30.0f).withHeight (40.0f).toNearestInt(), juce::Justification::topLeft, 3);
    }

    // Suite
    drawShadow (g, kSuite);
    drawPlate (g, kSuite);
    drawLabel (g, "SUITE", kSuite.reduced (10.0f, 8.0f).withHeight (14.0f));
    g.setColour (colours::subInk);
    g.setFont (Fonts::get().regular (type::readout));
    const auto line = [&] (int i, const juce::String& s)
    {
        g.drawText (s, kSuite.reduced (10.0f, 0.0f).withY (kSuite.getY() + 28.0f + 16.0f * (float) i).withHeight (14.0f),
                    juce::Justification::centredLeft, true);
    };
    line (0, "CATALOG  " + (engine.hasCatalog() ? cat.suite : juce::String ("-")));
    line (1, "CHECKED  " + (engine.lastChecked().toMilliseconds() == 0 ? juce::String ("-")
                                                                       : engine.lastChecked().formatted ("%H:%M")));
    line (2, "SYNC     " + engine.appVersion() + (engine.selfUpdateAvailable() ? "  NEW " + cat.syncVersion : juce::String()));

    if (! acct.signedIn())
        return;

    // Selected plug-in
    drawShadow (g, kDetail);
    drawPlate (g, kDetail);
    auto* p = selectedPackage();
    const auto body = kDetail.reduced (12.0f, 0.0f).withTrimmedRight (190.0f);
    if (p != nullptr)
    {
        drawLabel (g, p->name.toUpperCase(), kDetail.reduced (12.0f, 8.0f).withHeight (14.0f));

        g.setColour (colours::subInk);
        g.setFont (Fonts::get().regular (type::readout));
        g.drawFittedText (p->blurb, body.withY (kDetail.getY() + 28.0f).withHeight (30.0f).toNearestInt(),
                          juce::Justification::topLeft, 2);

        const auto st = engine.stateOf (p->id);
        juce::StringArray parts;
        if (st.au) parts.add ("AU");
        if (st.vst3) parts.add ("VST3");
        if (st.app) parts.add ("APP");
        const auto where = st.installed() ? "INSTALLED " + st.version + dot() + parts.joinIntoString (" + ")
                                          : juce::String ("NOT INSTALLED");
        g.setColour (colours::ink);
        g.drawText (where, body.withY (kDetail.getY() + 64.0f).withHeight (14.0f), juce::Justification::centredLeft, true);
        if (st.userCopies)
        {
            g.setColour (colours::subInk);
            g.drawText ("ALSO IN ~/LIBRARY (A DEV BUILD?). INSTALLING TIDIES IT UP.",
                        body.withY (kDetail.getY() + 80.0f).withHeight (14.0f), juce::Justification::centredLeft, true);
        }

        const auto lic = licenceText (accounts.licenseFor (p->id));
        drawReadout (g, { body.getX(), kDetail.getY() + 100.0f, readoutWidth (lic, type::readout, 8.0f, 90.0f), 18.0f }, lic);
    }

    // Job / notice line
    const auto& job = engine.job();
    const auto status = body.withY (kDetail.getBottom() - 24.0f).withHeight (14.0f);
    g.setFont (Fonts::get().bold (type::label));
    if (job.running)
    {
        g.setColour (colours::ink);
        juce::String s = job.title;
        if (job.progress >= 0.0) s << "  " << juce::roundToInt (job.progress * 100.0) << "%";
        else if (job.currentId.isEmpty()) s << "  CONFIRM IN THE PASSWORD PROMPT";
        g.drawText (s, status, juce::Justification::centredLeft, true);
    }
    else if (notice.isNotEmpty())
    {
        g.setColour (noticeIsError ? colours::red : colours::ink);
        g.drawText (noticeIsError ? notice : notice.toUpperCase(), status, juce::Justification::centredLeft, true);
    }
}

void MainPanel::resized()
{
    logo.setBounds (kLogo.toNearestInt());
    library.setBounds (kScreen.toNearestInt());
    signInView.setBounds (kRight.toNearestInt());

    const int bx = (int) kAccount.getX() + 10, bw = (int) kAccount.getWidth() - 20;
    signOut.setBounds (bx, (int) kAccount.getBottom() - 38, bw, 26);

    const int sy = (int) kSuite.getY();
    check.setBounds (bx, sy + 84, bw, 26);
    updateAll.setBounds (bx, sy + 118, bw, 26);
    apps.setBounds (bx, sy + 154, bw, 20);
    updateSync.setBounds (bx, sy + 184, bw, 26);

    const int ax = (int) kDetail.getRight() - 12 - 170, ay = (int) kDetail.getY() + 30;
    action.setBounds (ax, ay, 170, 30);
    cancel.setBounds (ax, ay, 170, 30);
    remove.setBounds (ax, ay + 42, 170, 26);

    source.setBounds (104, metrics::panelH - (int) metrics::footerH + 3, 76, 16);
}
} // namespace onga::sync
