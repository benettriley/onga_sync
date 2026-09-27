#pragma once

#include "../core/Account.h"

#include <onga_ui/OngaUI.h>

namespace onga::sync
{
/** Sign in to an ONGA account: Google, or email and password. Both stay greyed out until
    the account service is live. Development builds add SKIP SIGN-IN. */
class SignInView final : public juce::Component
{
public:
    explicit SignInView (AccountManager&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void setBusy (bool, const juce::String& message = {});
    void submitPassword();
    void finished (juce::Result);

    AccountManager& accounts;
    juce::TextButton google { "CONTINUE WITH GOOGLE" }, signIn { "SIGN IN" }, skip { "SKIP SIGN-IN" };
    juce::TextEditor email, password;
    juce::String message;
    bool messageIsError = false;
};
} // namespace onga::sync
