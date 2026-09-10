#include "ParameterControl.h"
#include "Theme.h"

namespace amanorsac
{
void FineSlider::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    auto adjusted = wheel;
    if (event.mods.isShiftDown())
        adjusted.deltaY *= 0.1f;
    Slider::mouseWheelMove(event, adjusted);
}

ParameterControl::ParameterControl(juce::AudioProcessorValueTreeState& state,
                                   const ParameterDescriptor& parameter,
                                   bool analog)
    : descriptor(parameter), analogStyle(analog)
{
    label.setText(descriptor.name, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, analogStyle ? theme::cream : theme::primaryText);
    label.setFont(juce::FontOptions(descriptor.name.length() > 18 ? 10.0f : 12.0f,
                                    juce::Font::bold));
    addAndMakeVisible(label);

    if (descriptor.kind == ParameterDescriptor::Kind::boolean)
    {
        toggle.setButtonText("ON");
        toggle.setColour(juce::ToggleButton::textColourId, analogStyle ? theme::cream : theme::primaryText);
        toggle.setColour(juce::ToggleButton::tickColourId, analogStyle ? theme::brass : theme::electricBlue);
        addAndMakeVisible(toggle);
        buttonAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(state, descriptor.id, toggle);
    }
    else if (descriptor.kind == ParameterDescriptor::Kind::choice)
    {
        choice.addItemList(descriptor.choices, 1);
        choice.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(choice);
        choiceAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(state, descriptor.id, choice);
    }
    else
    {
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 90, 22);
        slider.setDoubleClickReturnValue(true, descriptor.defaultValue);
        slider.setColour(juce::Slider::rotarySliderFillColourId, analogStyle ? theme::brass : theme::electricBlue);
        slider.setColour(juce::Slider::rotarySliderOutlineColourId, analogStyle ? theme::analogBackground.brighter(0.5f) : theme::digitalBorder);
        slider.setColour(juce::Slider::thumbColourId, analogStyle ? theme::cream : theme::primaryText);
        slider.setColour(juce::Slider::textBoxTextColourId, analogStyle ? theme::cream : theme::primaryText);
        slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        slider.setTextValueSuffix(descriptor.unit.isEmpty() ? juce::String() : " " + descriptor.unit);
        addAndMakeVisible(slider);
        sliderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state, descriptor.id, slider);
    }

    setTitle(descriptor.name);
    setDescription(descriptor.rangeText + "; default " + descriptor.defaultText);
    setWantsKeyboardFocus(true);
}

void ParameterControl::paint(juce::Graphics& graphics)
{
    auto area = getLocalBounds().toFloat().reduced(2.0f);
    graphics.setColour(analogStyle ? theme::analogMetal.brighter(0.05f) : theme::digitalPanel.withAlpha(0.82f));
    graphics.fillRoundedRectangle(area, 8.0f);
    graphics.setColour(analogStyle ? theme::brass.withAlpha(0.28f) : theme::digitalBorder.withAlpha(0.8f));
    graphics.drawRoundedRectangle(area, 8.0f, 1.0f);
}

void ParameterControl::resized()
{
    auto area = getLocalBounds().reduced(8);
    label.setBounds(area.removeFromTop(22));
    if (toggle.isVisible()) toggle.setBounds(area.reduced(14, 8));
    if (choice.isVisible()) choice.setBounds(area.reduced(4, 18));
    if (slider.isVisible()) slider.setBounds(area);
}
}
