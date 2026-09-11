#include "LicenseStore.h"

#if JUCE_WINDOWS
 #include <windows.h>
 #include <wincrypt.h>
 #pragma comment(lib, "crypt32.lib")
#elif JUCE_MAC
 #include <Security/Security.h>
#endif

namespace amanorsac::licensing
{
namespace
{
constexpr const char* deviceFile = "device.id";
constexpr const char* keyFile = "license-key.dat";
constexpr const char* proofFile = "license-proof.dat";
}

LicenseStore::LicenseStore(juce::String bundleName)
    : bundle(std::move(bundleName)), entropy(bundle + "/license/v1")
{
}

juce::File LicenseStore::directory() const
{
   #if JUCE_WINDOWS
    // LOCALAPPDATA, which is per user and never roams to another machine.
    auto root = juce::File::getSpecialLocation(juce::File::windowsLocalAppData);
   #else
    auto root = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
   #endif
    return root.getChildFile("Amanorsac Studio").getChildFile(bundle);
}

juce::String LicenseStore::deviceLabel()
{
    auto name = juce::SystemStats::getComputerName().trim();
    if (name.isEmpty()) name = "Unnamed computer";
    return name.substring(0, 120);
}

juce::String LicenseStore::deviceId()
{
    auto file = directory().getChildFile(deviceFile);
    auto existing = file.existsAsFile() ? file.loadFileAsString().trim() : juce::String();
    if (existing.length() >= 16 && existing.length() <= 128) return existing;

    // First run on this machine, or the file was lost. A fresh id takes a seat
    // again, which is the right outcome for a rebuilt computer.
    const auto created = juce::Uuid().toDashedString();
    directory().createDirectory();
    file.replaceWithText(created);
    return created;
}

// --------------------------------------------------------------- encryption

#if JUCE_WINDOWS
juce::String LicenseStore::read(const juce::File& file) const
{
    if (! file.existsAsFile()) return {};
    juce::MemoryBlock sealed;
    if (! file.loadFileAsData(sealed) || sealed.getSize() == 0) return {};

    DATA_BLOB in { static_cast<DWORD>(sealed.getSize()), static_cast<BYTE*>(sealed.getData()) };
    auto entropyBytes = entropy.toRawUTF8();
    DATA_BLOB salt { static_cast<DWORD>(strlen(entropyBytes)),
                     reinterpret_cast<BYTE*>(const_cast<char*>(entropyBytes)) };
    DATA_BLOB out {};

    if (! CryptUnprotectData(&in, nullptr, &salt, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
    {
        // Profile moved or password reset: the blob is useless now. Drop it and
        // let the activation screen come back, quietly.
        file.deleteFile();
        return {};
    }

    juce::String value(juce::CharPointer_UTF8(reinterpret_cast<const char*>(out.pbData)),
                       static_cast<size_t>(out.cbData));
    LocalFree(out.pbData);
    return value;
}

void LicenseStore::write(const juce::File& file, const juce::String& value) const
{
    directory().createDirectory();
    auto utf8 = value.toRawUTF8();
    DATA_BLOB in { static_cast<DWORD>(strlen(utf8)), reinterpret_cast<BYTE*>(const_cast<char*>(utf8)) };
    auto entropyBytes = entropy.toRawUTF8();
    DATA_BLOB salt { static_cast<DWORD>(strlen(entropyBytes)),
                     reinterpret_cast<BYTE*>(const_cast<char*>(entropyBytes)) };
    DATA_BLOB out {};

    auto description = juce::String(bundle).toWideCharPointer();
    if (CryptProtectData(&in, description, &salt, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
    {
        file.replaceWithData(out.pbData, out.cbData);
        LocalFree(out.pbData);
    }
}

#elif JUCE_MAC
juce::String LicenseStore::read(const juce::File& file) const
{
    // The Keychain holds the secret; the file marks that one was stored, so
    // the folder still describes the machine's state.
    if (! file.existsAsFile()) return {};

    auto service = juce::String("studio.amanorsac.") + bundle;
    auto account = file.getFileName();
    auto serviceUtf8 = service.toRawUTF8();
    auto accountUtf8 = account.toRawUTF8();

    void* data = nullptr;
    UInt32 length = 0;
    const auto status = SecKeychainFindGenericPassword(nullptr,
        static_cast<UInt32>(strlen(serviceUtf8)), serviceUtf8,
        static_cast<UInt32>(strlen(accountUtf8)), accountUtf8,
        &length, &data, nullptr);

    if (status != errSecSuccess || data == nullptr)
    {
        file.deleteFile();
        return {};
    }
    juce::String value(juce::CharPointer_UTF8(static_cast<const char*>(data)), static_cast<size_t>(length));
    SecKeychainItemFreeContent(nullptr, data);
    return value;
}

void LicenseStore::write(const juce::File& file, const juce::String& value) const
{
    directory().createDirectory();
    auto service = juce::String("studio.amanorsac.") + bundle;
    auto account = file.getFileName();
    auto serviceUtf8 = service.toRawUTF8();
    auto accountUtf8 = account.toRawUTF8();
    auto valueUtf8 = value.toRawUTF8();

    SecKeychainItemRef existing = nullptr;
    const auto found = SecKeychainFindGenericPassword(nullptr,
        static_cast<UInt32>(strlen(serviceUtf8)), serviceUtf8,
        static_cast<UInt32>(strlen(accountUtf8)), accountUtf8,
        nullptr, nullptr, &existing);

    if (found == errSecSuccess && existing != nullptr)
    {
        SecKeychainItemModifyAttributesAndData(existing, nullptr,
            static_cast<UInt32>(strlen(valueUtf8)), valueUtf8);
        CFRelease(existing);
    }
    else
    {
        SecKeychainAddGenericPassword(nullptr,
            static_cast<UInt32>(strlen(serviceUtf8)), serviceUtf8,
            static_cast<UInt32>(strlen(accountUtf8)), accountUtf8,
            static_cast<UInt32>(strlen(valueUtf8)), valueUtf8, nullptr);
    }
    file.replaceWithText("stored in keychain");
}

#else
juce::String LicenseStore::read(const juce::File& file) const
{
    return file.existsAsFile() ? file.loadFileAsString() : juce::String();
}

void LicenseStore::write(const juce::File& file, const juce::String& value) const
{
    directory().createDirectory();
    file.replaceWithText(value);
}
#endif

// ------------------------------------------------------------------ accessors

juce::String LicenseStore::loadKey() const { return read(directory().getChildFile(keyFile)); }
void LicenseStore::storeKey(const juce::String& key) const { write(directory().getChildFile(keyFile), key); }

juce::String LicenseStore::loadProof() const { return read(directory().getChildFile(proofFile)); }
void LicenseStore::storeProof(const juce::String& proof) const { write(directory().getChildFile(proofFile), proof); }

void LicenseStore::clear() const
{
    directory().getChildFile(keyFile).deleteFile();
    directory().getChildFile(proofFile).deleteFile();
}

juce::int64 LicenseStore::lastChanged() const
{
    const auto key = directory().getChildFile(keyFile).getLastModificationTime().toMilliseconds();
    const auto proof = directory().getChildFile(proofFile).getLastModificationTime().toMilliseconds();
    return juce::jmax(key, proof);
}
}
