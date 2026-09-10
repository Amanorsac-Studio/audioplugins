#include "AnalogFrontEnd.h"

namespace amanorsac
{
void AnalogFrontEnd::configure(const PluginSpec& spec, juce::String parameterPrefix)
{
    prefix = std::move(parameterPrefix);

    auto find = [&spec](const juce::String& id) -> const ParameterDescriptor*
    {
        for (const auto& descriptor : spec.parameters)
            if (descriptor.id == id) return &descriptor;
        return nullptr;
    };

    const auto& id = spec.id;
    // A01 and A10 own these stages internally; the rest are wrapped here.
    active = spec.isAnalog() && id != "A01" && id != "A10";
    if (! active) return;

    hasInput = find("input") != nullptr && (id == "A05" || id == "A07" || id == "A08" || id == "A09");
    hasOutput = find("output") != nullptr && id == "A08";
    hasMix = find("mix") != nullptr && (id == "A02" || id == "A09");
    hasMidSide = find("stereo_mode") != nullptr && id == "A02";
    hasPhase = find("phase") != nullptr && id != "A02";   // IRON PRE flips phase in its own engine
    hasBypass = find("bypass") != nullptr;

    if (const auto* hpf = find("hpf");
        hpf != nullptr && (id == "A04" || id == "A05" || id == "A06" || id == "A09"))
    {
        hasHpf = true;
        hpfIsSwitch = hpf->kind == ParameterDescriptor::Kind::boolean;
        hpfMinimum = hpf->minimum;
    }
}

void AnalogFrontEnd::prepare(double newSampleRate, int maximumBlockSize)
{
    sampleRate = newSampleRate;
    rawCopy.setSize(2, juce::jmax(1, maximumBlockSize));
    bypassSmoother.reset(newSampleRate, 0.02);
    bypassSmoother.setCurrentAndTargetValue(0.0f);
    for (auto& filter : highPass)
    {
        filter.prepare({ newSampleRate, static_cast<juce::uint32>(juce::jmax(1, maximumBlockSize)), 1 });
        filter.reset();
    }
}

void AnalogFrontEnd::reset()
{
    for (auto& filter : highPass) filter.reset();
    bypassSmoother.setCurrentAndTargetValue(bypassSmoother.getTargetValue());
    rawCopy.clear();
}

float AnalogFrontEnd::value(const juce::AudioProcessorValueTreeState& state, const juce::String& id,
                            float fallback) const
{
    if (const auto* parameter = state.getRawParameterValue(prefix + id))
        return parameter->load(std::memory_order_relaxed);
    return fallback;
}

void AnalogFrontEnd::processFront(juce::AudioBuffer<float>& buffer,
                                  const juce::AudioProcessorValueTreeState& state, int channels)
{
    if (! active) return;
    const auto samples = buffer.getNumSamples();
    rawCopy.setSize(2, samples, false, false, true);
    for (int channel = 0; channel < channels; ++channel)
        rawCopy.copyFrom(channel, 0, buffer, channel, 0, samples);

    frontGain = 1.0f;
    if (hasInput) frontGain *= juce::Decibels::decibelsToGain(value(state, "input"));
    if (hasPhase && value(state, "phase") > 0.5f) frontGain = -frontGain;
    if (frontGain != 1.0f)
        for (int channel = 0; channel < channels; ++channel)
            buffer.applyGain(channel, 0, samples, frontGain);

    if (hasHpf)
    {
        const auto raw = value(state, "hpf", hpfMinimum);
        // A continuous HPF reads OFF at the bottom of its travel; a switch is 80 Hz.
        const auto engaged = hpfIsSwitch ? raw > 0.5f : raw > hpfMinimum + 0.5f;
        const auto hz = hpfIsSwitch ? 80.0f : juce::jlimit(20.0f, 300.0f, raw);
        if (engaged)
        {
            auto coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate, hz);
            for (int channel = 0; channel < channels; ++channel)
            {
                auto& filter = highPass[static_cast<size_t>(channel)];
                *filter.coefficients = *coefficients;
                auto* data = buffer.getWritePointer(channel);
                for (int i = 0; i < samples; ++i) data[i] = filter.processSample(data[i]);
            }
        }
        else
            for (auto& filter : highPass) filter.reset();
    }

    encoded = hasMidSide && channels == 2 && value(state, "stereo_mode") > 0.5f;
    if (encoded)
    {
        auto* left = buffer.getWritePointer(0);
        auto* right = buffer.getWritePointer(1);
        for (int i = 0; i < samples; ++i)
        {
            const auto mid = (left[i] + right[i]) * 0.5f, side = (left[i] - right[i]) * 0.5f;
            left[i] = mid; right[i] = side;
        }
    }
}

void AnalogFrontEnd::processBack(juce::AudioBuffer<float>& buffer,
                                 const juce::AudioProcessorValueTreeState& state, int channels)
{
    if (! active) return;
    const auto samples = buffer.getNumSamples();

    if (encoded)
    {
        auto* left = buffer.getWritePointer(0);
        auto* right = buffer.getWritePointer(1);
        for (int i = 0; i < samples; ++i)
        {
            const auto mid = left[i], side = right[i];
            left[i] = mid + side; right[i] = mid - side;
        }
    }

    if (hasMix)
    {
        const auto mix = juce::jlimit(0.0f, 1.0f, value(state, "mix", 100.0f) * 0.01f);
        for (int channel = 0; channel < channels; ++channel)
        {
            auto* wet = buffer.getWritePointer(channel);
            const auto* raw = rawCopy.getReadPointer(channel);
            for (int i = 0; i < samples; ++i)
            {
                // Mixed against the trimmed input, so MIX 0 % is the trim alone.
                const auto dry = raw[i] * frontGain;
                wet[i] = dry + mix * (wet[i] - dry);
            }
        }
    }

    if (hasOutput)
        for (int channel = 0; channel < channels; ++channel)
            buffer.applyGain(channel, 0, samples, juce::Decibels::decibelsToGain(value(state, "output")));

    if (hasBypass)
    {
        bypassSmoother.setTargetValue(value(state, "bypass") > 0.5f ? 1.0f : 0.0f);
        if (bypassSmoother.isSmoothing() || bypassSmoother.getTargetValue() > 0.5f)
            for (int channel = 0; channel < channels; ++channel)
            {
                auto ramp = bypassSmoother;
                auto* wet = buffer.getWritePointer(channel);
                const auto* raw = rawCopy.getReadPointer(channel);
                for (int i = 0; i < samples; ++i)
                {
                    const auto amount = ramp.getNextValue();
                    wet[i] += amount * (raw[i] - wet[i]);
                }
                if (channel == channels - 1) bypassSmoother = ramp;
            }
    }
}
}
