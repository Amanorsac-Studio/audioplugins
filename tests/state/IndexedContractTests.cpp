#include "common/state/PluginSpec.h"

#include <set>
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
    const auto spec = amanorsac::PluginSpec::fromEmbeddedJson();
    const auto expectedCount = spec.id == "D02" ? 127 : (spec.id == "D03" ? 69 : 0);
    if (expectedCount == 0)
        return fail("Unexpected indexed contract " + spec.id);
    if (static_cast<int>(spec.parameters.size()) != expectedCount)
        return fail("Expected " + juce::String(expectedCount) + " parameters, found "
                    + juce::String(spec.parameters.size()));

    std::set<std::string> ids;
    for (const auto& parameter : spec.parameters)
        if (!ids.insert(parameter.id.toStdString()).second)
            return fail("Duplicate parameter ID " + parameter.id);

    const auto requiredBand = spec.id == "D02" ? "band.12.frequency" : "band.06.threshold";
    if (!ids.contains(requiredBand))
        return fail("Missing final indexed band ID " + juce::String(requiredBand));
    if (spec.id == "D03" && !ids.contains("xover.05.frequency"))
        return fail("Missing final indexed crossover ID");
    if (ids.contains("band.frequency") || ids.contains("band.threshold") || ids.contains("xover.frequency"))
        return fail("Unindexed template IDs leaked into the public parameter layout");

    auto layout = amanorsac::PluginSpec::createParameterLayout(spec);
    juce::ignoreUnused(layout);

    std::cout << "PASS: " << spec.id << " indexed contract has " << expectedCount
              << " unique public IDs" << std::endl;
    return 0;
}
