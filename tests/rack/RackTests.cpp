// Contract test for the rack: what it exposes, and the analog-only invariant.
// Signal behaviour and per-module fidelity are covered by the analog engine
// audit (tests/dsp/AnalogEngineAudit.cpp).

#include "rack/RackProcessor.h"

#include <cmath>
#include <iostream>

namespace
{
int fail(const juce::String& message)
{
    std::cerr << "FAIL: " << message << std::endl;
    return 1;
}
}

int main()
{
    amanorsac::RackProcessor rack;
    if (rack.spec.id != "R01") return fail("Rack contract identity mismatch");

    // The rack exposes its own controls plus every module's, all automatable.
    auto moduleParameters = 0;
    for (int slot = 0; slot < amanorsac::RackProcessor::moduleCount; ++slot)
    {
        const auto& module = rack.moduleSpec(slot);
        if (! module.isAnalog()) return fail("Non-analog module in rack slot " + juce::String(slot + 1));
        if (module.id.startsWithIgnoreCase("D")) return fail("Digital module exposed in rack");

        const auto prefix = amanorsac::RackProcessor::modulePrefix(slot);
        for (const auto& descriptor : module.parameters)
        {
            if (rack.state.getParameter(prefix + descriptor.id) == nullptr)
                return fail("Rack is missing module parameter " + prefix + descriptor.id);
            ++moduleParameters;
        }
        if (rack.state.getParameter("slot." + module.id + ".enabled") == nullptr)
            return fail("Rack is missing the enable for " + module.displayName);
    }
    if (moduleParameters < 150)
        return fail("Rack exposes only " + juce::String(moduleParameters) + " module parameters");

    for (const auto* id : { "input", "output", "mix", "bypass",
                            "split", "lane_a_level", "lane_b_level", "lane_b_phase" })
        if (rack.state.getParameter(id) == nullptr)
            return fail("Rack is missing its own " + juce::String(id) + " control");

    // Each slot carries its own routing: switched in, soloed, position, lane.
    for (int slot = 0; slot < amanorsac::RackProcessor::moduleCount; ++slot)
    {
        const auto prefix = "slot." + rack.moduleSpec(slot).id + ".";
        for (const auto* suffix : { "enabled", "solo", "order", "lane" })
            if (rack.state.getParameter(prefix + suffix) == nullptr)
                return fail("Rack is missing " + prefix + suffix);
    }

    // A module setting must survive a session save and reload. The target is
    // taken from the parameter itself so the check never depends on a range.
    auto* drive = rack.state.getParameter("A05.drive");
    if (drive == nullptr) return fail("Rack is missing A05.drive");
    drive->setValueNotifyingHost(0.62f);
    const auto* raw = rack.state.getRawParameterValue("A05.drive");
    if (raw == nullptr) return fail("Rack is missing the A05.drive value");
    const auto expected = raw->load();

    juce::MemoryBlock saved;
    rack.getStateInformation(saved);
    drive->setValueNotifyingHost(0.0f);
    rack.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    if (std::abs(raw->load() - expected) > juce::jmax(0.01f, std::abs(expected) * 0.001f))
        return fail("Rack state round-trip lost a module setting");

    // An enabled module must reach the audio.
    if (auto* enable = rack.state.getParameter("slot.A05.enabled")) enable->setValueNotifyingHost(1.0f);
    rack.setPlayConfigDetails(2, 2, 48000.0, 256);
    rack.prepareToPlay(48000.0, 256);

    juce::MidiBuffer midi;
    double phase = 0.0, difference = 0.0;
    for (int block = 0; block < 24; ++block)
    {
        juce::AudioBuffer<float> buffer(2, 256);
        for (int i = 0; i < 256; ++i)
        {
            const auto value = static_cast<float>(0.3 * std::sin(phase));
            phase += juce::MathConstants<double>::twoPi * 997.0 / 48000.0;
            buffer.setSample(0, i, value);
            buffer.setSample(1, i, value);
        }
        juce::AudioBuffer<float> dry;
        dry.makeCopyOf(buffer);
        rack.processBlock(buffer, midi);
        for (int i = 0; i < 256; ++i)
        {
            if (! std::isfinite(buffer.getSample(0, i))) return fail("Non-finite rack output");
            const auto delta = buffer.getSample(0, i) - dry.getSample(0, i);
            difference += delta * delta;
        }
    }
    if (difference < 1.0e-7) return fail("Enabled module made no change to the rack output");

    std::cout << "PASS: R01 analog-only rack, " << moduleParameters
              << " module parameters exposed" << std::endl;
    return 0;
}
