#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <cmath>

namespace amanorsac
{
class EnvelopeFollower
{
public:
    void prepare(double newSampleRate) noexcept
    {
        sampleRate = juce::jmax(1.0, newSampleRate);
        updateCoefficients();
        reset();
    }

    void setAttackRelease(float attackMilliseconds, float releaseMilliseconds) noexcept
    {
        attackMs = juce::jmax(0.01f, attackMilliseconds);
        releaseMs = juce::jmax(0.01f, releaseMilliseconds);
        updateCoefficients();
    }

    void reset() noexcept { envelope = 0.0f; }

    float processSample(float input) noexcept
    {
        const auto magnitude = std::abs(input);
        const auto coefficient = magnitude > envelope ? attackCoefficient : releaseCoefficient;
        envelope = magnitude + coefficient * (envelope - magnitude);
        return envelope;
    }

    float current() const noexcept { return envelope; }

private:
    void updateCoefficients() noexcept
    {
        const auto coefficientFor = [this](float milliseconds)
        {
            return std::exp(-1.0f / (0.001f * milliseconds * static_cast<float>(sampleRate)));
        };
        attackCoefficient = coefficientFor(attackMs);
        releaseCoefficient = coefficientFor(releaseMs);
    }

    double sampleRate = 44100.0;
    float attackMs = 10.0f;
    float releaseMs = 100.0f;
    float attackCoefficient = 0.0f;
    float releaseCoefficient = 0.0f;
    float envelope = 0.0f;
};

class DynamicsGainComputer
{
public:
    enum class Mode { compress, expandUp, expandDown };

    static float gainDecibels(float levelDb,
                              float thresholdDb,
                              float ratio,
                              float rangeDb,
                              Mode mode) noexcept
    {
        ratio = juce::jmax(0.01f, ratio);
        switch (mode)
        {
            case Mode::compress:
            {
                if (levelDb <= thresholdDb) return 0.0f;
                const auto reduction = (thresholdDb + (levelDb - thresholdDb) / ratio) - levelDb;
                return juce::jlimit(juce::jmin(0.0f, rangeDb), 0.0f, reduction);
            }
            case Mode::expandUp:
            {
                if (levelDb <= thresholdDb) return 0.0f;
                const auto boost = (levelDb - thresholdDb) * juce::jmax(0.0f, ratio - 1.0f);
                return juce::jlimit(0.0f, juce::jmax(0.0f, rangeDb), boost);
            }
            case Mode::expandDown:
            {
                if (levelDb >= thresholdDb) return 0.0f;
                const auto reduction = (thresholdDb - levelDb) * juce::jmax(0.0f, ratio - 1.0f);
                return -juce::jlimit(0.0f, std::abs(juce::jmin(0.0f, rangeDb)), reduction);
            }
        }
        return 0.0f;
    }
};
}

