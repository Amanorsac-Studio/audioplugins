#include "MachineId.h"

#include <juce_cryptography/juce_cryptography.h>

#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace amanorsac::licensing
{
namespace
{
/** Crockford-style alphabet: no I, L, O or U, so a customer reading the code
    off screen and typing it into an email cannot produce an ambiguous character. */
constexpr const char* alphabet = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

juce::String windowsMachineGuid()
{
   #if JUCE_WINDOWS
    // Written once by Windows at install time and stable for the life of that
    // installation, which is exactly the lifetime a licence should track.
    return juce::WindowsRegistry::getValue(
        "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Cryptography\\MachineGuid", {},
        juce::WindowsRegistry::WoW64_64bit);
   #else
    return {};
   #endif
}

juce::String systemVolumeSerial()
{
   #if JUCE_WINDOWS
    DWORD serial = 0;
    if (GetVolumeInformationW(L"C:\\", nullptr, 0, &serial, nullptr, nullptr, nullptr, 0))
        return juce::String::toHexString(static_cast<int>(serial));
    return {};
   #else
    return {};
   #endif
}

juce::String encodeGroups(const juce::String& hex, int groups, int groupLength)
{
    juce::String out;
    juce::BigInteger value;
    value.parseString(hex, 16);

    const auto alphabetSize = static_cast<int>(std::strlen(alphabet));
    for (int i = 0; i < groups * groupLength; ++i)
    {
        if (i > 0 && (i % groupLength) == 0) out << '-';
        // take five bits at a time from the digest
        auto slice = 0;
        for (int bit = 0; bit < 5; ++bit)
            if (value[i * 5 + bit]) slice |= (1 << bit);
        out << alphabet[slice % alphabetSize];
    }
    return out;
}
}

juce::String MachineId::digest()
{
    juce::StringArray parts;
    parts.add(windowsMachineGuid());
    parts.add(systemVolumeSerial());
    parts.add(juce::SystemStats::getUniqueDeviceID());
    parts.add(juce::SystemStats::getCpuVendor());
    parts.add(juce::String(juce::SystemStats::getNumCpus()));

    parts.removeEmptyStrings();
    // A machine that reports nothing usable would otherwise hash to a constant
    // shared by every such machine, so refuse rather than issue a global key.
    if (parts.isEmpty())
        return {};

    return juce::SHA256(parts.joinIntoString("|").toUTF8()).toHexString();
}

juce::String MachineId::printable()
{
    const auto hex = digest();
    if (hex.isEmpty()) return {};
    return encodeGroups(hex, 4, 5);
}

bool MachineId::matchesPrintable(const juce::String& candidate)
{
    const auto normalise = [](const juce::String& text)
    {
        return text.toUpperCase().retainCharacters(alphabet);
    };
    const auto mine = printable();
    return mine.isNotEmpty() && normalise(mine) == normalise(candidate);
}
}
