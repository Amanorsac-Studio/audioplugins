#include "common/audio/PluginProcessor.h"

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

    editor->resized();
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
