#pragma once

#include <juce_events/juce_events.h>

#include <functional>
#include <memory>
#include <vector>

/*
    ONGA accounts. One sign-in per Mac unlocks the library; later, licences live in the
    user's account and come down with it.

    The account service doesn't exist yet. IdentityService is the seam it plugs into:
    Google sign-in (browser + loopback redirect) and email/password both end in an Account
    with tokens and licences. Until then NotLiveYet answers every call, and development
    builds (ONGA_SYNC_DEV_BYPASS) get a "developer" account that skips sign-in and is
    licensed for everything.
*/
namespace onga::sync
{
enum class Provider { none, developer, google, password };

struct License
{
    juce::String product;    // catalog id, or "*" for everything
    juce::String kind;       // "perpetual" | "subscription" | "trial" | "developer"
    juce::Time expires;      // 0 = never
};

enum class LicenseState { none, licensed, trial, developer };

struct Account
{
    juce::String userId, email, displayName;
    Provider provider = Provider::none;
    juce::String accessToken, refreshToken;   // from the service; empty for the developer account
    juce::Time expiresAt;
    std::vector<License> licenses;

    bool signedIn() const { return provider != Provider::none && userId.isNotEmpty(); }

    LicenseState licenseFor (const juce::String& product, juce::Time now = juce::Time::getCurrentTime()) const;

    juce::var toVar() const;
    static Account fromVar (const juce::var&);

    static Account developer();
};

juce::String providerName (Provider);

//==============================================================================
/** The ONGA account service. Implementations call `done` on the message thread. */
class IdentityService
{
public:
    using Done = std::function<void (juce::Result, Account)>;

    virtual ~IdentityService() = default;

    /** False until the service is running; the sign-in form greys out. */
    virtual bool isLive() const = 0;

    virtual void signInWithPassword (const juce::String& email, const juce::String& password, Done) = 0;
    virtual void signInWithGoogle (Done) = 0;
    /** New tokens and the latest licences for a signed-in account. */
    virtual void refresh (const Account&, Done) = 0;
    virtual void signOut (const Account&) {}
};

/** Stand-in until the service exists. */
class NotLiveYet final : public IdentityService
{
public:
    bool isLive() const override { return false; }
    void signInWithPassword (const juce::String&, const juce::String&, Done d) override { later (std::move (d)); }
    void signInWithGoogle (Done d) override { later (std::move (d)); }
    void refresh (const Account&, Done d) override { later (std::move (d)); }

private:
    static void later (Done d)
    {
        juce::MessageManager::callAsync ([d = std::move (d)] { d (juce::Result::fail ("ONGA accounts aren't live yet."), {}); });
    }
};

//==============================================================================
/** The signed-in account, kept on disk between launches. Message thread only. */
class AccountManager final : public juce::ChangeBroadcaster
{
public:
    AccountManager (juce::File storeFile, std::unique_ptr<IdentityService> service);

    const Account& current() const noexcept { return account; }
    bool signedIn() const noexcept { return account.signedIn(); }
    IdentityService& getService() noexcept { return *service; }

    /** True in development builds: skip sign-in with a developer account. */
    static bool devBypassAvailable() noexcept;

    void signInDeveloper();
    void signInWithPassword (const juce::String& email, const juce::String& password, std::function<void (juce::Result)>);
    void signInWithGoogle (std::function<void (juce::Result)>);
    void signOut();

    LicenseState licenseFor (const juce::String& product) const { return account.licenseFor (product); }

private:
    void adopt (juce::Result, Account, const std::function<void (juce::Result)>&);
    void save();

    juce::File store;
    std::unique_ptr<IdentityService> service;
    Account account;
};
} // namespace onga::sync
