#include "Download.h"

#include <juce_cryptography/juce_cryptography.h>

namespace onga::sync
{
static std::unique_ptr<juce::InputStream> open (const juce::URL& url, int& status, juce::int64& total)
{
    if (url.isLocalFile())
    {
        auto f = url.getLocalFile();
        status = f.existsAsFile() ? 200 : 404;
        total = f.getSize();
        return f.existsAsFile() ? f.createInputStream() : nullptr;
    }

    auto stream = url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                             .withConnectionTimeoutMs (20000)
                                             .withNumRedirectsToFollow (10)
                                             .withStatusCode (&status));
    total = stream != nullptr ? stream->getTotalLength() : 0;
    return stream;
}

static juce::String httpError (const juce::URL& url, int status)
{
    if (status == 404)
        return "Not found: " + url.toString (false);
    if (status == 0)
        return "Couldn't reach " + url.getDomain() + ". Check your internet connection.";
    return "The server answered " + juce::String (status) + " for " + url.toString (false);
}

juce::Result fetchText (const juce::URL& url, juce::String& out)
{
    int status = 0;
    juce::int64 total = 0;
    auto in = open (url, status, total);
    if (in == nullptr || status >= 400)
        return juce::Result::fail (httpError (url, status));
    out = in->readEntireStreamAsString();
    return juce::Result::ok();
}

juce::String sha256Of (const juce::File& f)
{
    return juce::SHA256 (f).toHexString().toLowerCase();
}

juce::Result download (const Download& d, const juce::File& dest, const Progress& progress)
{
    int status = 0;
    juce::int64 total = 0;
    auto in = open (d.url, status, total);
    if (in == nullptr || status >= 400)
        return juce::Result::fail (httpError (d.url, status));
    if (total <= 0)
        total = d.size;

    dest.getParentDirectory().createDirectory();
    dest.deleteFile();
    {
        juce::FileOutputStream out (dest);
        if (! out.openedOk())
            return juce::Result::fail ("Couldn't write " + dest.getFullPathName());

        juce::HeapBlock<char> buf (1 << 16);
        juce::int64 done = 0;
        for (;;)
        {
            const int n = in->read (buf, 1 << 16);
            if (n < 0)
                return juce::Result::fail ("The download was interrupted.");
            if (n == 0)
                break;
            if (! out.write (buf, (size_t) n))
                return juce::Result::fail ("The disk is full.");
            done += n;
            if (progress && ! progress (done, total))
            {
                out.flush();
                dest.deleteFile();
                return juce::Result::fail ("cancelled");
            }
        }
        out.flush();
    }

    if (d.size > 0 && dest.getSize() != d.size)
    {
        dest.deleteFile();
        return juce::Result::fail ("The download was incomplete. Try again.");
    }
    if (d.sha256.isNotEmpty() && sha256Of (dest) != d.sha256)
    {
        dest.deleteFile();
        return juce::Result::fail ("The download didn't match its checksum, so it wasn't installed. Try again.");
    }
    return juce::Result::ok();
}
} // namespace onga::sync
