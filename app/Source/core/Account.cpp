#include "Account.h"

#ifndef ONGA_SYNC_DEV_BYPASS
 #define ONGA_SYNC_DEV_BYPASS 0
#endif

namespace onga::sync
{
juce::String providerName (Provider p)
{
    switch (p)
    {
        case Provider::developer: return "developer";
        case Provider::google:    return "google";
        case Provider::password:  return "password";
        case Provider::none:      break;
    }
    return "none";
}

static Provider providerFrom (const juce::String& s)
{
    for (auto p : { Provider::developer, Provider::google, Provider::password })
        if (providerName (p) == s)
            return p;
    return Provider::none;
}

LicenseState Account::licenseFor (const juce::String& product, juce::Time now) const
{
    auto best = LicenseState::none;
    for (auto& l : licenses)
    {
        if (l.product != "*" && l.product != product)
            continue;
        if (l.expires.toMilliseconds() != 0 && l.expires < now)
            continue;
        if (l.kind == "developer") return LicenseState::developer;
        if (l.kind == "trial") best = best == LicenseState::none ? LicenseState::trial : best;
        else best = LicenseState::licensed;
    }
    return best;
}

juce::var Account::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("userId", userId);
    o->setProperty ("email", email);
    o->setProperty ("displayName", displayName);
    o->setProperty ("provider", providerName (provider));
    o->setProperty ("accessToken", accessToken);
    o->setProperty ("refreshToken", refreshToken);
    o->setProperty ("expiresAt", expiresAt.toMilliseconds());

    juce::Array<juce::var> list;
    for (auto& l : licenses)
    {
        auto* lo = new juce::DynamicObject();
        lo->setProperty ("product", l.product);
        lo->setProperty ("kind", l.kind);
        lo->setProperty ("expires", l.expires.toMilliseconds());
        list.add (juce::var (lo));
    }
    o->setProperty ("licenses", list);
    return juce::var (o);
}

Account Account::fromVar (const juce::var& v)
{
    Account a;
    a.userId = v["userId"].toString();
    a.email = v["email"].toString();
    a.displayName = v["displayName"].toString();
    a.provider = providerFrom (v["provider"].toString());
    a.accessToken = v["accessToken"].toString();
    a.refreshToken = v["refreshToken"].toString();
    a.expiresAt = juce::Time ((juce::int64) v["expiresAt"]);
    if (auto* list = v["licenses"].getArray())
        for (auto& l : *list)
            a.licenses.push_back ({ l["product"].toString(), l["kind"].toString(), juce::Time ((juce::int64) l["expires"]) });
    return a;
}

Account Account::developer()
{
    Account a;
    a.userId = "developer";
    a.email = "dev@ongatools.local";
    a.displayName = "Developer";
    a.provider = Provider::developer;
    a.licenses.push_back ({ "*", "developer", {} });
    return a;
}

//==============================================================================
AccountManager::AccountManager (juce::File storeFile, std::unique_ptr<IdentityService> s)
    : store (std::move (storeFile)), service (std::move (s))
{
    if (store.existsAsFile())
        account = Account::fromVar (juce::JSON::parse (store));

    // A developer session from a dev build means nothing to a release build.
    if (account.provider == Provider::developer && ! devBypassAvailable())
        account = {};
}

bool AccountManager::devBypassAvailable() noexcept { return ONGA_SYNC_DEV_BYPASS != 0; }

void AccountManager::signInDeveloper()
{
    if (! devBypassAvailable())
        return;
    account = Account::developer();
    save();
    sendChangeMessage();
}

void AccountManager::signInWithPassword (const juce::String& email, const juce::String& password,
                                         std::function<void (juce::Result)> done)
{
    service->signInWithPassword (email, password,
                                 [this, done] (juce::Result r, Account a) { adopt (r, std::move (a), done); });
}

void AccountManager::signInWithGoogle (std::function<void (juce::Result)> done)
{
    service->signInWithGoogle ([this, done] (juce::Result r, Account a) { adopt (r, std::move (a), done); });
}

void AccountManager::adopt (juce::Result r, Account a, const std::function<void (juce::Result)>& done)
{
    if (r.wasOk() && a.signedIn())
    {
        account = std::move (a);
        save();
        sendChangeMessage();
    }
    if (done)
        done (r);
}

void AccountManager::signOut()
{
    if (account.provider != Provider::developer)
        service->signOut (account);
    account = {};
    store.deleteFile();
    sendChangeMessage();
}

void AccountManager::save()
{
    // Tokens belong in the Keychain once the service issues real ones.
    store.getParentDirectory().createDirectory();
    store.replaceWithText (juce::JSON::toString (account.toVar()));
}
} // namespace onga::sync
