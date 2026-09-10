#pragma once

#include "common/audio/PluginProcessor.h"

#include <juce_gui_extra/juce_gui_extra.h>

namespace amanorsac
{
class AnalogProductEditor final : public juce::AudioProcessorEditor
{
public:
    explicit AnalogProductEditor(PluginProcessor&);
    ~AnalogProductEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    class Surface;
    std::unique_ptr<Surface> surface;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnalogProductEditor)
};
}
