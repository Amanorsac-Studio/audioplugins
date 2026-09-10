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
        : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
          state(*this, nullptr, "AMANORSAC_TEST_STATE", amanorsac::PluginSpec::createParameterLayout(spec)) {}
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override
    { return layouts.getMainInputChannelSet() == layouts.getMainOutputChannelSet(); }
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    const juce::String getName() const override { return "AnalogCharacterHarness"; }
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

bool setParameter(juce::AudioProcessorValueTreeState& state, const juce::String& id, float value)
{
    if (auto* parameter = state.getParameter(id))
    {
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
        return true;
    }
    return false;
}

bool finite(const juce::AudioBuffer<float>& buffer)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            if (!std::isfinite(buffer.getSample(channel, sample))) return false;
    return true;
}

double differenceEnergy(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    double result = 0.0;
    for (int channel = 0; channel < a.getNumChannels(); ++channel)
        for (int sample = 0; sample < a.getNumSamples(); ++sample)
        {
            const auto difference = a.getSample(channel, sample) - b.getSample(channel, sample);
            result += static_cast<double>(difference * difference);
        }
    return result;
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
    if (spec.id != "A02" && spec.id != "A03" && spec.id != "A04" && spec.id != "A05" && spec.id != "A09")
        return fail("Unexpected embedded plugin " + spec.id);
    HarnessProcessor harness(spec);
    const auto focusParameter = spec.id == "A04" ? juce::String("saturation") : juce::String("drive");
    const auto focusValue = spec.id == "A05" ? 30.0f : 80.0f;
    if (!setParameter(harness.state, focusParameter, focusValue)) return fail("Missing focus parameter");
    if (spec.id == "A02")
    {
        setParameter(harness.state, "saturation", 85.0f);
        setParameter(harness.state, "low_tone", 3.0f);
        setParameter(harness.state, "high_tone", -2.0f);
        setParameter(harness.state, "transformer", 2.0f);
    }
    else
    if (spec.id == "A03")
    {
        setParameter(harness.state, "bus_color", 75.0f);
        setParameter(harness.state, "low", 3.0f);
        setParameter(harness.state, "mid", -4.0f);
        setParameter(harness.state, "high", 2.0f);
        setParameter(harness.state, "stereo_mode", 2.0f);
    }
    else if (spec.id == "A04")
    {
        setParameter(harness.state, "formula", 0.0f);
        setParameter(harness.state, "wow", 20.0f);
        setParameter(harness.state, "flutter", 20.0f);
        setParameter(harness.state, "head_bump", 60.0f);
        setParameter(harness.state, "hf_rolloff", 30.0f);
    }
    else if (spec.id == "A05")
    {
        setParameter(harness.state, "topology", 2.0f);
        setParameter(harness.state, "density", 80.0f);
        setParameter(harness.state, "harmonic_balance", 50.0f);
        setParameter(harness.state, "tone", 25.0f);
    }
    else
    {
        setParameter(harness.state, "low_boost", 6.0f);
        setParameter(harness.state, "mid_gain", -5.0f);
        setParameter(harness.state, "high_boost", 5.0f);
        setParameter(harness.state, "output_stage", 0.0f);
    }

    for (const auto sampleRate : { 44100.0, 96000.0, 192000.0 })
        for (const auto blockSize : { 32, 256, 1024 })
        {
            amanorsac::AnchorDSP dsp;
            dsp.prepare(sampleRate, blockSize, 2);
            juce::AudioBuffer<float> silence(2, blockSize);
            silence.clear();
            dsp.process(silence, harness.state, spec.id);
            if (!finite(silence) || silence.getMagnitude(0, blockSize) > 1.0e-7f)
                return fail(spec.id + " silence safety failed");

            juce::AudioBuffer<float> input(2, blockSize);
            for (int sample = 0; sample < blockSize; ++sample)
            {
                const auto left = static_cast<float>(0.2 * std::sin(juce::MathConstants<double>::twoPi
                                                                    * 997.0 * sample / sampleRate));
                input.setSample(0, sample, left);
                input.setSample(1, sample, left * 0.7f);
            }
            juce::AudioBuffer<float> output;
            output.makeCopyOf(input);
            dsp.process(output, harness.state, spec.id);
            if (!finite(output)) return fail(spec.id + " produced non-finite output");
            if (differenceEnergy(input, output) <= 1.0e-8)
                return fail(spec.id + " made no measurable change at " + juce::String(sampleRate)
                            + " Hz / " + juce::String(blockSize) + " samples");
        }

    if (spec.id == "A03")
    {
        setParameter(harness.state, "mix", 0.0f);
        amanorsac::AnchorDSP dsp;
        dsp.prepare(48000.0, 256, 2);
        juce::AudioBuffer<float> dry(2, 256);
        for (int sample = 0; sample < 256; ++sample)
        {
            const auto value = static_cast<float>(sample) / 512.0f;
            dry.setSample(0, sample, value);
            dry.setSample(1, sample, value);
        }
        juce::AudioBuffer<float> output;
        output.makeCopyOf(dry);
        dsp.process(output, harness.state, spec.id);
        if (differenceEnergy(dry, output) > 1.0e-10)
            return fail("A03 zero-percent mix did not preserve dry audio");
        setParameter(harness.state, "mix", 100.0f);

        setParameter(harness.state, "noise", 1.0f);
        juce::AudioBuffer<float> noise(2, 256);
        noise.clear();
        dsp.process(noise, harness.state, spec.id);
        if (!finite(noise) || noise.getMagnitude(0, 256) <= 1.0e-8f)
            return fail("A03 optional noise path did not produce bounded output");
        setParameter(harness.state, "noise", 0.0f);
    }

    const auto saved = harness.state.copyState();
    setParameter(harness.state, focusParameter, 5.0f);
    harness.state.replaceState(saved.createCopy());
    const auto* restored = harness.state.getRawParameterValue(focusParameter);
    if (restored == nullptr || std::abs(restored->load() - focusValue) > 0.01f)
        return fail(spec.id + " state round-trip failed");

    std::cout << "PASS: " << spec.id << " analog character matrix and state round-trip" << std::endl;
    return 0;
}
