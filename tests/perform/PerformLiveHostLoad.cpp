// Loads the shipped PERFORM LIVE.vst3 the way a host does, through the VST3
// SDK, not by linking the code. Proves the bundle scans, instantiates,
// reports zero latency, processes audio and opens its window.

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <iostream>

namespace
{
int failures = 0;

void check(bool ok, const juce::String& what)
{
    std::cout << (ok ? "  ok    " : "  FAIL  ") << what << "\n";
    if (! ok) ++failures;
}
}

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    std::cout << "== PERFORM LIVE host load ==\n";

    if (argc < 2) { std::cout << "usage: PerformLiveHostLoad <path to .vst3>\n"; return 2; }
    const juce::File bundle { juce::String(juce::CharPointer_UTF8(argv[1])) };
    const auto expectedName = argc > 2 ? juce::String(juce::CharPointer_UTF8(argv[2])) : juce::String("PERFORM LIVE");
    const auto expectedPresets = argc > 3 ? juce::String(argv[3]).getIntValue() : 20;
    check(bundle.exists(), "bundle exists at " + bundle.getFullPathName());

    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile(found, bundle.getFullPathName());
    check(found.size() == 1, "host scan finds one plug-in (" + juce::String(found.size()) + ")");
    if (found.isEmpty()) return 1;

    const auto& description = *found[0];
    check(description.name == expectedName, "name reads \"" + description.name + "\"");
    check(description.manufacturerName == "Amanorsac Studio", "maker reads \"" + description.manufacturerName + "\"");
    check(! description.isInstrument, "registers as an effect");

    for (const auto rate : { 44100.0, 48000.0 })
    {
        juce::String error;
        auto plugin = format.createInstanceFromDescription(description, rate, 256, error);
        check(plugin != nullptr, "instantiates at " + juce::String(rate / 1000.0, 1) + " kHz" + (error.isEmpty() ? "" : " (" + error + ")"));
        if (plugin == nullptr) continue;

        juce::AudioProcessor::BusesLayout stereo;
        stereo.inputBuses.add(juce::AudioChannelSet::stereo());
        stereo.outputBuses.add(juce::AudioChannelSet::stereo());
        check(plugin->setBusesLayout(stereo), "accepts stereo in and out");

        plugin->prepareToPlay(rate, 256);
        check(plugin->getLatencySamples() == 0, "reports zero latency to the host");
        check(plugin->getNumPrograms() == expectedPresets, "exposes " + juce::String(plugin->getNumPrograms()) + " presets to the host");
        check(plugin->getParameters().size() >= 28, "exposes " + juce::String(plugin->getParameters().size()) + " automatable parameters");

        juce::Random random(3);
        juce::AudioBuffer<float> buffer(2, 256);
        juce::MidiBuffer midi;
        bool finite = true;
        float peak = 0.0f;
        for (int block = 0; block < static_cast<int>(rate * 2.0 / 256.0); ++block)
        {
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 256; ++i) buffer.setSample(c, i, (random.nextFloat() * 2.0f - 1.0f) * 0.3f);
            plugin->processBlock(buffer, midi);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 256; ++i)
                {
                    finite = finite && std::isfinite(buffer.getSample(c, i));
                    peak = juce::jmax(peak, std::abs(buffer.getSample(c, i)));
                }
        }
        check(finite && peak > 0.01f && peak < 4.0f, "processes two seconds of audio cleanly (peak " + juce::String(peak, 2) + ")");

        const auto target = juce::jmin(3, plugin->getNumPrograms() - 1);
        plugin->setCurrentProgram(target);
        check(plugin->getCurrentProgram() == target, "host can switch presets (now \"" + plugin->getProgramName(plugin->getCurrentProgram()) + "\")");

        juce::MemoryBlock state;
        plugin->getStateInformation(state);
        plugin->setCurrentProgram(0);
        plugin->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        check(state.getSize() > 100, "saves and restores its session state");

        if (rate == 48000.0)
        {
            std::unique_ptr<juce::AudioProcessorEditor> editor(plugin->createEditorIfNeeded());
            check(editor != nullptr && editor->getWidth() > 0, "opens its window inside the host ("
                  + (editor != nullptr ? juce::String(editor->getWidth()) + " x " + juce::String(editor->getHeight()) : juce::String("none")) + ")");
            if (editor != nullptr) plugin->editorBeingDeleted(editor.get());
        }
        plugin->releaseResources();
    }

    std::cout << (failures == 0 ? "PASS" : "FAIL") << ": host load, " << failures << " failure(s)\n";
    return failures == 0 ? 0 : 1;
}
