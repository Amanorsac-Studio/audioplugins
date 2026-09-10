#pragma once

#include <juce_core/juce_core.h>

namespace amanorsac::licensing
{
/** Stable identifier for the machine a licence is bound to.

    Several weak identifiers are combined and hashed rather than trusting one,
    so a licence survives ordinary changes (a new network adapter, a second
    drive) while still refusing to move to a different computer.

    The printed form is what a customer sends when buying, so it is short,
    grouped and uses an alphabet with no characters that get confused by hand.
*/
struct MachineId
{
    /** Raw 64 hex character digest. Compared inside a licence. */
    static juce::String digest();

    /** Grouped, human readable form shown in the activation window. */
    static juce::String printable();

    /** True when the printed form matches this machine, ignoring spacing
        and letter case so a customer can retype it safely. */
    static bool matchesPrintable(const juce::String& candidate);
};
}
