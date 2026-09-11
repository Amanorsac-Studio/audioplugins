#include "LicenseCrypto.h"

#if JUCE_WINDOWS
 #include <windows.h>
 #include <bcrypt.h>
 #pragma comment(lib, "bcrypt.lib")
#elif JUCE_MAC
 #include <Security/Security.h>
 #include <CommonCrypto/CommonDigest.h>
#endif

namespace amanorsac::licensing
{
namespace
{
constexpr size_t signatureLength = 64;   // raw r||s, never DER
}

juce::MemoryBlock decodeBase64Url(const juce::String& text)
{
    auto standard = text.replaceCharacter('-', '+').replaceCharacter('_', '/');
    while (standard.length() % 4 != 0) standard << "=";

    juce::MemoryOutputStream stream;
    if (! juce::Base64::convertFromBase64(stream, standard)) return {};
    return { stream.getData(), stream.getDataSize() };
}

#if JUCE_WINDOWS
bool verifySignature(const void* data, size_t size, const juce::MemoryBlock& signature)
{
    if (signature.getSize() != signatureLength) return false;

    // BCRYPT_ECCPUBLIC_BLOB: magic, key length, then X and Y with the leading
    // 0x04 of the SEC1 point dropped.
    std::vector<std::uint8_t> blob(sizeof(BCRYPT_ECCKEY_BLOB) + 64);
    auto* header = reinterpret_cast<BCRYPT_ECCKEY_BLOB*>(blob.data());
    header->dwMagic = BCRYPT_ECDSA_PUBLIC_P256_MAGIC;
    header->cbKey = 32;
    std::memcpy(blob.data() + sizeof(BCRYPT_ECCKEY_BLOB), kLicenseSigningKey + 1, 64);

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (! BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0)))
        return false;

    BCRYPT_KEY_HANDLE key = nullptr;
    auto imported = BCRYPT_SUCCESS(BCryptImportKeyPair(algorithm, nullptr, BCRYPT_ECCPUBLIC_BLOB,
                                                       &key, blob.data(),
                                                       static_cast<ULONG>(blob.size()), 0));
    if (! imported)
    {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return false;
    }

    std::uint8_t digest[32] {};
    {
        BCRYPT_ALG_HANDLE hashAlgorithm = nullptr;
        if (BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&hashAlgorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0)))
        {
            BCRYPT_HASH_HANDLE hash = nullptr;
            if (BCRYPT_SUCCESS(BCryptCreateHash(hashAlgorithm, &hash, nullptr, 0, nullptr, 0, 0)))
            {
                BCryptHashData(hash, static_cast<PUCHAR>(const_cast<void*>(data)),
                               static_cast<ULONG>(size), 0);
                BCryptFinishHash(hash, digest, sizeof(digest), 0);
                BCryptDestroyHash(hash);
            }
            BCryptCloseAlgorithmProvider(hashAlgorithm, 0);
        }
    }

    const auto status = BCryptVerifySignature(key, nullptr, digest, sizeof(digest),
                                              static_cast<PUCHAR>(const_cast<void*>(signature.getData())),
                                              static_cast<ULONG>(signature.getSize()), 0);
    BCryptDestroyKey(key);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return BCRYPT_SUCCESS(status);
}

#elif JUCE_MAC
namespace
{
/** Security.framework wants DER, the server sends raw r||s. */
void appendDerInteger(std::vector<std::uint8_t>& out, const std::uint8_t* value, size_t length)
{
    size_t start = 0;
    while (start + 1 < length && value[start] == 0) ++start;          // strip leading zeros
    const auto needsPad = (value[start] & 0x80) != 0;                  // keep it positive
    out.push_back(0x02);
    out.push_back(static_cast<std::uint8_t>(length - start + (needsPad ? 1 : 0)));
    if (needsPad) out.push_back(0x00);
    out.insert(out.end(), value + start, value + length);
}
}

bool verifySignature(const void* data, size_t size, const juce::MemoryBlock& signature)
{
    if (signature.getSize() != signatureLength) return false;
    const auto* raw = static_cast<const std::uint8_t*>(signature.getData());

    std::vector<std::uint8_t> body;
    appendDerInteger(body, raw, 32);
    appendDerInteger(body, raw + 32, 32);

    std::vector<std::uint8_t> der;
    der.push_back(0x30);
    der.push_back(static_cast<std::uint8_t>(body.size()));
    der.insert(der.end(), body.begin(), body.end());

    std::uint8_t digest[CC_SHA256_DIGEST_LENGTH] {};
    CC_SHA256(data, static_cast<CC_LONG>(size), digest);

    auto keyData = CFDataCreate(nullptr, kLicenseSigningKey, sizeof(kLicenseSigningKey));
    const void* keys[] = { kSecAttrKeyType, kSecAttrKeyClass };
    const void* values[] = { kSecAttrKeyTypeECSECPrimeRandom, kSecAttrKeyClassPublic };
    auto attributes = CFDictionaryCreate(nullptr, keys, values, 2,
                                         &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);

    CFErrorRef error = nullptr;
    auto key = SecKeyCreateWithData(keyData, attributes, &error);
    auto verified = false;

    if (key != nullptr)
    {
        auto digestData = CFDataCreate(nullptr, digest, sizeof(digest));
        auto signatureData = CFDataCreate(nullptr, der.data(), static_cast<CFIndex>(der.size()));
        verified = SecKeyVerifySignature(key, kSecKeyAlgorithmECDSASignatureDigestX962SHA256,
                                         digestData, signatureData, &error);
        CFRelease(digestData);
        CFRelease(signatureData);
        CFRelease(key);
    }
    if (error != nullptr) CFRelease(error);
    CFRelease(attributes);
    CFRelease(keyData);
    return verified;
}

#else
bool verifySignature(const void*, size_t, const juce::MemoryBlock&) { return false; }
#endif

bool verifyProof(const juce::String& proof, Proof& result, juce::String* error)
{
    auto fail = [error](const char* reason)
    {
        if (error != nullptr) *error = reason;
        return false;
    };

    const auto dot = proof.indexOfChar('.');
    if (dot <= 0) return fail("malformed proof");

    const auto body = decodeBase64Url(proof.substring(0, dot));
    const auto signature = decodeBase64Url(proof.substring(dot + 1));
    if (body.getSize() == 0) return fail("malformed proof body");
    if (signature.getSize() != signatureLength) return fail("unexpected signature length");

    // The signature covers the decoded bytes exactly as they arrived. Parsing
    // and re-serialising the JSON first would change them and never verify.
    if (! verifySignature(body.getData(), body.getSize(), signature))
        return fail("signature did not verify");

    const auto parsed = juce::JSON::parse(body.toString());
    auto* object = parsed.getDynamicObject();
    if (object == nullptr) return fail("proof body is not an object");

    result.deviceKey = object->getProperty("deviceKey").toString();
    result.licenseKey = object->getProperty("licenseKey").toString();
    result.issuedAt = static_cast<juce::int64>(object->getProperty("issuedAt"));
    result.expiresAt = static_cast<juce::int64>(object->getProperty("expiresAt"));
    result.graceUntil = static_cast<juce::int64>(object->getProperty("graceUntil"));

    if (result.deviceKey.isEmpty() || result.licenseKey.isEmpty() || result.graceUntil <= 0)
        return fail("proof is missing fields");
    return true;
}
}
