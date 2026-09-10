#include "common/dsp/DynamicsPrimitives.h"

#include <cmath>
#include <iostream>

namespace
{
int fail(const char* message)
{
    std::cerr << "FAIL: " << message << std::endl;
    return 1;
}
}

int main()
{
    for (const auto sampleRate : { 44100.0, 96000.0, 192000.0 })
    {
        amanorsac::EnvelopeFollower follower;
        follower.prepare(sampleRate);
        follower.setAttackRelease(10.0f, 100.0f);

        auto previous = 0.0f;
        const auto attackSamples = static_cast<int>(sampleRate * 0.03);
        for (int sample = 0; sample < attackSamples; ++sample)
        {
            const auto current = follower.processSample(1.0f);
            if (!std::isfinite(current) || current + 1.0e-7f < previous)
                return fail("Envelope attack is non-finite or non-monotonic");
            previous = current;
        }
        if (previous < 0.9f || previous > 1.0001f)
            return fail("Envelope attack did not converge into the expected range");

        previous = follower.current();
        const auto releaseSamples = static_cast<int>(sampleRate * 0.3);
        for (int sample = 0; sample < releaseSamples; ++sample)
        {
            const auto current = follower.processSample(0.0f);
            if (!std::isfinite(current) || current > previous + 1.0e-7f)
                return fail("Envelope release is non-finite or non-monotonic");
            previous = current;
        }
        if (previous > 0.06f)
            return fail("Envelope release did not decay into the expected range");
    }

    using Computer = amanorsac::DynamicsGainComputer;
    const auto compressed = Computer::gainDecibels(-6.0f, -18.0f, 4.0f, -12.0f, Computer::Mode::compress);
    const auto belowThreshold = Computer::gainDecibels(-30.0f, -18.0f, 4.0f, -12.0f, Computer::Mode::compress);
    const auto upward = Computer::gainDecibels(-6.0f, -18.0f, 2.0f, 9.0f, Computer::Mode::expandUp);
    const auto downward = Computer::gainDecibels(-36.0f, -18.0f, 2.0f, -12.0f, Computer::Mode::expandDown);

    if (!(compressed < 0.0f && compressed >= -12.0f)) return fail("Compression gain is out of range");
    if (std::abs(belowThreshold) > 1.0e-7f) return fail("Compression acted below threshold");
    if (!(upward > 0.0f && upward <= 9.0f)) return fail("Upward expansion gain is out of range");
    if (!(downward < 0.0f && downward >= -12.0f)) return fail("Downward expansion gain is out of range");

    std::cout << "PASS: dynamics envelope and gain computer" << std::endl;
    return 0;
}

