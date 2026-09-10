#include "common/dsp/AnchorDSP.h"
#include "common/state/PluginSpec.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <iostream>

namespace
{
class HarnessProcessor final : public juce::AudioProcessor
{
public:
    explicit HarnessProcessor(const amanorsac::PluginSpec& spec)
        : AudioProcessor(BusesProperties()
                             .withInput("Input", juce::AudioChannelSet::stereo(), true)
                             .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
          state(*this, nullptr, "AMANORSAC_TEST_STATE", amanorsac::PluginSpec::createParameterLayout(spec))
    {
    }

    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override
    {
        return layouts.getMainInputChannelSet() == layouts.getMainOutputChannelSet();
    }
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    const juce::String getName() const override { return "AnchorTestHarness"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

    juce::AudioProcessorValueTreeState state;
};

bool allFinite(const juce::AudioBuffer<float>& buffer)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            if (!std::isfinite(buffer.getSample(channel, sample)))
                return false;
    return true;
}

float differenceEnergy(const juce::AudioBuffer<float>& left, const juce::AudioBuffer<float>& right)
{
    double energy = 0.0;
    for (int channel = 0; channel < left.getNumChannels(); ++channel)
        for (int sample = 0; sample < left.getNumSamples(); ++sample)
        {
            const auto difference = left.getSample(channel, sample) - right.getSample(channel, sample);
            energy += static_cast<double>(difference * difference);
        }
    return static_cast<float>(energy);
}

bool setParameter(juce::AudioProcessorValueTreeState& state, const juce::String& id, float plainValue)
{
    auto* parameter = state.getParameter(id);
    if (parameter == nullptr)
        return false;
    parameter->setValueNotifyingHost(parameter->convertTo0to1(plainValue));
    return true;
}

int fail(const juce::String& message)
{
    std::cerr << "FAIL: " << message << std::endl;
    return 1;
}
}

int main()
{
    const auto spec = amanorsac::PluginSpec::fromEmbeddedJson();
    if (spec.id != "D01" && spec.id != "A01")
        return fail("Test target embedded an unexpected plugin contract: " + spec.id);

    HarnessProcessor harness(spec);
    amanorsac::AnchorDSP dsp;

    const auto focusParameter = spec.id == "D01" ? juce::String("band.01.gain")
                                                  : juce::String("band.mid.gain");
    if (spec.id == "D01" && (spec.parameters.size() != 346
                             || harness.state.getParameter("band.24.delta") == nullptr))
        return fail("D01 fixed 24-band contract expansion failed");
    if (spec.id == "D01"
        && (harness.state.getRawParameterValue("band.06.enabled")->load() < 0.5f
            || harness.state.getRawParameterValue("band.07.enabled")->load() > 0.5f))
        return fail("D01 approved six-node startup state failed");
    if (!setParameter(harness.state, focusParameter, 6.0f))
        return fail("Missing anchor focus parameter " + focusParameter);

    for (const auto sampleRate : { 44100.0, 96000.0, 192000.0 })
    {
        for (const auto blockSize : { 32, 256, 1024 })
        {
            dsp.prepare(sampleRate, blockSize, 2);

            juce::AudioBuffer<float> silence(2, blockSize);
            silence.clear();
            dsp.process(silence, harness.state, spec.id);
            if (!allFinite(silence) || silence.getMagnitude(0, blockSize) > 1.0e-7f)
                return fail("Silence safety failed at " + juce::String(sampleRate) + " Hz / "
                            + juce::String(blockSize) + " samples");

            juce::AudioBuffer<float> input(2, blockSize);
            for (int channel = 0; channel < input.getNumChannels(); ++channel)
                for (int sample = 0; sample < blockSize; ++sample)
                    input.setSample(channel, sample,
                                    static_cast<float>(0.2 * std::sin(juce::MathConstants<double>::twoPi * 1000.0
                                                                      * static_cast<double>(sample) / sampleRate)));

            juce::AudioBuffer<float> processed;
            processed.makeCopyOf(input);
            dsp.process(processed, harness.state, spec.id);
            if (!allFinite(processed))
                return fail("Non-finite output at " + juce::String(sampleRate) + " Hz / "
                            + juce::String(blockSize) + " samples");
            if (differenceEnergy(input, processed) <= 1.0e-8f)
                return fail("Anchor DSP made no measurable change at " + juce::String(sampleRate)
                            + " Hz / " + juce::String(blockSize) + " samples");
        }
    }

    if (spec.id == "A01")
    {
        if (harness.state.getParameter("oversampling") == nullptr)
            return fail("A01 oversampling contract is missing");

        dsp.prepare(48000.0, 512, 2);
        juce::AudioBuffer<float> dryReference(2, 512);
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < 512; ++sample)
                dryReference.setSample(channel, sample,
                    static_cast<float>(0.17 * std::sin(juce::MathConstants<double>::twoPi
                                                       * (channel == 0 ? 317.0 : 911.0)
                                                       * sample / 48000.0)));

        setParameter(harness.state, "mix", 0.0f);
        setParameter(harness.state, "character", 3.0f);
        setParameter(harness.state, "drive", 10.0f);
        setParameter(harness.state, "oversampling", 2.0f);
        juce::AudioBuffer<float> dryProcessed;
        dryProcessed.makeCopyOf(dryReference);
        dsp.process(dryProcessed, harness.state, spec.id);
        // DEC-0010: the dry path is delayed by the reported oversampling latency so
        // MIX 0 % stays sample-aligned with the wet path, exactly as the host sees it.
        juce::AudioBuffer<float> alignedReference(2, 512);
        alignedReference.clear();
        const auto latency = dsp.latencySamples();
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = latency; sample < 512; ++sample)
                alignedReference.setSample(channel, sample, dryReference.getSample(channel, sample - latency));
        if (differenceEnergy(alignedReference, dryProcessed) > 1.0e-9f)
            return fail("A01 zero-mix path is not sample-accurate dry (allowing reported latency "
                        + juce::String(latency) + ")");

        setParameter(harness.state, "mix", 100.0f);
        for (int mode = 0; mode < 4; ++mode)
        {
            setParameter(harness.state, "character", static_cast<float>(mode));
            juce::AudioBuffer<float> processed;
            processed.makeCopyOf(dryReference);
            dsp.process(processed, harness.state, spec.id);
            if (!allFinite(processed))
                return fail("A01 character/oversampling path produced non-finite output");
        }
    }

    const auto saved = harness.state.copyState();
    if (!setParameter(harness.state, focusParameter, -6.0f))
        return fail("Could not mutate state for round-trip test");
    harness.state.replaceState(saved.createCopy());
    const auto* restored = harness.state.getRawParameterValue(focusParameter);
    if (restored == nullptr || std::abs(restored->load() - 6.0f) > 0.01f)
        return fail("State round-trip did not restore the anchor parameter");

    std::cout << "PASS: " << spec.id << " " << spec.displayName
              << " silence/sine matrix and state round-trip" << std::endl;
    return 0;
}
