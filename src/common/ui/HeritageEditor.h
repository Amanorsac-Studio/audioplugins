#pragma once

#include "common/ui/ProductHost.h"

#include <juce_gui_extra/juce_gui_extra.h>

namespace amanorsac
{
class HeritageEditor final : public juce::AudioProcessorEditor
{
public:
    HeritageEditor(juce::AudioProcessor&, ProductHost);
    ~HeritageEditor() override;

    /** The faceplate on its own, for a host that supplies its own frame: the
        rack embeds this and scales it into a module bay. */
    static std::unique_ptr<juce::Component> createSurface(ProductHost);
    static juce::Rectangle<float> surfaceSize();

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    class Surface;
    std::unique_ptr<Surface> surface;
    std::unique_ptr<class ActivationView> gate;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HeritageEditor)
};
}
