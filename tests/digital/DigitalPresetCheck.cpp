// Loads every factory preset of one digital product and proves it applies,
// changes the settings, and leaves the engine producing finite audio.

#include <juce_audio_utils/juce_audio_utils.h>

#include "common/audio/PluginProcessor.h"
#include "common/presets/PresetManager.h"
#include "common/licensing/LicenseClient.h"
#include "common/licensing/Entitlement.h"

#include <cmath>
#include <iostream>

using namespace amanorsac;

namespace
{
int failures = 0;

void check(bool ok, const juce::String& what)
{
    std::cout << (ok ? "  ok    " : "  FAIL  ") << what << "\n";
    if (! ok) ++failures;
}

juce::AudioBuffer<float> noise(double rate, double seconds, float level)
{
    juce::Random random(11);
    juce::AudioBuffer<float> b(2, static_cast<int>(rate * seconds));
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < b.getNumSamples(); ++i) b.setSample(c, i, (random.nextFloat() * 2.0f - 1.0f) * level);
    return b;
}

bool runAudio(PluginProcessor& processor, float& peak)
{
    auto buffer = noise(48000.0, 1.0, 0.3f);
    juce::MidiBuffer midi;
    peak = 0.0f;
    for (int start = 0; start < buffer.getNumSamples(); start += 256)
    {
        const auto count = juce::jmin(256, buffer.getNumSamples() - start);
        juce::AudioBuffer<float> block(buffer.getArrayOfWritePointers(), 2, start, count);
        processor.processBlock(block, midi);
    }
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto s = buffer.getSample(c, i);
            if (! std::isfinite(s)) return false;
            peak = juce::jmax(peak, std::abs(s));
        }
    return true;
}

/** A fingerprint of every parameter, to prove a preset actually moved things. */
juce::String fingerprint(const PluginProcessor& processor)
{
    juce::String out;
    for (const auto& descriptor : processor.spec.parameters)
        if (const auto* value = processor.state.getRawParameterValue(descriptor.id))
            out << juce::String(value->load(), 4) << ",";
    return out;
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;

    PluginProcessor processor;
    const auto product = processor.spec.id + " " + processor.spec.displayName;
    std::cout << "== " << product << " presets ==\n";

    auto* manager = processor.presetManager.get();
    check(manager != nullptr, "has a preset manager");
    if (manager == nullptr) return 1;

    processor.prepareToPlay(48000.0, 256);

    // Without a licence the engine deliberately outputs nothing, so audio can
    // only be judged on a machine that has one. Say which case this run is.
    // Audio is expected unless this build enforces the licence and the machine
    // has none; then silence is the correct behaviour.
    const auto enforcing = AMANORSAC_LICENSING_ENABLED != 0;
    const auto licensed = ! enforcing || licensing::LicenseClient::getInstance().isLicensed();
    std::cout << "  note  " << (licensed ? (enforcing ? "licensed: output is checked for level" : "test build, enforcement off: output is checked for level")
                                                         : "not licensed: output is silent by design, levels not checked") << "\n";

    const auto& items = manager->presets();
    int factory = 0;
    for (int i = 0; i < static_cast<int>(items.size()); ++i)
        if (manager->isFactory(i) && items[static_cast<size_t>(i)].name != "Default") ++factory;
    check(factory >= 12, juce::String(factory) + " factory presets in the bank");

    juce::StringArray seen;
    for (int i = 0; i < static_cast<int>(items.size()); ++i)
    {
        const auto name = items[static_cast<size_t>(i)].name;
        if (name == "Default" || ! manager->isFactory(i)) continue;

        const auto before = fingerprint(processor);
        const auto loaded = manager->load(i);
        const auto after = fingerprint(processor);
        float peak = 0.0f;
        const auto finite = runAudio(processor, peak);
        const auto category = items[static_cast<size_t>(i)].category;

        const auto levelOk = licensed ? peak > 0.0005f && peak < 12.0f : peak < 12.0f;
        check(loaded && finite && levelOk && category.isNotEmpty() && ! seen.contains(name) && before != after,
              "\"" + name + "\" (" + category + ") loads, changes the settings, stays finite"
                  + (licensed ? ", peak " + juce::String(peak, 2) : juce::String()));
        seen.add(name);
    }

    // At least one preset must differ from the contract defaults, or the bank
    // would be decoration.
    manager->loadDefault();
    const auto defaults = fingerprint(processor);
    bool anyDifferent = false;
    for (int i = 0; i < static_cast<int>(items.size()); ++i)
        if (manager->isFactory(i) && items[static_cast<size_t>(i)].name != "Default")
        {
            manager->load(i);
            if (fingerprint(processor) != defaults) { anyDifferent = true; break; }
        }
    check(anyDifferent, "presets change the settings");

    std::cout << (failures == 0 ? "PASS" : "FAIL") << ": " << product << ", " << failures << " failure(s)\n";
    return failures == 0 ? 0 : 1;
}
