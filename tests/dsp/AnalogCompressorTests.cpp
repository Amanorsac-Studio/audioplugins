#include "common/dsp/AnchorDSP.h"
#include "common/state/PluginSpec.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <iostream>

namespace
{
class Harness final : public juce::AudioProcessor
{
public:
    explicit Harness(const amanorsac::PluginSpec& spec)
        : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
          state(*this, nullptr, "TEST", amanorsac::PluginSpec::createParameterLayout(spec)) {}
    void prepareToPlay(double, int) override {} void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& l) const override { return l.getMainInputChannelSet() == l.getMainOutputChannelSet(); }
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    juce::AudioProcessorEditor* createEditor() override { return nullptr; } bool hasEditor() const override { return false; }
    const juce::String getName() const override { return "CompressorHarness"; }
    bool acceptsMidi() const override { return false; } bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; } double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; } int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {} const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {} void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}
    juce::AudioProcessorValueTreeState state;
};

bool set(juce::AudioProcessorValueTreeState& state, const juce::String& id, float value)
{
    if (auto* parameter = state.getParameter(id))
    { parameter->setValueNotifyingHost(parameter->convertTo0to1(value)); return true; }
    return false;
}

int fail(const juce::String& message) { std::cerr << "FAIL: " << message << std::endl; return 1; }
}

int main()
{
    const auto spec = amanorsac::PluginSpec::fromEmbeddedJson();
    if (spec.id != "A06" && spec.id != "A07" && spec.id != "A08") return fail("Unexpected plugin " + spec.id);
    Harness harness(spec);
    juce::String focus;
    float focusValue = 0.0f;
    if (spec.id == "A06")
    {
        focus = "input"; focusValue = 18.0f; set(harness.state, focus, focusValue);
        set(harness.state, "ratio", 4.0f); set(harness.state, "attack", 0.1f); set(harness.state, "release", 80.0f);
    }
    else if (spec.id == "A07")
    {
        focus = "peak_reduction"; focusValue = 90.0f; set(harness.state, focus, focusValue);
        set(harness.state, "response", 0.0f); set(harness.state, "release", 0.1f);
    }
    else
    {
        focus = "threshold"; focusValue = -40.0f; set(harness.state, focus, focusValue);
        set(harness.state, "ratio", 3.0f); set(harness.state, "attack", 0.1f); set(harness.state, "release", 0.1f);
    }

    for (const auto sampleRate : { 44100.0, 96000.0, 192000.0 })
        for (const auto blockSize : { 32, 256, 1024 })
        {
            amanorsac::AnchorDSP dsp;
            dsp.prepare(sampleRate, blockSize, 2);
            juce::AudioBuffer<float> silence(2, blockSize); silence.clear();
            dsp.process(silence, harness.state, spec.id);
            if (silence.getMagnitude(0, blockSize) > 1.0e-7f) return fail(spec.id + " silence failed");

            const auto blocks = juce::jmax(12, static_cast<int>(std::ceil(sampleRate * 0.12 / blockSize)));
            double phase = 0.0, inputEnergy = 0.0, outputEnergy = 0.0;
            int measured = 0;
            for (int block = 0; block < blocks; ++block)
            {
                juce::AudioBuffer<float> audio(2, blockSize);
                for (int sample = 0; sample < blockSize; ++sample)
                {
                    const auto v = static_cast<float>(0.65 * std::sin(phase));
                    phase += juce::MathConstants<double>::twoPi * 997.0 / sampleRate;
                    audio.setSample(0, sample, v); audio.setSample(1, sample, v * 0.75f);
                }
                juce::AudioBuffer<float> dry; dry.makeCopyOf(audio);
                dsp.process(audio, harness.state, spec.id);
                if (block >= blocks * 3 / 4)
                    for (int sample = 0; sample < blockSize; ++sample)
                    {
                        const auto in = dry.getSample(0, sample), out = audio.getSample(0, sample);
                        if (!std::isfinite(out)) return fail(spec.id + " non-finite output");
                        inputEnergy += in * in; outputEnergy += out * out; ++measured;
                    }
            }
            if (std::abs(outputEnergy - inputEnergy) / inputEnergy < 0.02)
                return fail(spec.id + " dynamics made no measurable level change");
        }

    set(harness.state, "mix", 0.0f);
    amanorsac::AnchorDSP dryDsp; dryDsp.prepare(48000.0, 128, 2);
    juce::AudioBuffer<float> dry(2, 128);
    for (int sample = 0; sample < 128; ++sample)
    {
        dry.setSample(0, sample, 0.1f);
        dry.setSample(1, sample, 0.1f);
    }
    juce::AudioBuffer<float> mixed; mixed.makeCopyOf(dry); dryDsp.process(mixed, harness.state, spec.id);
    for (int sample = 0; sample < 128; ++sample)
        if (std::abs(mixed.getSample(0, sample) - dry.getSample(0, sample)) > 1.0e-6f)
            return fail(spec.id + " zero mix failed");

    const auto saved = harness.state.copyState(); set(harness.state, focus, 0.0f); harness.state.replaceState(saved.createCopy());
    const auto* restored = harness.state.getRawParameterValue(focus);
    if (restored == nullptr || std::abs(restored->load() - focusValue) > 0.01f) return fail(spec.id + " state failed");
    std::cout << "PASS: " << spec.id << " compressor matrix" << std::endl;
    return 0;
}
