// VOCAL RACK check: zero latency, every preset loads and stays finite, bypass
// and switched-off modules pass audio untouched, solo works, undo works, and
// state recalls. Pass a .png path to also render the window with audio playing.

#include <juce_audio_utils/juce_audio_utils.h>

#include "vocalrack/VocalRackProcessor.h"
#include "vocalrack/VocalRackEditor.h"

#include <cmath>
#include <iostream>

using namespace amanorsac::vocalrack;

namespace
{
int failures = 0;

void check(bool ok, const juce::String& what)
{
    std::cout << (ok ? "  ok    " : "  FAIL  ") << what << "\n";
    if (! ok) ++failures;
}

void set(VocalRackProcessor& p, const char* id, float value)
{
    auto* parameter = p.state.getParameter(id);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

juce::AudioBuffer<float> music(double rate, double seconds, float level, int seed = 3)
{
    juce::Random random(seed);
    juce::AudioBuffer<float> b(2, static_cast<int>(rate * seconds));
    float pinkL = 0.0f, pinkR = 0.0f;
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        pinkL = 0.97f * pinkL + 0.2f * (random.nextFloat() * 2.0f - 1.0f);
        pinkR = 0.97f * pinkR + 0.2f * (random.nextFloat() * 2.0f - 1.0f);
        const auto t = static_cast<float>(i / rate);
        const auto voice = 0.3f * std::sin(juce::MathConstants<float>::twoPi * 220.0f * t)
                         * (0.5f + 0.5f * std::sin(juce::MathConstants<float>::twoPi * 1.5f * t));
        b.setSample(0, i, level * (pinkL + voice));
        b.setSample(1, i, level * (pinkR + voice));
    }
    return b;
}

juce::AudioBuffer<float> run(VocalRackProcessor& p, juce::AudioBuffer<float> audio, int block = 256)
{
    juce::MidiBuffer midi;
    for (int start = 0; start < audio.getNumSamples(); start += block)
    {
        const auto count = juce::jmin(block, audio.getNumSamples() - start);
        juce::AudioBuffer<float> view(audio.getArrayOfWritePointers(), audio.getNumChannels(), start, count);
        p.processBlock(view, midi);
    }
    return audio;
}

float maxDifference(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int from = 0)
{
    float worst = 0.0f;
    for (int c = 0; c < a.getNumChannels(); ++c)
        for (int i = from; i < a.getNumSamples(); ++i)
            worst = juce::jmax(worst, std::abs(a.getSample(c, i) - b.getSample(c, i)));
    return worst;
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

void allModules(VocalRackProcessor& p, bool on)
{
    for (const auto* id : { "dyn_on", "tone_on", "space_on", "fx_on" }) set(p, id, on ? 1.0f : 0.0f);
}
}

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    std::cout << std::unitbuf << "== VOCAL RACK ==\n";

    for (const auto rate : { 44100.0, 48000.0, 96000.0 })
    {
        VocalRackProcessor p;
        p.prepareToPlay(rate, 256);
        check(p.getLatencySamples() == 0, "reports zero latency at " + juce::String(rate / 1000.0, 1) + " kHz");

        allModules(p, false);
        p.prepareToPlay(rate, 256);
        const auto input = music(rate, 0.5, 0.3f);
        const auto output = run(p, input);
        check(maxDifference(input, output) < 1.0e-6f, "every module off passes audio untouched at " + juce::String(rate / 1000.0, 1) + " kHz");
    }

    {
        VocalRackProcessor p;
        p.loadFactory(1);
        p.prepareToPlay(48000.0, 256);
        set(p, "bypass", 1.0f);
        p.prepareToPlay(48000.0, 256);
        const auto input = music(48000.0, 0.5, 0.3f);
        check(maxDifference(input, run(p, input)) < 1.0e-6f, "bypass returns the input exactly");
    }

    {
        // The dry path is immediate: with dynamics and tone engaged, a click
        // still peaks on the sample it was sent.
        VocalRackProcessor p;
        set(p, "space_on", 0.0f);
        set(p, "fx_on", 0.0f);
        set(p, "comp", 0.0f);
        set(p, "color", 0.0f);
        p.prepareToPlay(48000.0, 256);
        juce::AudioBuffer<float> click(2, 4096);
        click.clear();
        click.setSample(0, 1000, 0.05f);
        click.setSample(1, 1000, 0.05f);
        const auto out = run(p, click);
        int at = 0;
        for (int i = 0; i < out.getNumSamples(); ++i) if (std::abs(out.getSample(0, i)) > std::abs(out.getSample(0, at))) at = i;
        check(at == 1000, "processing adds no delay (peak at " + juce::String(at) + ", sent at 1000)");
    }

    {
        VocalRackProcessor p;
        p.prepareToPlay(48000.0, 256);
        juce::AudioBuffer<float> silence(2, 48000);
        silence.clear();
        float peak = 0.0f;
        finite(run(p, silence), peak);
        check(peak < 1.0e-6f, "silence in gives silence out");
    }

    {
        // Each module on its own makes a change; soloing one mutes the others.
        const auto input = music(48000.0, 1.0, 0.3f);
        for (int m = 0; m < 4; ++m)
        {
            VocalRackProcessor p;
            allModules(p, false);
            set(p, (juce::String(modulePrefix[m]) + "_on").toRawUTF8(), 1.0f);
            if (m == fx) set(p, "fx_amount", 60.0f);
            p.prepareToPlay(48000.0, 256);
            check(maxDifference(input, run(p, input)) > 1.0e-3f, juce::String(modulePrefix[m]).toUpperCase() + " changes the sound on its own");
        }

        VocalRackProcessor soloed;
        set(soloed, "fx_amount", 60.0f);
        set(soloed, "space_solo", 1.0f);
        soloed.prepareToPlay(48000.0, 256);
        VocalRackProcessor spaceOnly;
        allModules(spaceOnly, false);
        set(spaceOnly, "space_on", 1.0f);
        spaceOnly.prepareToPlay(48000.0, 256);
        check(maxDifference(run(soloed, input), run(spaceOnly, input)) < 1.0e-5f, "solo leaves only that module in the chain");
    }

    {
        // Every style of every module stays finite with its control pushed.
        const auto input = music(48000.0, 0.6, 0.5f);
        bool allFinite = true;
        float worst = 0.0f;
        const char* ids[] { "dyn_style", "tone_style", "space_style", "fx_style" };
        const int counts[] { dynamicsStyles().size(), toneStyles().size(), spaceStyles().size(), fxStyles().size() };
        for (int m = 0; m < 4; ++m)
            for (int s = 0; s < counts[m]; ++s)
            {
                VocalRackProcessor p;
                set(p, ids[m], static_cast<float>(s));
                set(p, "comp", 100.0f); set(p, "color", 200.0f); set(p, "deess", 100.0f);
                set(p, "reverb", 100.0f); set(p, "delay", 100.0f); set(p, "dly_feedback", 90.0f);
                set(p, "fx_amount", 100.0f);
                p.prepareToPlay(48000.0, 256);
                float peak = 0.0f;
                allFinite = allFinite && finite(run(p, input), peak);
                worst = juce::jmax(worst, peak);
            }
        check(allFinite && worst < 16.0f, "every style stays finite with everything pushed (peak " + juce::String(worst, 2) + ")");
    }

    {
        const auto& bank = factoryPresets();
        check(bank.size() >= 25, juce::String(static_cast<int>(bank.size())) + " factory presets");
        juce::StringArray names;
        const auto input = music(48000.0, 1.0, 0.3f);
        for (int i = 0; i < static_cast<int>(bank.size()); ++i)
        {
            VocalRackProcessor p;
            bool idsValid = true;
            for (const auto& [id, v] : bank[static_cast<size_t>(i)].values) idsValid = idsValid && p.state.getParameter(id) != nullptr;
            p.loadFactory(i);
            p.prepareToPlay(48000.0, 256);
            float peak = 0.0f;
            const auto ok = finite(run(p, input), peak);
            const auto& preset = bank[static_cast<size_t>(i)];
            const auto unique = ! names.contains(preset.name);
            names.add(preset.name);
            check(idsValid && ok && unique && peak > 0.01f && peak < 8.0f && presetCategories().contains(preset.category),
                  "\"" + preset.name + "\" (" + preset.category + ") loads and stays finite, peak " + juce::String(peak, 2));
        }
    }

    {
        VocalRackProcessor p;
        const auto before = p.state.getRawParameterValue("comp")->load();
        p.undo.beginNewTransaction();
        set(p, "comp", 90.0f);
        // The plug-in writes values into its undo history on a timer; a check
        // with no message loop flushes them the same way a save would.
        p.state.copyState();
        p.undo.beginNewTransaction();
        p.undo.undo();
        check(std::abs(p.state.getRawParameterValue("comp")->load() - before) < 0.01f, "undo restores a changed control");
        p.undo.redo();
        check(std::abs(p.state.getRawParameterValue("comp")->load() - 90.0f) < 0.01f, "redo puts it back");
    }

    {
        VocalRackProcessor p;
        p.loadFactory(5);
        set(p, "reverb", 77.0f);
        juce::MemoryBlock saved;
        p.getStateInformation(saved);
        VocalRackProcessor q;
        q.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
        check(q.presetName() == p.presetName() && std::abs(q.state.getRawParameterValue("reverb")->load() - 77.0f) < 0.01f,
              "session save and reload restores the settings and preset");
    }

    if (argc > 1)
    {
        VocalRackProcessor p;
        p.loadFactory(1);
        p.prepareToPlay(48000.0, 512);
        std::cout << "  render: creating window\n";
        std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
        std::cout << "  render: window created\n";
        editor->setSize(1536, 1024);
        auto* rack = dynamic_cast<VocalRackEditor*>(editor.get());
        auto audio = music(48000.0, 3.0, 0.35f, 9);
        juce::MidiBuffer midi;
        for (int start = 0, frame = 0; start + 512 <= audio.getNumSamples(); start += 512, ++frame)
        {
            juce::AudioBuffer<float> view(audio.getArrayOfWritePointers(), 2, start, 512);
            p.processBlock(view, midi);
            if (rack != nullptr && frame % 3 == 2) rack->advance();
        }
        std::cout << "  render: audio played\n";
        const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
        std::cout << "  render: snapshot taken\n";
        const juce::File file { juce::File::getCurrentWorkingDirectory().getChildFile(juce::String(argv[1])) };
        file.deleteFile();
        juce::FileOutputStream stream(file);
        juce::PNGImageFormat().writeImageToStream(image, stream);
        std::cout << "rendered " << file.getFullPathName() << "\n";
    }

    std::cout << (failures == 0 ? "PASS" : "FAIL") << ": VOCAL RACK check, " << failures << " failure(s)\n";
    return failures == 0 ? 0 : 1;
}
