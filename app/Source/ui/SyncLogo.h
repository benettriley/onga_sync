#pragma once

#include <onga_ui/OngaUI.h>

namespace onga::sync
{
/** SYNC in the 5 x 7 pixel font. One red column sweeps across the word, leaving a short
    dithered wake: the library being checked, left to right. */
inline ui::PixelLogo::Generator syncLogo()
{
    return [] (float phase)
    {
        ui::LogoFrame f;
        const juce::String word ("SYNC");
        const float cell = 7.0f;
        const int cols = word.length() * 6 - 1;
        const float x0 = std::round ((ui::PixelLogo::kArtW - (float) cols * cell) * 0.5f);
        const float y0 = std::round ((ui::PixelLogo::kArtH - 7.0f * cell) * 0.5f);
        const int scan = (int) std::floor (phase * (float) (cols + 12)) - 6;

        ui::forEachGlyphCell (word, x0, y0, cell, [&] (float x, float y, int col)
        {
            ui::addCell (f.outline, x - 2.0f, y - 2.0f, cell + 4.0f);
            if (col == scan)                          ui::addCell (f.accent, x, y, cell);
            else if (col == scan - 1 || col == scan - 2) ui::addCell (f.dither, x, y, cell);
            else                                      ui::addCell (f.cream, x, y, cell);
        });

        if (scan >= 0 && scan < cols)
        {
            juce::Path ticks;
            const float x = x0 + (float) scan * cell;
            ticks.addRectangle (x, y0 - 10.0f, cell, 4.0f);
            ticks.addRectangle (x, y0 + 7.0f * cell + 6.0f, cell, 4.0f);
            f.over.push_back ({ ticks, ui::PixelLogo::useAccent() });
        }
        return f;
    };
}
} // namespace onga::sync
