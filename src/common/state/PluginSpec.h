#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace amanorsac
{
struct ParameterDescriptor
{
    enum class Kind { floating, integer, boolean, choice };

    juce::String id;
    juce::String name;
    juce::String unit;
    juce::String rangeText;
    juce::String defaultText;
    juce::StringArray choices;
    Kind kind = Kind::floating;
    float minimum = 0.0f;
    float maximum = 1.0f;
    float defaultValue = 0.0f;
    bool automatable = true;
};

struct PluginSpec
{
    juce::String id;
    juce::String displayName;
    juce::String series;
    juce::String primaryRole;
    juce::StringArray features;
    std::vector<ParameterDescriptor> parameters;

    [[nodiscard]] bool isDigital() const noexcept { return series.equalsIgnoreCase("Digital"); }
    [[nodiscard]] bool isAnalog() const noexcept { return series.equalsIgnoreCase("Analog"); }

    static PluginSpec fromEmbeddedJson();
    static PluginSpec fromJson(const juce::String& json);
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout(const PluginSpec&);

    /** Adds this contract to an existing layout, optionally under an ID prefix
        ("A01.") and a display prefix ("A01 "). The rack hosts every analog
        module in one parameter tree this way, so each module control is a real,
        automatable, session-saved host parameter. */
    static void appendParameters(juce::AudioProcessorValueTreeState::ParameterLayout&,
                                 const PluginSpec&, const juce::String& idPrefix = {},
                                 const juce::String& namePrefix = {});
};
}
