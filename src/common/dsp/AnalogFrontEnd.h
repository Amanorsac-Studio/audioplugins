#pragma once

#include "common/state/PluginSpec.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

namespace amanorsac
{
/** The console strip around an analog engine: input trim, phase, high-pass and
    M/S encode before it, and M/S decode, wet/dry mix, output trim and a
    click-free bypass crossfade after it.

    HERITAGE EQ and PLATE FOUR implement these stages inside their own signal
    paths (their faceplates place them in the middle of the chain). Every other
    analog product gets them from here, so the behaviour is identical across the
    line and cannot drift per product. The same object serves a single plugin
    and one rack slot: `parameterPrefix` selects which module's parameters it
    reads out of a shared tree.
*/
class AnalogFrontEnd
{
public:
    /** Decides which stages this product needs from its contract. */
    void configure(const PluginSpec&, juce::String parameterPrefix = {});
    void prepare(double sampleRate, int maximumBlockSize);
    void reset();

    [[nodiscard]] bool isActive() const noexcept { return active; }

    /** Trim, phase, HPF and M/S encode. Keeps a copy of the untouched input for
        the mix and bypass stages, so call `processBack` on the same block. */
    void processFront(juce::AudioBuffer<float>&, const juce::AudioProcessorValueTreeState&, int channels);
    void processBack(juce::AudioBuffer<float>&, const juce::AudioProcessorValueTreeState&, int channels);

private:
    [[nodiscard]] float value(const juce::AudioProcessorValueTreeState&, const juce::String& id,
                              float fallback = 0.0f) const;

    juce::String prefix;
    bool active = false, hasInput = false, hasOutput = false, hasMix = false;
    bool hasHpf = false, hpfIsSwitch = false, hasMidSide = false, hasPhase = false, hasBypass = false;
    float hpfMinimum = 20.0f;

    juce::AudioBuffer<float> rawCopy;
    juce::LinearSmoothedValue<float> bypassSmoother;
    std::array<juce::dsp::IIR::Filter<float>, 2> highPass;
    float frontGain = 1.0f;
    bool encoded = false;
    double sampleRate = 44100.0;
};
}
