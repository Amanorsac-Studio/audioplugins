#pragma once

#include <juce_core/juce_core.h>

namespace amanorsac::licensing
{
/** Where the bundle's licence lives on disk.

    Every plug-in in the bundle reads and writes the same folder, so one
    activation covers all ten and one machine uses one seat. That is the whole
    reason this is a bundle-level store and not a per-plug-in one.

        Windows   %LOCALAPPDATA%\Amanorsac Studio\<bundle>\
        macOS     ~/Library/Application Support/Amanorsac Studio/<bundle>/

    device.id is plain text. The key and the proof are encrypted with the OS
    key store, so copying the folder to another machine yields nothing.
*/
class LicenseStore
{
public:
    /** The bundle name is the folder name, shared by every plug-in in it. */
    explicit LicenseStore(juce::String bundleName);

    [[nodiscard]] juce::File directory() const;

    /** A random id made once and kept forever. Never a hardware serial, a MAC
        address or anything else personal: it only has to be stable and
        unique, and a wiped machine taking a fresh seat is correct. */
    [[nodiscard]] juce::String deviceId();

    /** A name the customer will recognise in My Apps. */
    [[nodiscard]] static juce::String deviceLabel();

    [[nodiscard]] juce::String loadKey() const;
    void storeKey(const juce::String&) const;

    /** Only ever called after a proof verified: the file's presence is the
        evidence that activation truly succeeded. */
    [[nodiscard]] juce::String loadProof() const;
    void storeProof(const juce::String&) const;

    void clear() const;

    /** Newest write time across the key and proof files, so an already open
        plug-in can notice that another one activated the bundle. */
    [[nodiscard]] juce::int64 lastChanged() const;

private:
    [[nodiscard]] juce::String read(const juce::File&) const;
    void write(const juce::File&, const juce::String&) const;

    juce::String bundle;
    juce::String entropy;   // ties an encrypted blob to this product
};
}
