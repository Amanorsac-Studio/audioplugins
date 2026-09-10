#pragma once

#include "common/presets/PresetManager.h"
#include "common/state/PluginSpec.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <functional>

namespace amanorsac
{
/** Everything a product's editor needs from whatever is hosting it.

    A product editor is written once and used twice: as the plugin's own window,
    and as one slot of the rack. The two differ only in where the parameters
    live, so the editor never names a processor type; it asks its host. In the
    rack a module's controls sit under an "A01." prefix in the rack's tree, and
    `qualify` is what maps a contract id onto the real one.
*/
struct ProductHost
{
    const PluginSpec* spec = nullptr;                    // the module contract, ids unprefixed
    juce::AudioProcessorValueTreeState* state = nullptr;
    juce::UndoManager* undoManager = nullptr;
    presets::PresetManager* presetManager = nullptr;

    std::function<juce::String(const juce::String&)> qualify;
    std::function<float(int)> inputPeak, outputPeak;
    std::function<float()> gainReduction;
    std::function<double()> sampleRate;
    std::function<int()> latency;

    /** False inside the rack, which draws its own top bar and owns the presets,
        so a module does not put a second preset system on the screen. */
    bool ownsTopBar = true;

    [[nodiscard]] juce::String id(const juce::String& contractId) const
    {
        return qualify ? qualify(contractId) : contractId;
    }

    [[nodiscard]] juce::AudioProcessorParameter* parameter(const juce::String& contractId) const
    {
        return state != nullptr ? state->getParameter(id(contractId)) : nullptr;
    }

    [[nodiscard]] std::atomic<float>* value(const juce::String& contractId) const
    {
        return state != nullptr ? state->getRawParameterValue(id(contractId)) : nullptr;
    }

    [[nodiscard]] float peakIn(int channel) const { return inputPeak ? inputPeak(channel) : 0.0f; }
    [[nodiscard]] float peakOut(int channel) const { return outputPeak ? outputPeak(channel) : 0.0f; }
    [[nodiscard]] float reductionDb() const { return gainReduction ? gainReduction() : 0.0f; }
};
}
