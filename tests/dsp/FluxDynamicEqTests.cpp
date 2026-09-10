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
    const juce::String getName() const override { return "FluxTestHarness"; }
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

bool setParameter(juce::AudioProcessorValueTreeState& state, const juce::String& id, float plainValue)
{
    if (auto* parameter = state.getParameter(id))
    {
        parameter->setValueNotifyingHost(parameter->convertTo0to1(plainValue));
        return true;
    }
    return false;
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
    if (spec.id != "D02") return fail("Unexpected embedded plugin " + spec.id);

    HarnessProcessor harness(spec);
    for (const auto& setting : std::initializer_list<std::pair<juce::String, float>> {
             { "band_count", 1.0f }, { "band.01.frequency", 1000.0f }, { "band.01.q", 2.0f },
             { "band.01.threshold", -50.0f }, { "band.01.range", -12.0f },
             { "band.01.ratio", 4.0f }, { "band.01.attack", 0.1f }, { "band.01.release", 100.0f } })
        if (!setParameter(harness.state, setting.first, setting.second))
            return fail("Missing parameter " + setting.first);

    for (const auto sampleRate : { 44100.0, 96000.0, 192000.0 })
    {
        for (const auto blockSize : { 32, 256, 1024 })
        {
            amanorsac::AnchorDSP dsp;
            dsp.prepare(sampleRate, blockSize, 2);
            juce::AudioBuffer<float> silence(2, blockSize);
            silence.clear();
            dsp.process(silence, harness.state, spec.id);
            if (silence.getMagnitude(0, blockSize) > 1.0e-7f)
                return fail("D02 silence safety failed");

            const auto blocks = juce::jmax(12, static_cast<int>(std::ceil(sampleRate * 0.12 / blockSize)));
            double phase = 0.0;
            double inputEnergy = 0.0;
            double outputEnergy = 0.0;
            int measuredSamples = 0;
            for (int block = 0; block < blocks; ++block)
            {
                juce::AudioBuffer<float> input(2, blockSize);
                for (int sample = 0; sample < blockSize; ++sample)
                {
                    const auto sampleValue = static_cast<float>(0.5 * std::sin(phase));
                    phase += juce::MathConstants<double>::twoPi * 1000.0 / sampleRate;
                    for (int channel = 0; channel < 2; ++channel) input.setSample(channel, sample, sampleValue);
                }
                juce::AudioBuffer<float> output;
                output.makeCopyOf(input);
                dsp.process(output, harness.state, spec.id);
                for (int channel = 0; channel < output.getNumChannels(); ++channel)
                    for (int sample = 0; sample < blockSize; ++sample)
                        if (!std::isfinite(output.getSample(channel, sample)))
                            return fail("Non-finite D02 output");
                if (block >= blocks * 3 / 4)
                    for (int sample = 0; sample < blockSize; ++sample)
                    {
                        const auto in = input.getSample(0, sample);
                        const auto out = output.getSample(0, sample);
                        inputEnergy += static_cast<double>(in * in);
                        outputEnergy += static_cast<double>(out * out);
                        ++measuredSamples;
                    }
            }

            const auto inputRms = std::sqrt(inputEnergy / measuredSamples);
            const auto outputRms = std::sqrt(outputEnergy / measuredSamples);
            if (!(outputRms < inputRms * 0.8))
                return fail("D02 attenuation failed at " + juce::String(sampleRate) + " Hz / "
                            + juce::String(blockSize) + " samples");
        }
    }

    const auto saved = harness.state.copyState();
    setParameter(harness.state, "band.01.range", -2.0f);
    harness.state.replaceState(saved.createCopy());
    const auto* restored = harness.state.getRawParameterValue("band.01.range");
    if (restored == nullptr || std::abs(restored->load() + 12.0f) > 0.01f)
        return fail("D02 indexed state round-trip failed");

    std::cout << "PASS: D02 12-slot dynamic-EQ matrix and state round-trip" << std::endl;
    return 0;
}
