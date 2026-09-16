#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace amanorsac
{
class PluginProcessor;

/** The live picture at the top of every digital window.

    Equalisers and dynamics show the real spectrum before and after the
    engine, with their bands, crossovers or focus drawn over it and grabbable.
    Time effects and the imager show motion instead: echoes travelling,
    a reverb tail breathing, a vectorscope.
*/
class DigitalDisplay : public juce::Component
{
public:
    ~DigitalDisplay() override = default;

    /** The right display for this product. Never null for a digital product. */
    static std::unique_ptr<DigitalDisplay> create(PluginProcessor&);

    /** Called by the window about thirty times a second. */
    virtual void tick() = 0;

    /** The window tells the display which slot its panels are showing. */
    virtual void setSelectedSlot(const juce::String& family, int slot) { juce::ignoreUnused(family, slot); }

    /** The display asks the window to show a slot, when a node is clicked. */
    std::function<void(const juce::String& family, int slot)> onSelectSlot;
};
}
