#pragma once

#include "Catalog.h"

#include <functional>

namespace onga::sync
{
/** Called with bytes so far and the total (0 if unknown). Return false to cancel. */
using Progress = std::function<bool (juce::int64 done, juce::int64 total)>;

/** Fetches a small text file (the catalog). http(s) or file://. */
juce::Result fetchText (const juce::URL& url, juce::String& out);

/** Downloads to `dest` (replacing it), then checks the size and SHA-256 when the catalog
    gives them. A file that fails the check is deleted. */
juce::Result download (const Download& d, const juce::File& dest, const Progress& progress);

/** Lowercase hex SHA-256 of a file. */
juce::String sha256Of (const juce::File& f);
} // namespace onga::sync
