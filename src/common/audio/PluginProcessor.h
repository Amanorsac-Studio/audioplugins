#pragma once

#include "common/dsp/AnchorDSP.h"
#include "common/dsp/AnalogFrontEnd.h"
#include "common/licensing/LicenseClient.h"
#include "common/state/PluginSpec.h"
#include "common/presets/PresetManager.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>

namespace amanorsac
{
class PluginProcessor final : public juce::AudioProcessor
{
public:
    PluginProcessor();
    ~PluginProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return spec.displayName; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    // Host program list mirrors the user preset folder (Default + user presets).
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int) override;
    const juce::String getProgramName(int) override;
    void changeProgramName(int, const juce::String&) override;

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    /** Hosts drive their own bypass switch through this. Products whose
        contract has no bypass parameter return nullptr and the host bypasses
        externally, which is the JUCE default behaviour. */
    juce::AudioProcessorParameter* getBypassParameter() const override
    {
        return state.getParameter("bypass");
    }

    const PluginSpec spec;
    juce::UndoManager undoManager;
    juce::AudioProcessorValueTreeState state;
    // Declared after state: it listens to the parameters and is destroyed first.
    std::unique_ptr<presets::PresetManager> presetManager;
    [[nodiscard]] float getInputPeak(int channel) const noexcept
    {
        return inputPeaks[static_cast<size_t>(juce::jlimit(0, 1, channel))].load();
    }
    /** Gain reduction in dB (<= 0) for the compressor products, 0 elsewhere. */
    [[nodiscard]] float getGainReductionDb() const noexcept { return dsp.gainReductionDb(); }
    [[nodiscard]] float getOutputPeak(int channel) const noexcept
    {
        return outputPeaks[static_cast<size_t>(juce::jlimit(0, 1, channel))].load();
    }

private:
    static BusesProperties makeBusesProperties();
    AnchorDSP dsp;
    // Advances with the audio clock so the entitlement curve is time varying.
    // Ramps the output away when the bundle is not licensed, so it is
    // obvious rather than silent-but-working, and never clicks.
    juce::LinearSmoothedValue<float> entitlement;

    // The console strip around the engine, shared with the rack.
    AnalogFrontEnd frontEnd;
    std::array<std::atomic<float>, 2> inputPeaks {};
    std::array<std::atomic<float>, 2> outputPeaks {};

    JUCE_DECLARE_WEAK_REFERENCEABLE(PluginProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};
}
