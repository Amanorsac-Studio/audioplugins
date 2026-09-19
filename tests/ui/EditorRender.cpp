#include "common/audio/PluginProcessor.h"
#include "common/ui/DigitalChassis.h"

#include <cmath>

#include <juce_gui_extra/juce_gui_extra.h>

#include <iostream>

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI initialiseJuce;

    const auto destination = argc > 1
        ? juce::File(juce::String::fromUTF8(argv[1]))
        : juce::File::getCurrentWorkingDirectory().getChildFile("analog-editor-render.png");

    amanorsac::PluginProcessor processor;
    processor.setPlayConfigDetails(2, 2, 48000.0, 512);
    processor.prepareToPlay(48000.0, 512);

    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    if (editor == nullptr)
    {
        std::cerr << "Editor creation failed\n";
        return 1;
    }

    // Optional: argv[2] is the width to render at, argv[3] a factory preset to show.
    if (argc > 3)
    {
        const auto wanted = juce::String::fromUTF8(argv[3]);
        const auto& list = processor.presetManager->presets();
        for (int i = 0; i < static_cast<int>(list.size()); ++i)
            if (list[static_cast<size_t>(i)].name == wanted) processor.presetManager->load(i);
    }
    if (argc > 2)
    {
        const auto width = juce::String(argv[2]).getIntValue();
        if (width > 0) editor->setSize(width, juce::roundToInt(width * static_cast<double>(editor->getHeight()) / editor->getWidth()));
    }
    editor->resized();

    // A digital window shows live analysis, so play something through it
    // first: shaped noise with a moving level and a few tones, like music.
    if (auto* digital = dynamic_cast<amanorsac::DigitalChassis*>(editor.get()))
    {
        juce::Random random(5);
        juce::AudioBuffer<float> block(2, 512);
        juce::MidiBuffer midi;
        float pinkL = 0.0f, pinkR = 0.0f, phase = 0.0f;
        for (int frame = 0; frame < 280; ++frame)
        {
            const auto level = 0.18f + 0.12f * std::sin(static_cast<float>(frame) * 0.11f);
            for (int i = 0; i < block.getNumSamples(); ++i)
            {
                pinkL = 0.97f * pinkL + 0.2f * (random.nextFloat() * 2.0f - 1.0f);
                pinkR = 0.97f * pinkR + 0.2f * (random.nextFloat() * 2.0f - 1.0f);
                phase += 2.0f * juce::MathConstants<float>::pi / 48000.0f;
                const auto tones = 0.2f * std::sin(phase * 110.0f) + 0.08f * std::sin(phase * 1250.0f) + 0.05f * std::sin(phase * 6800.0f);
                block.setSample(0, i, level * (pinkL + tones));
                block.setSample(1, i, level * (0.8f * pinkR + 0.2f * pinkL + tones));
            }
            processor.processBlock(block, midi);
            if (frame % 3 == 2) digital->advanceDisplay();
        }
    }

    const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
    destination.getParentDirectory().createDirectory();
    juce::FileOutputStream stream(destination);
    juce::PNGImageFormat png;
    if (! stream.openedOk() || ! png.writeImageToStream(image, stream))
    {
        std::cerr << "Could not write editor render\n";
        return 2;
    }

    std::cout << destination.getFullPathName() << '\n';
    return 0;
}
