#pragma once

#include "VocalRackProcessor.h"

#include <juce_gui_extra/juce_gui_extra.h>

namespace amanorsac::vocalrack
{
/** The VOCAL RACK window: four module cards over four control panels, laid
    out on a fixed 1536 x 1024 canvas and scaled as one piece. */
class VocalRackEditor final : public juce::AudioProcessorEditor,
                              private juce::Timer
{
public:
    explicit VocalRackEditor(VocalRackProcessor&);
    ~VocalRackEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    /** One animation frame, for tools that render without a message loop. */
    void advance() { timerCallback(); }

private:
    void timerCallback() override;

    class Surface;
    std::unique_ptr<Surface> surface;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VocalRackEditor)
};
}
