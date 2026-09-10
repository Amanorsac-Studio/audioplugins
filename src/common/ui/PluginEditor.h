#pragma once

#include "common/audio/PluginProcessor.h"
#include "common/ui/ParameterControl.h"

#include <juce_gui_extra/juce_gui_extra.h>

namespace amanorsac
{
class PluginEditor final : public juce::AudioProcessorEditor
{
public:
    explicit PluginEditor(PluginProcessor&);
    ~PluginEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    struct ControlPage
    {
        juce::String name;
        std::vector<size_t> parameterIndices;
    };

    PluginProcessor& processor;
    juce::Image designImage;
    juce::ComboBox pageSelector;
    juce::TextButton previousPage { "<" };
    juce::TextButton nextPage { ">" };
    juce::Label inspectorTitle;
    juce::Label inspectorHint;
    juce::Component controlsContent;
    std::vector<ControlPage> pages;
    std::vector<std::unique_ptr<ParameterControl>> controls;
    int currentPage = 0;

    void buildPages();
    void rebuildPage();
    void changePage(int delta);
    [[nodiscard]] int inspectorWidth() const noexcept;
    [[nodiscard]] juce::Rectangle<int> designBounds() const noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};
}
