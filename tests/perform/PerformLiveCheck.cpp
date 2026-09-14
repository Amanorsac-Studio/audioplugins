// PERFORM LIVE check: proves the path is zero-latency, every preset loads and
// stays finite, and mute and bypass do what they say. Pass a .png path to also
// render the editor.

#include <juce_audio_utils/juce_audio_utils.h>

#include "perform/PerformProcessor.h"

#include <cmath>
#include <iostream>

using namespace amanorsac::perform;

namespace
{
int failures = 0;

void check(bool ok, const juce::String& what)
{
    std::cout << (ok ? "  ok    " : "  FAIL  ") << what << "\n";
    if (! ok) ++failures;
}

void set(PerformProcessor& p, const char* id, float value)
{
    auto* parameter = p.state.getParameter(id);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

/** Runs `seconds` of audio in 256-sample blocks; returns the output. */
juce::AudioBuffer<float> run(PerformProcessor& p, const juce::AudioBuffer<float>& input)
{
    juce::AudioBuffer<float> out(input);
    juce::MidiBuffer midi;
    for (int start = 0; start < out.getNumSamples(); start += 256)
    {
        const auto count = juce::jmin(256, out.getNumSamples() - start);
        juce::AudioBuffer<float> block(out.getArrayOfWritePointers(), out.getNumChannels(), start, count);
        p.processBlock(block, midi);
    }
    return out;
}

juce::AudioBuffer<float> noise(double rate, double seconds, float level)
{
    juce::Random random(7);
    juce::AudioBuffer<float> b(2, static_cast<int>(rate * seconds));
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < b.getNumSamples(); ++i) b.setSample(c, i, (random.nextFloat() * 2.0f - 1.0f) * level);
    return b;
}

bool finite(const juce::AudioBuffer<float>& b, float& peak)
{
    peak = 0.0f;
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const auto s = b.getSample(c, i);
            if (! std::isfinite(s)) return false;
            peak = juce::jmax(peak, std::abs(s));
        }
    return true;
}
}

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;

    std::cout << "== PERFORM LIVE ==\n";

    for (const auto rate : { 44100.0, 48000.0, 96000.0 })
    {
        PerformProcessor p;
        p.prepareToPlay(rate, 256);
        check(p.getLatencySamples() == 0, "reports zero latency at " + juce::String(rate / 1000.0, 1) + " kHz");

        // Every module off: an impulse must leave on the same sample, untouched.
        set(p, "eq_on", 0); set(p, "comp_on", 0); set(p, "rev_on", 0); set(p, "dly_on", 0);
        p.prepareToPlay(rate, 256);
        juce::AudioBuffer<float> impulse(2, 4096);
        impulse.clear();
        impulse.setSample(0, 100, 0.5f);
        impulse.setSample(1, 100, 0.5f);
        const auto out = run(p, impulse);
        float error = 0.0f;
        for (int i = 0; i < out.getNumSamples(); ++i) error = juce::jmax(error, std::abs(out.getSample(0, i) - impulse.getSample(0, i)));
        check(error < 1.0e-6f, "all modules off passes the input through on the same sample (error " + juce::String(error, 9) + ")");
    }

    {
        // With the EQ and compressor engaged the dry transient still peaks on its own sample.
        PerformProcessor p;
        p.loadFactory(0);
        set(p, "rev_on", 0); set(p, "dly_on", 0);
        p.prepareToPlay(48000.0, 256);
        juce::AudioBuffer<float> impulse(2, 4096);
        impulse.clear();
        impulse.setSample(0, 1000, 0.05f);
        impulse.setSample(1, 1000, 0.05f);
        const auto out = run(p, impulse);
        int at = 0;
        for (int i = 0; i < out.getNumSamples(); ++i) if (std::abs(out.getSample(0, i)) > std::abs(out.getSample(0, at))) at = i;
        check(at == 1000, "EQ and compressor add no delay (peak at sample " + juce::String(at) + ", sent at 1000)");
    }

    const auto& bank = factoryPresets();
    check(bank.size() >= 15, juce::String(static_cast<int>(bank.size())) + " factory presets");
    for (int i = 0; i < static_cast<int>(bank.size()); ++i)
    {
        PerformProcessor p;
        bool idsValid = true;
        for (const auto& [id, value] : bank[static_cast<size_t>(i)].values)
            idsValid = idsValid && p.state.getParameter(id) != nullptr;
        p.loadFactory(i);
        p.prepareToPlay(48000.0, 256);
        const auto out = run(p, noise(48000.0, 3.0, 0.3f));
        float peak = 0.0f;
        const auto ok = finite(out, peak);
        check(idsValid && ok && peak < 8.0f,
              "preset \"" + bank[static_cast<size_t>(i)].name + "\" loads and stays finite (peak " + juce::String(peak, 2) + ")");
    }

    {
        PerformProcessor p;
        p.prepareToPlay(48000.0, 256);
        set(p, "out_mute", 1);
        auto out = run(p, noise(48000.0, 0.5, 0.5f));
        float tail = 0.0f;
        for (int i = out.getNumSamples() - 4800; i < out.getNumSamples(); ++i) tail = juce::jmax(tail, std::abs(out.getSample(0, i)));
        check(tail == 0.0f, "mute silences the output");
    }

    {
        PerformProcessor p;
        p.prepareToPlay(48000.0, 256);
        juce::AudioBuffer<float> silence(2, 48000);
        silence.clear();
        const auto out = run(p, silence);
        float peak = 0.0f;
        finite(out, peak);
        check(peak == 0.0f, "silence in gives silence out");
    }

    {
        // Tempo-synced 1/4 at 120 BPM with nothing else: echo arrives 24000 samples later.
        PerformProcessor p;
        set(p, "eq_on", 0); set(p, "comp_on", 0); set(p, "rev_on", 0);
        set(p, "dly_div", 2); set(p, "dly_mix", 100); set(p, "dly_feedback", 0); set(p, "dly_pingpong", 0); set(p, "dly_filter", 20000);
        p.prepareToPlay(48000.0, 256);
        juce::AudioBuffer<float> impulse(2, 30000);
        impulse.clear();
        impulse.setSample(0, 10, 1.0f);
        const auto out = run(p, impulse);
        int echoAt = 0;
        for (int i = 100; i < out.getNumSamples(); ++i) if (std::abs(out.getSample(0, i)) > std::abs(out.getSample(0, echoAt))) echoAt = i;
        check(std::abs(echoAt - 24010) <= 2, "a 1/4 echo at 120 BPM lands on the beat (sample " + juce::String(echoAt) + ")");
    }

    {
        PerformProcessor p;
        p.loadFactory(3);
        juce::MemoryBlock saved;
        p.getStateInformation(saved);
        PerformProcessor q;
        q.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
        check(q.presetName() == p.presetName()
                  && std::abs(q.state.getRawParameterValue("comp_ratio")->load() - p.state.getRawParameterValue("comp_ratio")->load()) < 1e-4f,
              "session save and reload restores the settings and preset name");
    }

    if (argc > 1)
    {
        PerformProcessor p;
        p.prepareToPlay(48000.0, 256);
        run(p, noise(48000.0, 0.5, 0.4f));
        std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
        editor->setSize(1536, 1024);
        const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
        const juce::File file { juce::File::getCurrentWorkingDirectory().getChildFile(juce::String(argv[1])) };
        file.deleteFile();
        juce::FileOutputStream stream(file);
        juce::PNGImageFormat().writeImageToStream(image, stream);
        std::cout << "rendered " << file.getFullPathName() << "\n";
    }

    std::cout << (failures == 0 ? "PASS" : "FAIL") << ": PERFORM LIVE check, " << failures << " failure(s)\n";
    return failures == 0 ? 0 : 1;
}
