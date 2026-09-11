#pragma once

#include <juce_core/juce_core.h>

#include <cstdint>

namespace amanorsac::licensing
{
/** The studio signing key, the same for every Amanorsac product.

    Raw SEC1 uncompressed point: 0x04 followed by X and Y, 32 bytes each.
    This is the whole security model. The private half exists only on the
    licence server, so nothing a customer or a cracker can do on their own
    machine produces a proof that verifies against this.

    There is no development key and no second key anywhere in the build. A
    previous product shipped a development key, reported "Activated" from the
    server's HTTP 200 rather than a verified signature, and locked every buyer
    out. That is why verify() is the only path to a licensed state.
*/
inline constexpr std::uint8_t kLicenseSigningKey[65] = {
    0x04, 0xcd, 0xa5, 0x7d, 0x1c, 0xc8, 0xa6, 0xe2, 0x71, 0xd5, 0x48, 0x49,
    0xce, 0x55, 0xd5, 0x03, 0x77, 0x56, 0x66, 0x90, 0xfd, 0xb6, 0x95, 0x45,
    0xa4, 0x1a, 0x92, 0xc4, 0x77, 0xda, 0xcb, 0x00, 0x0d, 0x2c, 0x06, 0x0b,
    0xa8, 0x3f, 0xbd, 0x9b, 0x70, 0x85, 0xaf, 0xff, 0xc0, 0x42, 0xd4, 0x00,
    0x7e, 0x5b, 0x96, 0xfe, 0x68, 0xff, 0xec, 0x91, 0x11, 0xf6, 0x21, 0x00,
    0x79, 0xfc, 0x43, 0x59, 0x52
};
inline constexpr bool kLicenseSigningKeyConfigured = true;

/** What a verified proof said. Only ever produced by verifyProof(). */
struct Proof
{
    juce::String deviceKey;
    juce::String licenseKey;
    juce::int64 issuedAt = 0;     // milliseconds since the epoch
    juce::int64 expiresAt = 0;    // issuedAt + 48 hours
    juce::int64 graceUntil = 0;   // issuedAt + 30 days
};

/** Decodes base64url: '-' for '+', '_' for '/', padding optional. */
juce::MemoryBlock decodeBase64Url(const juce::String& text);

/** ECDSA P-256 / SHA-256 over exactly these bytes, with a raw 64 byte r||s
    signature. Returns false for any other signature length. */
bool verifySignature(const void* data, size_t size, const juce::MemoryBlock& signature);

/** Verifies "<base64url body>.<base64url signature>" and, only if the
    signature is good, parses the body.

    `error` receives a short reason when this returns false. The proof is
    still not a licence on its own: the caller must check that deviceKey and
    licenseKey are this machine's and this key, and that the clock is sane.
*/
bool verifyProof(const juce::String& proof, Proof& result, juce::String* error = nullptr);
}
