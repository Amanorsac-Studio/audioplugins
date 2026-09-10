#include "LicenseManager.h"
#include "MachineId.h"

#include <cmath>

namespace amanorsac::licensing
{
namespace
{
/** Studio public key. The matching private key never ships and must never be
    committed: it lives only on the signing machine. Replacing this constant
    invalidates every licence already issued. */
constexpr const char* publicKeyString =
    "5,6ddd313f10b2523a5207baae8d13101d9c4cf304a9674566cc81b5074d63bef854cb57c62e8d8ed17089df760809348013e68d98263c349135a70788e1dca400c5016fc6ba6e6e748bd4d2b380266a296ab478753c5fb606c04b896a5ff4fc14136c3bc60c7b1edaceb7d5d096f0c45341e00e664f28d88d70b70797aa9825e9";

constexpr const char* productCode = "AMANORSAC-SUITE-1";
constexpr const char* signatureTag = "SIGNATURE:";

juce::String canonicalPayload(const juce::StringArray& lines)
{
    juce::StringArray payload;
    for (const auto& line : lines)
    {
        const auto trimmed = line.trim();
        if (trimmed.isEmpty() || trimmed.startsWith("#")) continue;
        if (trimmed.startsWith(signatureTag)) continue;
        payload.add(trimmed);
    }
    return payload.joinIntoString("\n");
}

juce::String fieldValue(const juce::String& payload, const juce::String& key)
{
    for (const auto& line : juce::StringArray::fromLines(payload))
        if (line.startsWithIgnoreCase(key + ":"))
            return line.fromFirstOccurrenceOf(":", false, false).trim();
    return {};
}
}

LicenseManager& LicenseManager::getInstance()
{
    static LicenseManager instance;
    return instance;
}

LicenseManager::LicenseManager()
{
    salt = static_cast<juce::uint32>(juce::Time::getCurrentTime().toMilliseconds() & 0xffffffffu);
    refresh();
}

juce::File LicenseManager::licenceFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
               .getChildFile("Amanorsac Studio")
               .getChildFile("licence.amanorsac");
}

bool LicenseManager::verifySignature(const juce::String& payload, const juce::String& signature)
{
    const juce::String keyText(publicKeyString);
    // An unconfigured build must never behave as though every licence is valid.
    if (keyText.isEmpty() || keyText.startsWith("AMANORSAC_PUBLIC_KEY"))
        return false;
    if (payload.isEmpty() || signature.isEmpty())
        return false;

    juce::RSAKey publicKey(keyText);

    juce::BigInteger signed_;
    signed_.parseString(signature, 16);
    if (signed_.isZero()) return false;

    publicKey.applyToValue(signed_);

    juce::BigInteger expected;
    expected.parseString(juce::SHA256(payload.toUTF8()).toHexString(), 16);

    return ! expected.isZero() && signed_ == expected;
}

LicenseManager::Status LicenseManager::evaluate(const juce::String& licenceText,
                                                juce::String& ownerOut) const
{
    if (licenceText.trim().isEmpty()) return Status::unlicensed;

    const auto lines = juce::StringArray::fromLines(licenceText);
    const auto payload = canonicalPayload(lines);

    juce::String signature;
    for (const auto& line : lines)
        if (line.trim().startsWith(signatureTag))
            signature = line.trim().fromFirstOccurrenceOf(signatureTag, false, false).trim();

    if (payload.isEmpty() || signature.isEmpty()) return Status::malformed;
    if (fieldValue(payload, "product") != juce::String(productCode)) return Status::malformed;

    const auto machine = fieldValue(payload, "machine");
    if (machine.isEmpty()) return Status::malformed;

    // Signature first: a licence that fails here tells us nothing else is worth
    // trusting, including the machine it claims to be for.
    if (! verifySignature(payload, signature)) return Status::forged;

    if (! MachineId::matchesPrintable(machine)) return Status::wrongMachine;

    const auto expiry = fieldValue(payload, "expires");
    if (expiry.isNotEmpty() && ! expiry.equalsIgnoreCase("never"))
    {
        const auto parts = juce::StringArray::fromTokens(expiry, "-", {});
        if (parts.size() == 3)
        {
            const juce::Time deadline(parts[0].getIntValue(), parts[1].getIntValue() - 1,
                                      parts[2].getIntValue(), 23, 59);
            if (juce::Time::getCurrentTime() > deadline) return Status::expired;
        }
    }

    ownerOut = fieldValue(payload, "licensedto");
    return Status::licensed;
}

void LicenseManager::refresh()
{
    owner.clear();
    const auto file = licenceFile();
    currentStatus = file.existsAsFile() ? evaluate(file.loadFileAsString(), owner)
                                        : Status::unlicensed;
}

LicenseManager::Status LicenseManager::activateFromText(const juce::String& licenceText)
{
    juce::String candidateOwner;
    const auto result = evaluate(licenceText, candidateOwner);
    if (result != Status::licensed) return result;

    const auto file = licenceFile();
    file.getParentDirectory().createDirectory();
    file.replaceWithText(licenceText);

    refresh();
    return currentStatus;
}

LicenseManager::Status LicenseManager::activateFromFile(const juce::File& file)
{
    if (! file.existsAsFile()) return Status::unlicensed;
    return activateFromText(file.loadFileAsString());
}

juce::String LicenseManager::statusMessage() const
{
    switch (currentStatus)
    {
        case Status::licensed:     return "Licensed to " + (owner.isNotEmpty() ? owner : juce::String("this machine"));
        case Status::unlicensed:   return "Demo mode. No licence installed.";
        case Status::malformed:    return "That licence file could not be read.";
        case Status::forged:       return "That licence failed verification.";
        case Status::wrongMachine: return "That licence was issued to a different computer.";
        case Status::expired:      return "That licence has expired.";
    }
    return {};
}

float LicenseManager::entitlementScale(int samplesElapsed) const noexcept
{
    if (currentStatus == Status::licensed)
        return 1.0f;

    // Demo behaviour: mostly audible, with a short dip roughly every twenty
    // seconds. The period is salted per run so the pattern cannot be learned and
    // compiled out, and the value is a smooth curve rather than a flag, so a
    // patched constant leaves the product audibly wrong instead of unlocked.
    constexpr auto period = 20 * 48000;
    const auto phase = static_cast<float>((static_cast<juce::uint32>(samplesElapsed) + salt) % period)
                     / static_cast<float>(period);
    if (phase > 0.94f)
        return 0.12f + 0.88f * (1.0f - std::sin((phase - 0.94f) / 0.06f * juce::MathConstants<float>::pi));

    return 0.86f;
}
}
