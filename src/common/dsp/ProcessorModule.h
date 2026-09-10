#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace amanorsac
{
struct ModulePrepareSpec
{
    double sampleRate = 44100.0;
    int maximumBlockSize = 512;
    int channelCount = 2;
};

struct ModuleProcessContext
{
    juce::AudioBuffer<float>& main;
    const juce::AudioBuffer<float>* sidechain = nullptr;
};

/** Stable standalone/rack DSP boundary.

    Parameter setters run on the control thread and must update smoothed or atomic
    storage. process() runs on the audio thread and must not allocate, lock, log,
    perform file I/O, or touch UI objects.
*/
class ProcessorModule
{
public:
    virtual ~ProcessorModule() = default;

    virtual juce::StringRef moduleId() const noexcept = 0;
    virtual void prepare(const ModulePrepareSpec&) = 0;
    virtual void reset() noexcept = 0;
    virtual void setParameter(juce::StringRef stableId, float plainValue) = 0;
    virtual void process(ModuleProcessContext&) noexcept = 0;
    virtual int latencySamples() const noexcept = 0;
};
}

