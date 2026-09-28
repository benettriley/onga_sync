#include "LibraryScreen.h"

namespace onga::sync
{
using namespace onga::ui;

namespace
{
    // Column x offsets inside the screen (design px).
    constexpr float cMarker = 12.0f, cName = 26.0f, cType = 170.0f, cInstalled = 296.0f, cLatest = 378.0f, cStatus = 456.0f;

    struct Look { juce::String text; juce::Colour colour; };

    Look statusLook (Status s, LicenseState lic)
    {
        switch (s)
        {
            case Status::upToDate:        return { "UP TO DATE", colours::signal };
            case Status::updateAvailable: return { "UPDATE", colours::amber };
            case Status::newer:           return { "NEWER", colours::ghost };
            case Status::notInstalled:    return lic == LicenseState::none ? Look { "NO LICENCE", colours::dim }
                                                                          : Look { "NOT INSTALLED", colours::ghost };
            case Status::unknown:         break;
        }
        return { "-", colours::dim };
    }
}

LibraryScreen::LibraryScreen (Engine& e, AccountManager& a) : engine (e), accounts (a)
{
    setWantsKeyboardFocus (true);
    setTitle ("Library");
}

void LibraryScreen::setSelected (const juce::String& id)
{
    if (id == selected)
        return;
    selected = id;
    repaint();
    if (onSelectionChanged)
        onSelectionChanged();
}

juce::Rectangle<float> LibraryScreen::rowBounds (int index) const
{
    return { 1.0f, kListTop + (float) index * kRowH, (float) getWidth() - 2.0f, kRowH };
}

void LibraryScreen::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    paintScreen (g, r);
    drawScreenTitle (g, r, "LIBRARY");

    const auto& cat = engine.catalog();
    juce::String header;
    if (engine.isChecking())            header = "CHECKING...";
    else if (! engine.hasCatalog())     header = "OFFLINE";
    else
    {
        const int n = (int) cat.packages.size(), u = engine.updatable().size();
        const int installed = n - engine.notInstalled().size();
        header = juce::String (installed) + " OF " + juce::String (n) + " INSTALLED";
        if (u > 0)                  header << dot() << u << (u == 1 ? " UPDATE" : " UPDATES");
        else if (installed > 0)     header << dot() << "ALL CURRENT";
    }
    drawScreenHeader (g, r, header);

    if (! engine.hasCatalog())
    {
        g.setColour (colours::ghost);
        g.setFont (Fonts::get().regular (type::screenText));
        const auto msg = engine.isChecking() ? juce::String ("Fetching the catalog...")
                                             : engine.catalogError().isNotEmpty() ? engine.catalogError()
                                                                                  : juce::String ("No catalog yet.");
        g.drawFittedText (msg, r.reduced (40.0f, 60.0f).toNearestInt(), juce::Justification::centred, 4);
        return;
    }

    // Column header
    g.setColour (colours::ghost);
    g.setFont (Fonts::get().regular (type::screenText));
    const float hy = kListTop - 18.0f;
    for (auto [x, t] : { std::pair { cName, "PLUG-IN" }, { cType, "TYPE" }, { cInstalled, "INSTALLED" },
                         { cLatest, "LATEST" }, { cStatus, "STATUS" } })
        g.drawText (t, juce::Rectangle<float> (x, hy, 100.0f, 14.0f), juce::Justification::centredLeft, false);
    g.setColour (colours::rule);
    g.fillRect (juce::Rectangle<float> (12.0f, kListTop - 3.0f, r.getWidth() - 24.0f, 1.0f));

    for (int i = 0; i < (int) cat.packages.size(); ++i)
        paintRow (g, cat.packages[(size_t) i], rowBounds (i), cat.packages[(size_t) i].id == selected);

    drawLegend (g, r.getRight() - 12.0f, r.getBottom() - 14.0f,
                { { "UP TO DATE", colours::signal }, { "UPDATE", colours::amber }, { "WORKING", colours::blue } });

    if (hasKeyboardFocus (false))
        drawFocusRing (g, r.reduced (3.0f));
}

void LibraryScreen::paintRow (juce::Graphics& g, const Package& p, juce::Rectangle<float> row, bool isSelected)
{
    const auto status = engine.statusOf (p.id);
    const auto state = engine.stateOf (p.id);
    const auto lic = accounts.licenseFor (p.id);
    const auto& job = engine.job();
    const bool working = job.running && job.ids.contains (p.id);

    if (isSelected)
    {
        g.setColour (colours::cream);
        g.drawRect (row.reduced (6.0f, 2.0f), 1.0f);
        g.setColour (colours::red);
        g.fillRect (juce::Rectangle<float> (row.getX() + cMarker, row.getCentreY() - 3.0f, 6.0f, 6.0f));
    }

    auto cellAt = [&] (float x, float w) { return juce::Rectangle<float> (row.getX() + x, row.getY(), w, row.getHeight()); };

    g.setColour (colours::cream);
    g.setFont (Fonts::get().bold (12.0f));
    g.drawText (p.name, cellAt (cName, cType - cName - 6.0f), juce::Justification::centredLeft, true);

    g.setFont (Fonts::get().regular (type::screenText));
    g.setColour (colours::ghost);
    g.drawText (p.type.toUpperCase() + (p.hasApp() ? " + APP" : ""), cellAt (cType, cInstalled - cType - 6.0f),
                juce::Justification::centredLeft, true);

    g.setColour (state.installed() ? colours::signal : colours::dim);
    g.drawText (state.installed() ? state.version : juce::String ("-"), cellAt (cInstalled, 80.0f),
                juce::Justification::centredLeft, true);
    g.setColour (colours::signal);
    g.drawText (p.version, cellAt (cLatest, 80.0f), juce::Justification::centredLeft, true);

    const auto statusArea = cellAt (cStatus, row.getRight() - cStatus - 14.0f);
    if (working)
    {
        const bool downloading = job.currentId == p.id && job.progress >= 0.0;
        g.setColour (colours::blue);
        g.drawText (downloading ? "GET" : "WAIT", statusArea, juce::Justification::centredLeft, false);
        if (downloading)
        {
            // Ten hard cells, filled as the download lands.
            const int lit = juce::roundToInt (job.progress * 10.0);
            for (int i = 0; i < 10; ++i)
            {
                g.setColour (i < lit ? colours::blue : colours::dim);
                g.fillRect (juce::Rectangle<float> (statusArea.getX() + 34.0f + (float) i * 8.0f, row.getCentreY() - 3.0f, 6.0f, 6.0f));
            }
        }
        return;
    }

    const auto look = statusLook (status, lic);
    g.setColour (look.colour);
    g.drawText (look.text, statusArea, juce::Justification::centredLeft, false);
}

void LibraryScreen::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    const auto& cat = engine.catalog();
    for (int i = 0; i < (int) cat.packages.size(); ++i)
        if (rowBounds (i).contains (e.position))
            setSelected (cat.packages[(size_t) i].id);
}

bool LibraryScreen::keyPressed (const juce::KeyPress& k)
{
    const auto& cat = engine.catalog();
    if (cat.packages.empty())
        return false;

    int index = 0;
    for (int i = 0; i < (int) cat.packages.size(); ++i)
        if (cat.packages[(size_t) i].id == selected)
            index = i;

    if (k == juce::KeyPress::upKey)   index = juce::jmax (0, index - 1);
    else if (k == juce::KeyPress::downKey) index = juce::jmin ((int) cat.packages.size() - 1, index + 1);
    else return false;

    setSelected (cat.packages[(size_t) index].id);
    return true;
}
} // namespace onga::sync
