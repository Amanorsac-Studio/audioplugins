#pragma once

#include "common/audio/PluginProcessor.h"

#include <juce_gui_extra/juce_gui_extra.h>

namespace amanorsac
{
/** Analog faceplate editor built on the approved v2 raster package.

    The approved artwork is drawn as the background exactly as delivered, with a
    transparent aperture at every control. Live controls are placed into those
    apertures using the shipped geometry, so the chrome is the approved pixels
    and only the moving parts are rendered by JUCE.

    Knobs reuse the photographed sprite for their body and rotate it around the
    measured pictured pose. Meters and numeric readouts are drawn live because a
    single flattened raster cannot supply their moving states.
*/
class FaceplateEditor final : public juce::AudioProcessorEditor
{
public:
    explicit FaceplateEditor(PluginProcessor&);
    ~FaceplateEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    /** True when the product ships a faceplate and geometry in the binary data. */
    static bool isAvailableFor(const juce::String& pluginId);

private:
    class Surface;
    std::unique_ptr<Surface> surface;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FaceplateEditor)
};
}
