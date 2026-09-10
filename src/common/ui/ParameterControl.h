#pragma once

#include "common/state/PluginSpec.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace amanorsac
{
class FineSlider final : public juce::Slider
{
public:
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
};

class ParameterControl final : public juce::Component
{
public:
    ParameterControl(juce::AudioProcessorValueTreeState&, const ParameterDescriptor&, bool analog);
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    const ParameterDescriptor descriptor;
    const bool analogStyle;
    juce::Label label;
    FineSlider slider;
    juce::ToggleButton toggle;
    juce::ComboBox choice;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> choiceAttachment;
};
}

