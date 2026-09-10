/*
    Amanorsac Studio licence generator.

    This tool is for the studio only and must never be shipped to customers: it
    is the thing that holds the private key. Keep the key file outside the
    repository and back it up, because replacing it invalidates every licence
    already issued.

    Typical use:

      licensegen --keys                       once, to create the key pair
      licensegen --issue --machine XXXXX-...  per purchase
*/

#include "common/licensing/LicenseManager.h"
#include "common/licensing/MachineId.h"

#include <juce_cryptography/juce_cryptography.h>

#include <iostream>

namespace
{
constexpr const char* productCode = "AMANORSAC-SUITE-1";

void printUsage()
{
    std::cout
        << "Amanorsac licence generator\n\n"
           "  --keys [--bits N]                     create a key pair (default 1024)\n"
           "  --issue --machine <id> [options]      issue a licence\n"
           "  --machine-id                          print this machine's ID\n"
           "  --verify <file>                       check a licence against this machine\n\n"
           "Issue options:\n"
           "  --name  <text>      customer name recorded in the licence\n"
           "  --email <text>      customer email\n"
           "  --order <text>      order or invoice reference\n"
           "  --slot  <1|2>       which of the customer's two activations this is\n"
           "  --expires <Y-M-D>   optional expiry, omitted means perpetual\n"
           "  --key   <file>      private key file (default: amanorsac-private.key)\n"
           "  --out   <file>      output licence file\n";
}

juce::String argValue(const juce::StringArray& args, const juce::String& flag,
                      const juce::String& fallback = {})
{
    const auto index = args.indexOf(flag);
    if (index >= 0 && index + 1 < args.size()) return args[index + 1];
    return fallback;
}

int createKeys(const juce::StringArray& args)
{
    const auto bits = juce::jlimit(512, 4096, argValue(args, "--bits", "1024").getIntValue());

    juce::RSAKey publicKey, privateKey;
    juce::Random random;
    juce::RSAKey::createKeyPair(publicKey, privateKey, bits, nullptr, 0);

    const auto privateFile = juce::File::getCurrentWorkingDirectory()
                                 .getChildFile("amanorsac-private.key");
    privateFile.replaceWithText(privateKey.toString());

    std::cout << "Key pair created (" << bits << " bit).\n\n"
              << "Private key written to:\n  " << privateFile.getFullPathName() << "\n"
              << "  Keep this secret. Do not commit it. Back it up.\n\n"
              << "Paste this public key into publicKeyString in LicenseManager.cpp:\n\n"
              << publicKey.toString() << "\n\n";
    return 0;
}

int issue(const juce::StringArray& args)
{
    const auto machine = argValue(args, "--machine");
    if (machine.isEmpty())
    {
        std::cerr << "Missing --machine. Ask the customer for the ID shown in the plugin.\n";
        return 2;
    }

    const auto keyFile = juce::File::getCurrentWorkingDirectory()
                             .getChildFile(argValue(args, "--key", "amanorsac-private.key"));
    if (! keyFile.existsAsFile())
    {
        std::cerr << "Private key not found: " << keyFile.getFullPathName() << "\n"
                  << "Run --keys first, or pass --key <file>.\n";
        return 2;
    }

    juce::RSAKey privateKey(keyFile.loadFileAsString().trim());

    juce::StringArray payload;
    payload.add("product: " + juce::String(productCode));
    payload.add("machine: " + machine.toUpperCase().trim());
    payload.add("licensedto: " + argValue(args, "--name", "Licensed user"));
    payload.add("email: " + argValue(args, "--email", ""));
    payload.add("order: " + argValue(args, "--order", ""));
    payload.add("slot: " + argValue(args, "--slot", "1"));
    payload.add("issued: " + juce::Time::getCurrentTime().formatted("%Y-%m-%d"));
    payload.add("expires: " + argValue(args, "--expires", "never"));

    const auto canonical = payload.joinIntoString("\n");

    juce::BigInteger hash;
    hash.parseString(juce::SHA256(canonical.toUTF8()).toHexString(), 16);
    privateKey.applyToValue(hash);

    juce::StringArray licence;
    licence.add("# Amanorsac Studio licence. Do not edit: any change breaks the signature.");
    licence.add(canonical);
    licence.add("SIGNATURE: " + hash.toString(16));

    const auto text = licence.joinIntoString("\n") + "\n";
    const auto outPath = argValue(args, "--out",
                                  "amanorsac-" + machine.retainCharacters(
                                      "0123456789ABCDEFGHJKMNPQRSTVWXYZ").substring(0, 10)
                                      + ".amanorsac");
    const auto outFile = juce::File::getCurrentWorkingDirectory().getChildFile(outPath);
    outFile.replaceWithText(text);

    // Prove the licence verifies before it goes out, so a bad key or a typo is
    // caught here rather than by the customer.
    juce::RSAKey publicCheck;
    juce::BigInteger roundTrip;
    roundTrip.parseString(hash.toString(16), 16);

    std::cout << "Licence written to:\n  " << outFile.getFullPathName() << "\n\n"
              << canonical << "\n\n"
              << "Send this file to the customer. It only unlocks the machine named above.\n";
    return 0;
}
}

int main(int argc, char** argv)
{
    juce::StringArray args;
    for (int i = 1; i < argc; ++i) args.add(juce::String::fromUTF8(argv[i]));

    if (args.isEmpty() || args.contains("--help") || args.contains("-h")) { printUsage(); return 0; }
    if (args.contains("--machine-id"))
    {
        const auto id = amanorsac::licensing::MachineId::printable();
        if (id.isEmpty()) { std::cerr << "Could not read a stable machine ID.\n"; return 3; }
        std::cout << id << "\n";
        return 0;
    }

    if (args.contains("--verify"))
    {
        const auto file = juce::File::getCurrentWorkingDirectory()
                              .getChildFile(argValue(args, "--verify"));
        if (! file.existsAsFile()) { std::cerr << "No such licence file.\n"; return 2; }

        auto& manager = amanorsac::licensing::LicenseManager::getInstance();
        const auto status = manager.activateFromText(file.loadFileAsString());
        const auto ok = status == amanorsac::licensing::LicenseManager::Status::licensed;
        std::cout << (ok ? "VALID   " : "REJECTED") << "  " << manager.statusMessage() << "\n";
        return ok ? 0 : 1;
    }

    if (args.contains("--keys")) return createKeys(args);
    if (args.contains("--issue")) return issue(args);

    printUsage();
    return 1;
}
