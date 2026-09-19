#pragma once

#include "common/state/PluginSpec.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

namespace amanorsac
{
class PluginProcessor;

/** The window every digital product wears: the PERFORM LIVE language applied
    to the machine contract. Panels, their colours and the order of controls
    come from a section map per product, so a product only ever shows controls
    its engine really implements, grouped the way an engineer thinks about it.

    Laid out on a fixed 1536 x 1024 canvas and scaled as one piece, so the
    proportions hold at any window size.
*/
class DigitalChassis final : public juce::AudioProcessorEditor,
                             private juce::Timer
{
public:
    explicit DigitalChassis(PluginProcessor&);
    ~DigitalChassis() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    /** One display frame, for tools that render without a message loop. */
    void advanceDisplay() { timerCallback(); }

private:
    void timerCallback() override;

    class Surface;
    std::unique_ptr<Surface> surface;
    std::unique_ptr<class ActivationView> gate;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DigitalChassis)
};
}
