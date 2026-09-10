#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace amanorsac
{
class SpectrumGraph final : public juce::Component
{
public:
    explicit SpectrumGraph(juce::AudioProcessorValueTreeState&, juce::String bandPrefix = "band");
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    juce::AudioProcessorValueTreeState& state;
    const juce::String prefix;
    const juce::String verticalParameter;
    bool dragging = false;

    juce::Point<float> nodePosition() const;
    void setFromPosition(juce::Point<float>);
    void setParameter(const juce::String&, float);
    float parameter(const juce::String&, float) const;
    juce::String bandId(const juce::String& suffix) const;
    void resetParameter(const juce::String&);
    void showContextMenu();
};
}
