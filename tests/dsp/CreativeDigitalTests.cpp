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
          state(*this, nullptr, "State", amanorsac::PluginSpec::createParameterLayout(spec)) {}
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layout) const override { return layout.getMainInputChannelSet() == layout.getMainOutputChannelSet(); }
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    const juce::String getName() const override { return "CreativeDigitalTest"; }
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

bool set(juce::AudioProcessorValueTreeState& state, const juce::String& id, float value)
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
}

int main()
{
    const auto spec = amanorsac::PluginSpec::fromEmbeddedJson();
    if (spec.id != "D07" && spec.id != "D08" && spec.id != "D09" && spec.id != "D10")
        return fail("unexpected product " + spec.id);

    Harness harness(spec);
    juce::String stateProbe;
    float stateValue = 0.0f;
    if (spec.id == "D07")
    {
        const float crossovers[] { 120.0f, 400.0f, 1200.0f, 4000.0f, 10000.0f };
        for (int i = 0; i < 5; ++i) set(harness.state, "xover." + juce::String(i + 1).paddedLeft('0', 2) + ".frequency", crossovers[i]);
        set(harness.state, "zone.03.harmonics", 48.0f);
        set(harness.state, "global_mix", 100.0f);
        stateProbe = "zone.03.harmonics";
        stateValue = 48.0f;
    }
    else if (spec.id == "D08")
    {
        for (int i = 2; i <= 8; ++i) set(harness.state, "tap." + juce::String(i).paddedLeft('0', 2) + ".enabled", 0.0f);
        set(harness.state, "tap.01.time", 20.0f);
        set(harness.state, "tap.01.level", 0.0f);
        set(harness.state, "tap.01.filter", 18000.0f);
        set(harness.state, "feedback", 20.0f);
        set(harness.state, "mix", 100.0f);
        set(harness.state, "duck", 0.0f);
        set(harness.state, "mod_depth", 0.0f);
        stateProbe = "tap.01.time";
        stateValue = 20.0f;
    }
    else if (spec.id == "D09")
    {
        set(harness.state, "pre_delay", 0.0f);
        set(harness.state, "decay", 1.0f);
        set(harness.state, "mix", 100.0f);
        stateProbe = "decay";
        stateValue = 1.0f;
    }
    else
    {
        const float crossovers[] { 120.0f, 500.0f, 2000.0f, 8000.0f };
        for (int i = 0; i < 4; ++i) set(harness.state, "xover." + juce::String(i + 1).paddedLeft('0', 2) + ".frequency", crossovers[i]);
        set(harness.state, "band.03.width", 220.0f);
        set(harness.state, "global_width", 180.0f);
        stateProbe = "global_width";
        stateValue = 180.0f;
    }

    for (const auto sampleRate : { 44100.0, 96000.0, 192000.0 })
    for (const auto blockSize : { 32, 256, 1024 })
    {
        amanorsac::AnchorDSP dsp;
        dsp.prepare(sampleRate, blockSize, 2);
        double phase = 0.0;
        double difference = 0.0;
        double energy = 0.0;
        const auto blocks = juce::jmax(16, static_cast<int>(std::ceil(sampleRate * 0.24 / blockSize)));
        for (int block = 0; block < blocks; ++block)
        {
            juce::AudioBuffer<float> audio(2, blockSize);
            for (int i = 0; i < blockSize; ++i)
            {
                float left = 0.0f, right = 0.0f;
                if (spec.id == "D08" || spec.id == "D09")
                    left = right = (block == 0 && i == 0) ? 0.8f : 0.0f;
                else
                {
                    left = static_cast<float>(0.55 * std::sin(phase));
                    right = static_cast<float>(0.35 * std::sin(phase * 1.013 + 0.4));
                    phase += juce::MathConstants<double>::twoPi * 1000.0 / sampleRate;
                }
                audio.setSample(0, i, left);
                audio.setSample(1, i, right);
            }
            juce::AudioBuffer<float> dry;
            dry.makeCopyOf(audio);
            dsp.process(audio, harness.state, spec.id);
            for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < blockSize; ++i)
            {
                const auto output = audio.getSample(channel, i);
                if (!std::isfinite(output)) return fail("non-finite output");
                const auto delta = output - dry.getSample(channel, i);
                difference += delta * delta;
                energy += output * output;
            }
        }
        if (difference < 1.0e-8 || energy < 1.0e-8)
            return fail("no measurable processing at " + juce::String(sampleRate) + "/" + juce::String(blockSize));
    }

    if (spec.id == "D10")
    {
        set(harness.state, "mono_check", 1.0f);
        amanorsac::AnchorDSP dsp;
        dsp.prepare(48000.0, 256, 2);
        juce::AudioBuffer<float> audio(2, 256);
        for (int i = 0; i < 256; ++i) { audio.setSample(0, i, i * 0.001f); audio.setSample(1, i, 0.25f - i * 0.0005f); }
        dsp.process(audio, harness.state, spec.id);
        for (int i = 0; i < 256; ++i)
            if (std::abs(audio.getSample(0, i) - audio.getSample(1, i)) > 1.0e-6f) return fail("mono check mismatch");
    }

    if (spec.id == "D08" || spec.id == "D09")
    {
        // Mix at zero must be a sample-accurate dry path: an insert must never mute the DAW.
        set(harness.state, "mix", 0.0f);
        amanorsac::AnchorDSP dryPath;
        dryPath.prepare(48000.0, 512, 2);
        juce::AudioBuffer<float> audio(2, 512);
        juce::AudioBuffer<float> reference(2, 512);
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < 512; ++sample)
            {
                const auto value = static_cast<float>(0.35 * std::sin(juce::MathConstants<double>::twoPi
                                                                      * 997.0 * sample / 48000.0));
                audio.setSample(channel, sample, value);
                reference.setSample(channel, sample, value);
            }
        dryPath.process(audio, harness.state, spec.id);
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < 512; ++sample)
                if (std::abs(audio.getSample(channel, sample) - reference.getSample(channel, sample)) > 1.0e-6f)
                    return fail("mix=0 did not preserve the dry path");

        // Exercise the user-reported runaway-feedback case for a sustained interval.
        set(harness.state, "mix", 100.0f);
        if (spec.id == "D08")
        {
            set(harness.state, "feedback", 98.0f);
            set(harness.state, "duck", 0.0f);
            set(harness.state, "mod_depth", 50.0f);
            for (int tap = 1; tap <= 8; ++tap)
            {
                const auto prefix = "tap." + juce::String(tap).paddedLeft('0', 2);
                set(harness.state, prefix + ".enabled", 1.0f);
                set(harness.state, prefix + ".level", 6.0f);
                set(harness.state, prefix + ".time", 20.0f + tap * 13.0f);
            }
        }
        else
        {
            set(harness.state, "decay", 40.0f);
            set(harness.state, "density", 100.0f);
            set(harness.state, "diffusion", 100.0f);
            set(harness.state, "width", 200.0f);
        }

        amanorsac::AnchorDSP stress;
        constexpr int stressBlockSize = 256;
        constexpr int stressBlocks = 2250; // 12 seconds at 48 kHz.
        stress.prepare(48000.0, stressBlockSize, 2);
        float maximumMagnitude = 0.0f;
        for (int block = 0; block < stressBlocks; ++block)
        {
            juce::AudioBuffer<float> stressAudio(2, stressBlockSize);
            stressAudio.clear();
            if (block == 0)
            {
                stressAudio.setSample(0, 0, 0.9f);
                stressAudio.setSample(1, 0, 0.9f);
            }
            stress.process(stressAudio, harness.state, spec.id);
            for (int channel = 0; channel < 2; ++channel)
                for (int sample = 0; sample < stressBlockSize; ++sample)
                {
                    const auto output = stressAudio.getSample(channel, sample);
                    if (!std::isfinite(output)) return fail("non-finite long feedback tail");
                    maximumMagnitude = juce::jmax(maximumMagnitude, std::abs(output));
                }
        }
        if (maximumMagnitude > 2.05f)
            return fail("unbounded long feedback tail: " + juce::String(maximumMagnitude));
    }

    set(harness.state, stateProbe, stateValue);
    const auto saved = harness.state.copyState();
    set(harness.state, stateProbe, 0.0f);
    harness.state.replaceState(saved.createCopy());
    if (std::abs(harness.state.getRawParameterValue(stateProbe)->load() - stateValue) > 0.02f) return fail("state roundtrip");
    std::cout << "PASS: " << spec.id << " creative digital matrix" << std::endl;
    return 0;
}
