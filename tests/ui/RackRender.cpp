#include "rack/RackProcessor.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <iostream>

/** Headless snapshot of the rack editor, so its layout can be checked without
    opening a window and stealing focus from whatever is running. */
int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI initialiseJuce;

    const auto destination = argc > 1
        ? juce::File(juce::String::fromUTF8(argv[1]))
        : juce::File::getCurrentWorkingDirectory().getChildFile("rack-editor-render.png");

    amanorsac::RackProcessor processor;
    processor.setPlayConfigDetails(2, 2, 48000.0, 512);
    processor.prepareToPlay(48000.0, 512);

    // Show the rack as a customer would first meet it: a few modules in.
    for (const auto* id : { "slot.A01.enabled", "slot.A06.enabled", "slot.A10.enabled" })
        if (auto* parameter = processor.state.getParameter(id))
            parameter->setValueNotifyingHost(1.0f);

    // Optional third argument renders the split, parallel arrangement.
    if (argc > 3 && juce::String(argv[3]).equalsIgnoreCase("split"))
    {
        if (auto* split = processor.state.getParameter("split")) split->setValueNotifyingHost(1.0f);
        for (const auto* id : { "slot.A09.enabled", "slot.A05.enabled" })
            if (auto* parameter = processor.state.getParameter(id))
                parameter->setValueNotifyingHost(1.0f);
        processor.moveSlot(9, amanorsac::RackProcessor::Lane::b, 0);
        processor.moveSlot(4, amanorsac::RackProcessor::Lane::b, 1);
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    if (editor == nullptr)
    {
        std::cerr << "No editor" << std::endl;
        return 1;
    }

    const auto width = argc > 2 ? juce::String(argv[2]).getIntValue() : 1280;
    editor->setSize(width, juce::roundToInt(static_cast<double>(width) * editor->getHeight() / editor->getWidth()));

    const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true);
    juce::PNGImageFormat format;
    destination.deleteFile();
    if (auto stream = destination.createOutputStream(); stream != nullptr && format.writeImageToStream(image, *stream))
    {
        std::cout << destination.getFullPathName() << std::endl;
        return 0;
    }
    std::cerr << "Could not write " << destination.getFullPathName() << std::endl;
    return 1;
}
