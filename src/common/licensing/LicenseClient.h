#pragma once

#include "LicenseCrypto.h"
#include "LicenseStore.h"

#include <juce_events/juce_events.h>

#include <atomic>

namespace amanorsac::licensing
{
/** The licence for the whole bundle, shared by every plug-in in the process.

    One instance serves every plug-in window that is open, and the store on
    disk is shared with every other process, so activating in any one plug-in
    activates all ten on that machine and uses a single seat.

    Network work never touches the audio thread: `isLicensed()` is an atomic
    flag, and everything else happens on a background thread or the message
    thread.
*/
class LicenseClient final : public juce::ChangeBroadcaster,
                            private juce::Timer
{
public:
    /** The one instance for this process. */
    static LicenseClient& getInstance();

    enum class State { unlicensed, licensed, grace };

    /** Read by the audio thread. Nothing else in this class is. */
    [[nodiscard]] bool isLicensed() const noexcept { return licensed.load(std::memory_order_relaxed); }
    [[nodiscard]] State state() const noexcept { return currentState.load(std::memory_order_relaxed); }

    [[nodiscard]] juce::String licenseKey() const;
    [[nodiscard]] juce::String deviceLabel() const { return LicenseStore::deviceLabel(); }
    /** Days left before the stored proof stops working offline. */
    [[nodiscard]] int daysOfGraceLeft() const;

    struct Result
    {
        bool ok = false;
        juce::String message;                // safe to show a customer
        juce::StringArray devicesInUse;      // set when the seat limit is reached
    };

    /** Sends the key and this device's id, verifies the signed proof that
        comes back, and only then reports success. The callback arrives on the
        message thread. */
    void activate(const juce::String& key, std::function<void(Result)> callback);

    /** Frees the seat on the server and forgets the licence on this machine. */
    void deactivate(std::function<void(Result)> callback);

    /** Called once when a plug-in opens: adopts a stored proof and, if there
        is a key but no usable proof, activates in the background. */
    void start();

private:
    LicenseClient();
    ~LicenseClient() override;

    void timerCallback() override;
    void refreshFromStore();
    void applyProof(const juce::String& proof, const juce::String& key);
    void setUnlicensed();
    Result post(const juce::String& endpoint, const juce::var& body, juce::String& responseBody) const;

    [[nodiscard]] static juce::String baseUrl();

    LicenseStore store;
    std::atomic<bool> licensed { false };
    std::atomic<State> currentState { State::unlicensed };
    std::atomic<juce::int64> graceUntil { 0 };
    std::atomic<juce::int64> lastHeartbeat { 0 };
    juce::int64 seenStoreChange = 0;
    bool started = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LicenseClient)
};
}
