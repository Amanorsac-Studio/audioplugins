#pragma once

#include <juce_cryptography/juce_cryptography.h>

namespace amanorsac::licensing
{
/** Offline, node-locked licence checking.

    A licence is a short text payload signed with the studio's private RSA key.
    Only the public key ships inside the product, so a licence cannot be forged
    without the private key: there is no key generator to write. The payload
    names the machine it belongs to, so a valid licence copied to a second
    computer stops verifying there.

    This raises the cost of casual sharing to "patch the binary", which is the
    honest ceiling for anything running on the customer's own CPU. The value in
    entitlementScale() exists so that patching is not a one byte edit: it feeds
    real audio coefficients rather than gating a boolean.
*/
class LicenseManager
{
public:
    enum class Status
    {
        unlicensed,      ///< no licence file present
        malformed,       ///< present but not parseable
        forged,          ///< signature does not verify against the public key
        wrongMachine,    ///< genuine licence issued to a different computer
        expired,         ///< time limited licence whose date has passed
        licensed
    };

    static LicenseManager& getInstance();

    /** Re-reads the licence from disk. Called at construction and after an
        activation attempt. */
    void refresh();

    [[nodiscard]] Status status() const noexcept { return currentStatus; }
    [[nodiscard]] bool isLicensed() const noexcept { return currentStatus == Status::licensed; }
    [[nodiscard]] juce::String licensedTo() const { return owner; }
    [[nodiscard]] juce::String statusMessage() const;

    /** Scales the processed signal. Exactly 1.0 when licensed. Unlicensed runs
        return a value that varies over time so the demo state is audible and
        cannot be replaced by a single constant. */
    [[nodiscard]] float entitlementScale(int samplesElapsed) const noexcept;

    /** Installs a licence supplied by the customer. Returns the resulting
        status so the UI can explain a rejection precisely. */
    Status activateFromText(const juce::String& licenceText);
    Status activateFromFile(const juce::File& file);

    /** Where an installed licence lives, shown in the activation window. */
    static juce::File licenceFile();

    /** Verifies a payload/signature pair. Exposed so the licence generator can
        prove a freshly issued licence before it is sent out. */
    static bool verifySignature(const juce::String& payload, const juce::String& signature);

private:
    LicenseManager();

    Status evaluate(const juce::String& licenceText, juce::String& ownerOut) const;

    Status currentStatus = Status::unlicensed;
    juce::String owner;
    juce::uint32 salt = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LicenseManager)
};
}
