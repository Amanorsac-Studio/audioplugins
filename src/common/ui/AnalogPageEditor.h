#pragma once

#include "common/ui/ProductHost.h"

#include <juce_gui_extra/juce_gui_extra.h>

namespace amanorsac
{
/** Shared faceplate for the A03-A10 analog products.

    The chassis, chrome and control widgets come from the analog design system;
    the layout is derived from each product's own parameter contract, so every
    automatable parameter appears exactly once and no control on screen is dead.
    Each product supplies its own hero visual and accent colour.
*/
class AnalogPageEditor final : public juce::AudioProcessorEditor
{
public:
    AnalogPageEditor(juce::AudioProcessor&, ProductHost);
    ~AnalogPageEditor() override;

    /** The faceplate on its own, for a host that supplies its own frame: the
        rack embeds this and scales it into a module bay. */
    static std::unique_ptr<juce::Component> createSurface(ProductHost);
    /** Each product's backdrop has its own size, so the caller asks. */
    static juce::Rectangle<float> surfaceSize(const juce::String& productId);

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    class Surface;
    std::unique_ptr<Surface> surface;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnalogPageEditor)
};
}
