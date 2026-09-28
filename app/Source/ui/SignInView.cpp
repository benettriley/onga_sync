#include "SignInView.h"

namespace onga::sync
{
using namespace onga::ui;

namespace
{
    constexpr float kFormW = 300.0f;
}

SignInView::SignInView (AccountManager& a) : accounts (a)
{
    const bool live = accounts.getService().isLive();

    for (auto* ed : { &email, &password })
    {
        ed->setFont (Fonts::get().regular (11.0f));
        ed->setIndents (8, 6);
        ed->setEnabled (live);
        addAndMakeVisible (*ed);
    }
    email.setTextToShowWhenEmpty ("email", colours::ghost);
    password.setTextToShowWhenEmpty ("password", colours::ghost);
    password.setPasswordCharacter ((juce::juce_wchar) 0x2022);
    password.onReturnKey = [this] { submitPassword(); };

    google.setEnabled (live);
    signIn.setEnabled (live);
    google.onClick = [this]
    {
        setBusy (true, "Finish signing in in your browser...");
        accounts.signInWithGoogle ([safe = juce::Component::SafePointer<SignInView> (this)] (juce::Result r)
                                   { if (safe != nullptr) safe->finished (r); });
    };
    signIn.onClick = [this] { submitPassword(); };
    addAndMakeVisible (google);
    addAndMakeVisible (signIn);

    skip.onClick = [this] { accounts.signInDeveloper(); };
    skip.setVisible (AccountManager::devBypassAvailable());
    addChildComponent (skip);
}

void SignInView::submitPassword()
{
    if (! accounts.getService().isLive())
        return;
    if (! email.getText().containsChar ('@') || password.isEmpty())
    {
        message = "Enter your email and password.";
        messageIsError = true;
        repaint();
        return;
    }
    setBusy (true, "Signing in...");
    accounts.signInWithPassword (email.getText().trim(), password.getText(),
                                 [safe = juce::Component::SafePointer<SignInView> (this)] (juce::Result r)
                                 { if (safe != nullptr) safe->finished (r); });
}

void SignInView::finished (juce::Result r)
{
    password.clear();
    setBusy (false, r.wasOk() ? juce::String() : r.getErrorMessage());
    messageIsError = r.failed();
    repaint();
}

void SignInView::setBusy (bool busy, const juce::String& text)
{
    const bool live = accounts.getService().isLive();
    for (juce::Component* c : { (juce::Component*) &google, (juce::Component*) &signIn,
                                (juce::Component*) &email, (juce::Component*) &password })
        c->setEnabled (live && ! busy);
    message = text;
    messageIsError = false;
    repaint();
}

void SignInView::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    drawPlate (g, r);
    drawLabel (g, "SIGN IN", r.reduced (12.0f, 10.0f).withHeight (14.0f));

    const auto form = juce::Rectangle<float> (kFormW, r.getHeight()).withCentre (r.getCentre());

    g.setColour (colours::ink);
    g.setFont (Fonts::get().bold (type::screenTitle));
    g.drawText ("ONE ACCOUNT. EVERY ONGA PLUG-IN.", form.withY (r.getY() + 44.0f).withHeight (20.0f).expanded (60.0f, 0.0f),
                juce::Justification::centred, false);
    g.setColour (colours::subInk);
    g.setFont (Fonts::get().regular (type::readout));
    g.drawFittedText ("Your licences live in your account, so a new Mac is one sign-in away.",
                      form.withY (r.getY() + 68.0f).withHeight (30.0f).toNearestInt(), juce::Justification::centredTop, 2);

    // "or" rule between Google and email.
    const float ory = google.getBottom() + 18.0f;
    g.setColour (colours::ink);
    fillChecker (g, juce::Rectangle<float> (form.getX(), ory, form.getWidth() * 0.5f - 18.0f, 1.0f), colours::ink);
    fillChecker (g, juce::Rectangle<float> (form.getCentreX() + 18.0f, ory, form.getWidth() * 0.5f - 18.0f, 1.0f), colours::ink);
    g.setFont (Fonts::get().bold (type::label));
    g.drawText ("OR", juce::Rectangle<float> (36.0f, 14.0f).withCentre ({ form.getCentreX(), ory }), juce::Justification::centred, false);

    auto note = juce::Rectangle<float> (form.getX(), (float) signIn.getBottom() + 14.0f, form.getWidth(), 32.0f);
    g.setFont (Fonts::get().regular (type::caption + 1.0f));
    if (message.isNotEmpty())
    {
        g.setColour (messageIsError ? colours::red : colours::subInk);
        g.drawFittedText (message, note.toNearestInt(), juce::Justification::centredTop, 2);
    }
    else if (! accounts.getService().isLive())
    {
        g.setColour (colours::subInk);
        g.drawFittedText ("ONGA accounts are on the way. Google and email sign-in switch on when they're live.",
                          note.toNearestInt(), juce::Justification::centredTop, 2);
    }

    if (skip.isVisible())
    {
        g.setColour (colours::subInk);
        g.setFont (Fonts::get().regular (type::caption));
        g.drawText ("DEVELOPMENT BUILD" + dot() + "LICENSED FOR EVERYTHING",
                    juce::Rectangle<float> (form.getX() - 40.0f, (float) skip.getBottom() + 6.0f, form.getWidth() + 80.0f, 14.0f),
                    juce::Justification::centred, false);
    }
}

void SignInView::resized()
{
    const auto r = getLocalBounds().toFloat();
    const auto form = juce::Rectangle<float> (kFormW, r.getHeight()).withCentre (r.getCentre());
    const int x = juce::roundToInt (form.getX()), w = juce::roundToInt (kFormW);

    int y = juce::roundToInt (r.getY()) + 116;
    google.setBounds (x, y, w, 30);            y += 30 + 36;
    email.setBounds (x, y, w, 26);             y += 26 + 8;
    password.setBounds (x, y, w, 26);          y += 26 + 12;
    signIn.setBounds (x, y, w, 30);

    skip.setBounds (x + w / 4, getHeight() - 64, w / 2, 26);
}
} // namespace onga::sync
