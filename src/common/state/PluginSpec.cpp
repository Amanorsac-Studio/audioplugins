#include "PluginSpec.h"
#include "GeneratedPluginSpec.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace amanorsac
{
namespace
{
juce::String firstLine(juce::String value)
{
    return value.upToFirstOccurrenceOf("\n", false, false).trim();
}

juce::String displayLine(juce::String value)
{
    if (value.containsChar('\n'))
        return value.fromLastOccurrenceOf("\n", false, false).trim();
    return value.trim();
}

std::vector<float> extractNumbers(const juce::String& source)
{
    std::vector<float> result;
    const auto text = source.toStdString();
    const char* cursor = text.c_str();

    while (*cursor != '\0')
    {
        const auto numericStart = std::isdigit(static_cast<unsigned char>(*cursor)) != 0
                               || ((*cursor == '-' || *cursor == '+')
                                   && std::isdigit(static_cast<unsigned char>(cursor[1])) != 0)
                               || (*cursor == '.' && std::isdigit(static_cast<unsigned char>(cursor[1])) != 0);
        if (!numericStart)
        {
            ++cursor;
            continue;
        }

        char* end = nullptr;
        const auto value = std::strtof(cursor, &end);
        if (end != cursor)
        {
            result.push_back(value);
            cursor = end;
        }
        else
        {
            ++cursor;
        }
    }

    return result;
}

bool parseBoolean(const juce::String& text)
{
    return text.trim().equalsIgnoreCase("true") || text.trim().equalsIgnoreCase("on") || text.getIntValue() != 0;
}

juce::String inferUnit(const juce::String& range)
{
    if (range.containsIgnoreCase("dB/oct")) return "dB/oct";
    if (range.containsIgnoreCase("dB")) return "dB";
    if (range.containsIgnoreCase("kHz")) return "kHz";
    if (range.containsIgnoreCase("Hz")) return "Hz";
    if (range.containsIgnoreCase("ms")) return "ms";
    if (range.containsIgnoreCase("seconds") || range.endsWithIgnoreCase(" s")) return "s";
    if (range.containsChar('%')) return "%";
    return {};
}

juce::StringArray parseChoices(const juce::String& text)
{
    auto choices = juce::StringArray::fromTokens(text, "/", "");
    choices.trim();
    choices.removeEmptyStrings();
    return choices;
}

int findChoice(const juce::StringArray& choices, const juce::String& wanted)
{
    for (int index = 0; index < choices.size(); ++index)
        if (choices[index].equalsIgnoreCase(wanted.trim()))
            return index;
    return 0;
}

juce::String indexedId(const juce::String& base, int oneBasedIndex)
{
    const auto prefix = base.upToFirstOccurrenceOf(".", false, false);
    const auto suffix = base.fromFirstOccurrenceOf(".", false, false);
    return prefix + "." + juce::String(oneBasedIndex).paddedLeft('0', 2) + "." + suffix;
}

std::vector<ParameterDescriptor> expandIndexedContracts(const PluginSpec& spec,
                                                        std::vector<ParameterDescriptor> source)
{
    const auto bandSlots = spec.id == "D01" ? 24 : spec.id == "D02" ? 12 : (spec.id == "D03" ? 6 : 0);
    const auto zoneSlots = spec.id == "D07" ? 6 : 0;
    const auto tapSlots = spec.id == "D08" ? 8 : 0;
    const auto widthBandSlots = spec.id == "D10" ? 5 : 0;
    const auto crossoverSlots = spec.id == "D03" || spec.id == "D07" ? 5
                              : spec.id == "D10" ? 4 : 0;
    if (bandSlots == 0 && zoneSlots == 0 && tapSlots == 0 && widthBandSlots == 0)
        return source;

    std::vector<ParameterDescriptor> expanded;
    expanded.reserve(source.size() * static_cast<size_t>(bandSlots));
    for (const auto& descriptor : source)
    {
        const auto isBand = descriptor.id.startsWith("band.");
        const auto isZone = descriptor.id.startsWith("zone.");
        const auto isTap = descriptor.id.startsWith("tap.");
        const auto isCrossover = descriptor.id.startsWith("xover.");
        const auto slotCount = isBand ? (bandSlots > 0 ? bandSlots : widthBandSlots)
                             : isZone ? zoneSlots : isTap ? tapSlots
                             : isCrossover ? crossoverSlots : 0;
        if (slotCount == 0)
        {
            expanded.push_back(descriptor);
            continue;
        }

        for (int slot = 1; slot <= slotCount; ++slot)
        {
            auto indexed = descriptor;
            indexed.id = indexedId(descriptor.id, slot);
            indexed.name = (isBand ? "Band " : isZone ? "Zone " : isTap ? "Tap " : "Crossover ")
                         + juce::String(slot).paddedLeft('0', 2) + " " + descriptor.name;
            // PRISM exposes 24 bands, but the approved design starts with six active nodes.
            // Leaving eighteen identity filters enabled wasted substantial CPU in Debug builds.
            if (spec.id == "D01" && isBand && slot > 6 && descriptor.id.endsWith(".enabled"))
                indexed.defaultValue = 0.0f;
            if (spec.id == "D01" && isBand && slot <= 6 && descriptor.id.endsWith(".frequency"))
            {
                constexpr float startupFrequencies[] { 80.0f, 200.0f, 600.0f, 1500.0f, 4500.0f, 12000.0f };
                indexed.defaultValue = startupFrequencies[slot - 1];
                indexed.defaultText = juce::String(indexed.defaultValue, 0) + " Hz";
            }
            // Slots must not all start on top of each other: spread them so a
            // fresh instance shows, and sounds like, a sensible layout.
            if (spec.id == "D02" && isBand && descriptor.id.endsWith(".frequency"))
            {
                constexpr float spread[] { 120.0f, 450.0f, 2000.0f, 7000.0f, 60.0f, 250.0f,
                                           800.0f, 1200.0f, 3200.0f, 5000.0f, 10000.0f, 14000.0f };
                indexed.defaultValue = spread[slot - 1];
            }
            if (isCrossover)
            {
                constexpr float d03[] { 120.0f, 500.0f, 2000.0f, 6000.0f, 12000.0f };
                constexpr float d07[] { 150.0f, 600.0f, 2000.0f, 5000.0f, 10000.0f };
                constexpr float d10[] { 150.0f, 800.0f, 3000.0f, 8000.0f };
                if (spec.id == "D03") indexed.defaultValue = d03[slot - 1];
                if (spec.id == "D07") indexed.defaultValue = d07[slot - 1];
                if (spec.id == "D10") indexed.defaultValue = d10[slot - 1];
            }
            // One tap sounds by default; the others wait, spaced in time.
            if (spec.id == "D08" && isTap && descriptor.id.endsWith(".time"))
                indexed.defaultValue = 125.0f * static_cast<float>(slot);
            if (spec.id == "D08" && isTap && descriptor.id.endsWith(".enabled") && slot > 1)
                indexed.defaultValue = 0.0f;
            if (spec.id == "D01" && isBand && slot <= 6 && descriptor.id.endsWith(".gain"))
            {
                constexpr float startupGains[] { -1.5f, 1.0f, 2.5f, 5.0f, 2.5f, 0.5f };
                indexed.defaultValue = startupGains[slot - 1];
                indexed.defaultText = juce::String(indexed.defaultValue, 1) + " dB";
            }
            expanded.push_back(std::move(indexed));
        }
    }
    return expanded;
}
}

PluginSpec PluginSpec::fromEmbeddedJson()
{
    return fromJson(juce::String::fromUTF8(generated::json));
}

PluginSpec PluginSpec::fromJson(const juce::String& json)
{
    PluginSpec spec;
    const auto root = juce::JSON::parse(json);
    auto* object = root.getDynamicObject();
    jassert(object != nullptr);
    if (object == nullptr)
        return spec;

    spec.id = object->getProperty("plugin_id").toString();
    spec.displayName = object->getProperty("display_name").toString();
    spec.series = object->getProperty("series").toString();
    spec.primaryRole = object->getProperty("primary_role").toString();

    if (const auto* features = object->getProperty("feature_set").getArray())
        for (const auto& feature : *features)
            spec.features.add(feature.toString());

    if (const auto* parameters = object->getProperty("parameters").getArray())
    {
        spec.parameters.reserve(static_cast<size_t>(parameters->size()));
        for (const auto& value : *parameters)
        {
            auto* parameter = value.getDynamicObject();
            if (parameter == nullptr)
                continue;

            ParameterDescriptor descriptor;
            descriptor.id = firstLine(parameter->getProperty("id").toString());
            descriptor.name = displayLine(parameter->getProperty("label").toString());
            descriptor.rangeText = parameter->getProperty("range_or_values").toString();
            descriptor.defaultText = parameter->getProperty("default").toString();
            descriptor.automatable = static_cast<bool>(parameter->getProperty("automatable"));
            descriptor.unit = inferUnit(descriptor.rangeText);

            const auto type = parameter->getProperty("type").toString();
            if (type == "bool")
            {
                descriptor.kind = ParameterDescriptor::Kind::boolean;
                descriptor.defaultValue = parseBoolean(descriptor.defaultText) ? 1.0f : 0.0f;
            }
            else if (type == "enum")
            {
                descriptor.kind = ParameterDescriptor::Kind::choice;
                descriptor.choices = parseChoices(descriptor.rangeText);
                descriptor.minimum = 0.0f;
                descriptor.maximum = static_cast<float>(juce::jmax(0, descriptor.choices.size() - 1));
                descriptor.defaultValue = static_cast<float>(findChoice(descriptor.choices, descriptor.defaultText));
            }
            else
            {
                descriptor.kind = type == "int" ? ParameterDescriptor::Kind::integer
                                                  : ParameterDescriptor::Kind::floating;
                const auto rangeNumbers = extractNumbers(descriptor.rangeText);
                const auto defaultNumbers = extractNumbers(descriptor.defaultText);
                if (rangeNumbers.size() >= 2)
                {
                    descriptor.minimum = rangeNumbers[0];
                    descriptor.maximum = rangeNumbers[1];
                }
                descriptor.defaultValue = defaultNumbers.empty() ? descriptor.minimum : defaultNumbers.front();
            }

            if (descriptor.id.isNotEmpty())
                spec.parameters.push_back(std::move(descriptor));
        }
    }

    spec.parameters = expandIndexedContracts(spec, std::move(spec.parameters));
    return spec;
}

juce::AudioProcessorValueTreeState::ParameterLayout PluginSpec::createParameterLayout(const PluginSpec& spec)
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    appendParameters(layout, spec);
    return layout;
}

void PluginSpec::appendParameters(juce::AudioProcessorValueTreeState::ParameterLayout& layout,
                                  const PluginSpec& spec, const juce::String& idPrefix,
                                  const juce::String& namePrefix)
{
    for (const auto& descriptor : spec.parameters)
    {
        auto parameter = descriptor;
        parameter.id = idPrefix + descriptor.id;
        parameter.name = namePrefix + descriptor.name;
        const juce::ParameterID id { parameter.id, 1 };
        switch (parameter.kind)
        {
            case ParameterDescriptor::Kind::boolean:
                layout.add(std::make_unique<juce::AudioParameterBool>(
                    id, parameter.name, parameter.defaultValue > 0.5f,
                    juce::AudioParameterBoolAttributes().withAutomatable(parameter.automatable)));
                break;

            case ParameterDescriptor::Kind::choice:
                layout.add(std::make_unique<juce::AudioParameterChoice>(
                    id, parameter.name, parameter.choices, static_cast<int>(parameter.defaultValue),
                    juce::AudioParameterChoiceAttributes().withAutomatable(parameter.automatable)));
                break;

            case ParameterDescriptor::Kind::integer:
                layout.add(std::make_unique<juce::AudioParameterInt>(
                    id, parameter.name,
                    static_cast<int>(std::lround(parameter.minimum)),
                    static_cast<int>(std::lround(parameter.maximum)),
                    static_cast<int>(std::lround(parameter.defaultValue)),
                    juce::AudioParameterIntAttributes().withLabel(parameter.unit)
                                                       .withAutomatable(parameter.automatable)));
                break;

            case ParameterDescriptor::Kind::floating:
            {
                auto interval = 0.01f;
                if (parameter.maximum - parameter.minimum > 1000.0f) interval = 0.1f;
                if (parameter.maximum - parameter.minimum <= 2.0f) interval = 0.001f;
                juce::NormalisableRange<float> range(parameter.minimum, parameter.maximum, interval);
                if ((parameter.unit == "Hz" || parameter.unit == "ms" || parameter.unit == "s")
                    && parameter.minimum > 0.0f && parameter.maximum / parameter.minimum > 10.0f)
                    range.setSkewForCentre(std::sqrt(parameter.minimum * parameter.maximum));

                layout.add(std::make_unique<juce::AudioParameterFloat>(
                    id, parameter.name, range,
                    juce::jlimit(parameter.minimum, parameter.maximum, parameter.defaultValue),
                    juce::AudioParameterFloatAttributes().withLabel(parameter.unit)
                                                         .withAutomatable(parameter.automatable)));
                break;
            }
        }
    }
}
}
