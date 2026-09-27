#pragma once

#include "../app/Engine.h"
#include "../core/Account.h"

#include <onga_ui/OngaUI.h>

namespace onga::sync
{
/** The dark screen: every ONGA plug-in, what's installed, what's current. Click or use
    the arrow keys to pick one; the plate below acts on the pick. */
class LibraryScreen final : public juce::Component
{
public:
    LibraryScreen (Engine&, AccountManager&);

    juce::String getSelected() const { return selected; }
    void setSelected (const juce::String& id);
    std::function<void()> onSelectionChanged;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

    static constexpr float kRowH = 26.0f, kListTop = 58.0f;

private:
    juce::Rectangle<float> rowBounds (int index) const;
    void paintRow (juce::Graphics&, const Package&, juce::Rectangle<float>, bool isSelected);

    Engine& engine;
    AccountManager& accounts;
    juce::String selected;
};
} // namespace onga::sync
