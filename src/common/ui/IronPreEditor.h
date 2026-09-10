#pragma once

#include "common/audio/PluginProcessor.h"

#include <juce_gui_extra/juce_gui_extra.h>

namespace amanorsac
{
/** A02 IRON PRE faceplate, built to the approved high-fidelity anchor.

    The whole product is laid out in the shared 1536x960 analog design space and
    scaled to the editor size, so the proportions match the anchor at any
    window size or display scale.
*/
class IronPreEditor final : public juce::AudioProcessorEditor
{
public:
    explicit IronPreEditor(PluginProcessor&);
    ~IronPreEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    class Surface;
    std::unique_ptr<Surface> surface;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IronPreEditor)
};
}
