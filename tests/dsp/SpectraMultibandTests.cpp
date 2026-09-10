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
    const juce::String getName() const override { return "SpectraTestHarness"; }
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

int fail(const juce::String& message)
{
    std::cerr << "FAIL: " << message << std::endl;
    return 1;
}

float runTone(amanorsac::AnchorDSP& dsp, HarnessProcessor& harness, double sampleRate,
              int blockSize, double frequency, int blocks, float& inputRms)
{
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
            phase += juce::MathConstants<double>::twoPi * frequency / sampleRate;
            input.setSample(0, sample, sampleValue);
            input.setSample(1, sample, sampleValue);
        }
        juce::AudioBuffer<float> dry;
        dry.makeCopyOf(input);
        dsp.process(input, harness.state, "D03");
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < blockSize; ++sample)
                if (!std::isfinite(input.getSample(channel, sample))) return -1.0f;
        if (block >= blocks * 3 / 4)
            for (int sample = 0; sample < blockSize; ++sample)
            {
                const auto in = dry.getSample(0, sample);
                const auto out = input.getSample(0, sample);
                inputEnergy += static_cast<double>(in * in);
                outputEnergy += static_cast<double>(out * out);
                ++measuredSamples;
            }
    }
    inputRms = static_cast<float>(std::sqrt(inputEnergy / measuredSamples));
    return static_cast<float>(std::sqrt(outputEnergy / measuredSamples));
}
}

int main()
{
    const auto spec = amanorsac::PluginSpec::fromEmbeddedJson();
    if (spec.id != "D03") return fail("Unexpected embedded plugin " + spec.id);
    HarnessProcessor harness(spec);
    if (!setParameter(harness.state, "band_count", 2.0f)
        || !setParameter(harness.state, "xover.01.frequency", 1000.0f)
        || !setParameter(harness.state, "band.01.bypass", 1.0f)
        || !setParameter(harness.state, "band.02.bypass", 1.0f))
        return fail("Missing D03 numbered parameters");

    for (const auto sampleRate : { 44100.0, 96000.0, 192000.0 })
        for (const auto blockSize : { 32, 256, 1024 })
        {
            amanorsac::AnchorDSP dsp;
            dsp.prepare(sampleRate, blockSize, 2);
            juce::AudioBuffer<float> silence(2, blockSize);
            silence.clear();
            dsp.process(silence, harness.state, "D03");
            if (silence.getMagnitude(0, blockSize) > 1.0e-7f)
                return fail("D03 silence safety failed");

            const auto blocks = juce::jmax(12, static_cast<int>(std::ceil(sampleRate * 0.12 / blockSize)));
            float dryRms = 0.0f;
            const auto reconstructedRms = runTone(dsp, harness, sampleRate, blockSize, 300.0, blocks, dryRms);
            if (reconstructedRms < 0.0f) return fail("Non-finite crossover output");
            const auto reconstructionRatio = reconstructedRms / dryRms;
            if (reconstructionRatio < 0.94f || reconstructionRatio > 1.06f)
                return fail("D03 reconstruction failed at " + juce::String(sampleRate) + " Hz / "
                            + juce::String(blockSize) + " samples");

            setParameter(harness.state, "split_phase", 1.0f);
            dsp.reset();
            float linearInputRms = 0.0f;
            const auto linearRms = runTone(dsp, harness, sampleRate, blockSize, 300.0, blocks, linearInputRms);
            const auto linearRatio = linearRms / linearInputRms;
            if (linearRms < 0.0f || linearRatio < 0.98f || linearRatio > 1.02f)
                return fail("D03 linear-phase reconstruction failed at " + juce::String(sampleRate) + " Hz / "
                            + juce::String(blockSize) + " samples");
            setParameter(harness.state, "split_phase", 0.0f);

            for (const auto band : { juce::String("band.01"), juce::String("band.02") })
            {
                setParameter(harness.state, band + ".bypass", 0.0f);
                setParameter(harness.state, band + ".threshold", -50.0f);
                setParameter(harness.state, band + ".range", -12.0f);
                setParameter(harness.state, band + ".ratio", 4.0f);
                setParameter(harness.state, band + ".attack", 0.1f);
            }
            setParameter(harness.state, "global_link", 100.0f);
            dsp.reset();
            float compressedInputRms = 0.0f;
            const auto compressedRms = runTone(dsp, harness, sampleRate, blockSize, 300.0, blocks,
                                               compressedInputRms);
            if (!(compressedRms >= 0.0f && compressedRms < compressedInputRms * 0.8f))
                return fail("D03 compression failed at " + juce::String(sampleRate) + " Hz / "
                            + juce::String(blockSize) + " samples");

            setParameter(harness.state, "band.01.bypass", 1.0f);
            setParameter(harness.state, "band.02.bypass", 1.0f);
        }

    const auto saved = harness.state.copyState();
    setParameter(harness.state, "xover.01.frequency", 4000.0f);
    harness.state.replaceState(saved.createCopy());
    const auto* restored = harness.state.getRawParameterValue("xover.01.frequency");
    if (restored == nullptr || std::abs(restored->load() - 1000.0f) > 0.1f)
        return fail("D03 crossover state round-trip failed");

    std::cout << "PASS: D03 multiband matrix, linked dynamics, and state round-trip" << std::endl;
    return 0;
}
