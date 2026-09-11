#include "LicenseClient.h"

namespace amanorsac::licensing
{
namespace
{
// The bundle folder every plug-in shares, so one activation covers all ten.
constexpr const char* bundleName = "Amanorsac Analog";

constexpr int heartbeatMinutes = 60;
constexpr juce::int64 fortyEightHours = 48LL * 60 * 60 * 1000;
constexpr juce::int64 oneDay = 24LL * 60 * 60 * 1000;

juce::String normaliseKey(const juce::String& typed)
{
    return typed.trim().toUpperCase();
}
}

LicenseClient& LicenseClient::getInstance()
{
    static LicenseClient instance;
    return instance;
}

LicenseClient::LicenseClient() : store(bundleName) {}
LicenseClient::~LicenseClient() { stopTimer(); }

juce::String LicenseClient::baseUrl()
{
    // The shipped default is the live server. The override exists so a
    // developer can point at a staging box; a build that defaults anywhere
    // else sends every customer's activation into the void.
    const auto override = juce::SystemStats::getEnvironmentVariable("AMANORSAC_LICENSE_URL", {});
    return override.isNotEmpty() ? override.trim() : juce::String("https://amanorsac.studio");
}

juce::String LicenseClient::licenseKey() const { return store.loadKey(); }

int LicenseClient::daysOfGraceLeft() const
{
    const auto until = graceUntil.load();
    if (until <= 0) return 0;
    const auto remaining = until - juce::Time::currentTimeMillis();
    return remaining <= 0 ? 0 : static_cast<int>(remaining / oneDay) + 1;
}

void LicenseClient::start()
{
    if (started) return;
    started = true;
    refreshFromStore();
    startTimer(5000);   // notice another plug-in activating, and run the heartbeat

    if (! isLicensed() && store.loadKey().isNotEmpty())
    {
        // A key but no usable proof: try to get one without bothering anybody.
        activate(store.loadKey(), [](Result) {});
    }
}

void LicenseClient::setUnlicensed()
{
    licensed.store(false, std::memory_order_relaxed);
    currentState.store(State::unlicensed, std::memory_order_relaxed);
    graceUntil.store(0);
    sendChangeMessage();
}

void LicenseClient::applyProof(const juce::String& proof, const juce::String& key)
{
    Proof verified;
    juce::String error;
    if (! verifyProof(proof, verified, &error)) { setUnlicensed(); return; }

    // A proof for another machine or another key is not this machine's licence.
    if (verified.deviceKey != store.deviceId() || verified.licenseKey != normaliseKey(key))
    {
        setUnlicensed();
        return;
    }

    const auto now = juce::Time::currentTimeMillis();
    if (now > verified.graceUntil)
    {
        // Stale beyond the offline window: forget it and ask for a fresh one.
        store.storeProof({});
        setUnlicensed();
        return;
    }
    // A clock wound far back would otherwise extend the window indefinitely.
    if (now < verified.issuedAt - fortyEightHours) { setUnlicensed(); return; }

    graceUntil.store(verified.graceUntil);
    currentState.store(now > verified.expiresAt ? State::grace : State::licensed, std::memory_order_relaxed);
    licensed.store(true, std::memory_order_relaxed);
    sendChangeMessage();
}

void LicenseClient::refreshFromStore()
{
    seenStoreChange = store.lastChanged();
    const auto key = store.loadKey();
    const auto proof = store.loadProof();
    if (key.isEmpty() || proof.isEmpty()) { setUnlicensed(); return; }
    applyProof(proof, key);
}

void LicenseClient::timerCallback()
{
    // Another plug-in in another process may have activated the bundle.
    const auto changed = store.lastChanged();
    if (changed != seenStoreChange) refreshFromStore();

    const auto now = juce::Time::currentTimeMillis();
    const auto due = lastHeartbeat.load() + heartbeatMinutes * 60LL * 1000;
    if (now >= due && store.loadKey().isNotEmpty())
    {
        lastHeartbeat.store(now);
        activate(store.loadKey(), [](Result) {});
    }
}

LicenseClient::Result LicenseClient::post(const juce::String& endpoint, const juce::var& body,
                                          juce::String& responseBody) const
{
    Result result;
    const auto url = juce::URL(baseUrl() + endpoint).withPOSTData(juce::JSON::toString(body));

    int status = 0;
    juce::StringPairArray headers;
    auto stream = url.createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inPostData)
                                            .withExtraHeaders("Content-Type: application/json")
                                            .withConnectionTimeoutMs(15000)
                                            .withResponseHeaders(&headers)
                                            .withStatusCode(&status));

    if (stream == nullptr)
    {
        result.message = "Could not reach the licence server. Check your internet connection and try again.";
        return result;
    }
    responseBody = stream->readEntireStreamAsString();

    if (status >= 200 && status < 300) { result.ok = true; return result; }

    const auto parsed = juce::JSON::parse(responseBody);
    const auto code = parsed.getProperty("error", {}).toString();
    const auto message = parsed.getProperty("message", {}).toString();

    if (code == "no_such_license")
        result.message = "That key was not recognised. Check it against My Apps on amanorsac.studio.";
    else if (code == "device_limit_reached")
    {
        const auto maximum = static_cast<int>(parsed.getProperty("max_devices", 2));
        result.message = "This licence is already on " + juce::String(maximum) + " computers. "
                         "Remove one in My Apps, or use Deactivate on the other machine.";
        if (const auto* devices = parsed.getProperty("devices", {}).getArray())
            for (const auto& device : *devices)
                result.devicesInUse.add(device.getProperty("device_name", "Unnamed").toString());
    }
    else if (message.isNotEmpty()) result.message = message;
    else result.message = "The licence server could not complete that request. Please try again shortly.";
    return result;
}

void LicenseClient::activate(const juce::String& key, std::function<void(Result)> callback)
{
    const auto cleaned = normaliseKey(key);
    if (cleaned.isEmpty())
    {
        if (callback) callback({ false, "Enter the licence key from My Apps.", {} });
        return;
    }

    juce::Thread::launch([this, cleaned, callback]
    {
        auto* body = new juce::DynamicObject();
        body->setProperty("licenseKey", cleaned);
        body->setProperty("deviceKey", store.deviceId());
        body->setProperty("deviceLabel", LicenseStore::deviceLabel());

        juce::String response;
        auto result = post("/licenses/activate", juce::var(body), response);

        if (result.ok)
        {
            const auto proof = juce::JSON::parse(response).getProperty("proof", {}).toString();
            Proof verified;
            juce::String error;

            // The server answering 200 is not a licence. Only a signature that
            // verifies against the studio key is.
            if (proof.isEmpty() || ! verifyProof(proof, verified, &error))
            {
                result.ok = false;
                result.message = "The licence could not be verified. Please contact support@amanorsac.studio.";
            }
            else if (verified.deviceKey != store.deviceId() || verified.licenseKey != cleaned)
            {
                result.ok = false;
                result.message = "The licence could not be verified for this computer.";
            }
            else
            {
                store.storeKey(cleaned);
                store.storeProof(proof);
                lastHeartbeat.store(juce::Time::currentTimeMillis());
            }
        }

        juce::MessageManager::callAsync([this, result, callback]
        {
            if (result.ok) refreshFromStore();
            if (callback) callback(result);
        });
    });
}

void LicenseClient::deactivate(std::function<void(Result)> callback)
{
    const auto key = store.loadKey();
    juce::Thread::launch([this, key, callback]
    {
        Result result;
        if (key.isNotEmpty())
        {
            auto* body = new juce::DynamicObject();
            body->setProperty("licenseKey", key);
            body->setProperty("deviceKey", store.deviceId());
            juce::String response;
            result = post("/licenses/deactivate", juce::var(body), response);
            // An unknown key still means this machine should forget it.
            if (! result.ok && response.contains("no_such_license")) result.ok = true;
        }
        else result.ok = true;

        juce::MessageManager::callAsync([this, result, callback]
        {
            if (result.ok)
            {
                store.clear();
                seenStoreChange = store.lastChanged();
                setUnlicensed();
            }
            if (callback) callback(result);
        });
    });
}
}
