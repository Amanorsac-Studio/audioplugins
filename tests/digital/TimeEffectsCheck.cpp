// Measures ORBIT DELAY and SPACEVERB PRO instead of trusting them: echo timing
// to the sample, feedback ratio, tempo sync, click-free modulation and time
// changes, freeze, the real decay time against the DECAY control, stereo
// decorrelation and level. Built once per product; it runs the tests that
// belong to the product it was built for.

#include <juce_audio_utils/juce_audio_utils.h>

#include "common/audio/PluginProcessor.h"

#include <algorithm>
#include <cmath>
#include <iostream>

using namespace amanorsac;

namespace
{
int failures = 0;
constexpr double rate = 48000.0;

void check(bool ok, const juce::String& what)
{
    std::cout << (ok ? "  ok    " : "  FAIL  ") << what << "\n";
    if (! ok) ++failures;
}

void set(PluginProcessor& p, const juce::String& id, float value)
{
    if (auto* parameter = p.state.getParameter(id))
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
    else
    {
        std::cout << "  FAIL  no such control: " << id << "\n";
        ++failures;
    }
}

std::unique_ptr<PluginProcessor> fresh()
{
    auto p = std::make_unique<PluginProcessor>();
    p->setPlayConfigDetails(2, 2, rate, 256);
    return p;
}

juce::AudioBuffer<float> run(PluginProcessor& p, juce::AudioBuffer<float> audio, int block = 256)
{
    juce::MidiBuffer midi;
    for (int start = 0; start < audio.getNumSamples(); start += block)
    {
        const auto count = juce::jmin(block, audio.getNumSamples() - start);
        juce::AudioBuffer<float> view(audio.getArrayOfWritePointers(), audio.getNumChannels(), start, count);
        p.processBlock(view, midi);
    }
    return audio;
}

juce::AudioBuffer<float> impulse(double seconds, int at = 100, float level = 0.8f)
{
    juce::AudioBuffer<float> b(2, static_cast<int>(rate * seconds));
    b.clear();
    b.setSample(0, at, level);
    b.setSample(1, at, level);
    return b;
}

juce::AudioBuffer<float> sine(double seconds, float frequency, float level)
{
    juce::AudioBuffer<float> b(2, static_cast<int>(rate * seconds));
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const auto s = level * static_cast<float>(std::sin(juce::MathConstants<double>::twoPi * frequency * i / rate));
        b.setSample(0, i, s);
        b.setSample(1, i, s);
    }
    return b;
}

juce::AudioBuffer<float> noise(double seconds, float level, bool mono)
{
    juce::Random random(21);
    juce::AudioBuffer<float> b(2, static_cast<int>(rate * seconds));
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const auto l = (random.nextFloat() * 2.0f - 1.0f) * level;
        const auto r = mono ? l : (random.nextFloat() * 2.0f - 1.0f) * level;
        b.setSample(0, i, l);
        b.setSample(1, i, r);
    }
    return b;
}

int peakIndex(const juce::AudioBuffer<float>& b, int from, int to)
{
    int at = from;
    for (int i = from; i < juce::jmin(to, b.getNumSamples()); ++i)
        if (std::abs(b.getSample(0, i)) > std::abs(b.getSample(0, at))) at = i;
    return at;
}

float peakIn(const juce::AudioBuffer<float>& b, int from, int to, int channel = 0)
{
    float peak = 0.0f;
    for (int i = juce::jmax(0, from); i < juce::jmin(to, b.getNumSamples()); ++i)
        peak = juce::jmax(peak, std::abs(b.getSample(channel, i)));
    return peak;
}

double rms(const juce::AudioBuffer<float>& b, int from, int to, int channel = 0)
{
    double sum = 0.0;
    int n = 0;
    for (int i = juce::jmax(0, from); i < juce::jmin(to, b.getNumSamples()); ++i, ++n)
        sum += static_cast<double>(b.getSample(channel, i)) * b.getSample(channel, i);
    return n > 0 ? std::sqrt(sum / n) : 0.0;
}

/** The largest second difference: a click shows as a spike here, while a
    smooth tone stays near level * (2 pi f / rate)^2. */
float roughness(const juce::AudioBuffer<float>& b, int from, int to)
{
    float worst = 0.0f;
    for (int i = juce::jmax(2, from); i < juce::jmin(to, b.getNumSamples()); ++i)
        worst = juce::jmax(worst, std::abs(b.getSample(0, i) - 2.0f * b.getSample(0, i - 1) + b.getSample(0, i - 2)));
    return worst;
}

/** How far the single worst second difference stands above the sustained
    level. A pitch bend raises the whole curve and scores near 1; a click is
    one outlier and scores many times higher. */
float spikiness(const juce::AudioBuffer<float>& b, int from, int to)
{
    std::vector<float> values;
    for (int i = juce::jmax(2, from); i < juce::jmin(to, b.getNumSamples()); ++i)
        values.push_back(std::abs(b.getSample(0, i) - 2.0f * b.getSample(0, i - 1) + b.getSample(0, i - 2)));
    if (values.size() < 1000) return 0.0f;
    std::sort(values.begin(), values.end());
    const auto sustained = values[static_cast<size_t>(static_cast<double>(values.size()) * 0.995)];
    return values.back() / juce::jmax(1.0e-9f, sustained);
}

bool finiteAndBounded(const juce::AudioBuffer<float>& b, float limit, float& peak)
{
    peak = 0.0f;
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const auto s = b.getSample(c, i);
            if (! std::isfinite(s)) return false;
            peak = juce::jmax(peak, std::abs(s));
        }
    return peak < limit;
}

// ================================================================== ORBIT
void prepareOrbit(PluginProcessor& p, float timeMs, float feedback, bool sync)
{
    set(p, "tempo_sync", sync ? 1.0f : 0.0f);
    set(p, "time", timeMs);
    set(p, "feedback", feedback);
    set(p, "mix", 100.0f);
    set(p, "duck", 0.0f);
    set(p, "mod_depth", 0.0f);
    for (int tap = 1; tap <= 8; ++tap) set(p, "tap." + juce::String(tap).paddedLeft('0', 2) + ".enabled", 0.0f);
    p.prepareToPlay(rate, 256);
}

void orbitChecks()
{
    {
        auto p = fresh();
        prepareOrbit(*p, 300.0f, 0.0f, false);
        const auto out = run(*p, impulse(1.0));
        const auto at = peakIndex(out, 200, out.getNumSamples());
        check(std::abs(at - (100 + 14400)) <= 1, "a 300 ms echo lands 14400 samples later (found " + juce::String(at - 100) + ")");
    }
    {
        auto p = fresh();
        prepareOrbit(*p, 500.0f, 0.0f, false);
        const auto out = run(*p, impulse(1.0));
        const auto at = peakIndex(out, 200, out.getNumSamples());
        check(std::abs(at - (100 + 24000)) <= 1, "the TIME control moves the echo: 500 ms lands 24000 samples later (found " + juce::String(at - 100) + ")");
    }
    {
        auto p = fresh();
        prepareOrbit(*p, 250.0f, 50.0f, false);
        const auto out = run(*p, impulse(1.5));
        const auto first = peakIn(out, 100 + 12000 - 50, 100 + 12000 + 50);
        const auto second = peakIn(out, 100 + 24000 - 50, 100 + 24000 + 50);
        const auto third = peakIn(out, 100 + 36000 - 50, 100 + 36000 + 50);
        const auto ratio1 = second / juce::jmax(1.0e-9f, first), ratio2 = third / juce::jmax(1.0e-9f, second);
        check(ratio1 > 0.3f && ratio1 < 0.56f && ratio2 > 0.3f && ratio2 < 0.56f,
              "50 % feedback roughly halves each repeat (ratios " + juce::String(ratio1, 2) + ", " + juce::String(ratio2, 2) + ")");
    }
    {
        // No play head here, so the engine assumes 120 BPM: the grid is 125 ms.
        auto p = fresh();
        prepareOrbit(*p, 400.0f, 0.0f, true);
        const auto out = run(*p, impulse(1.0));
        const auto at = peakIndex(out, 200, out.getNumSamples()) - 100;
        check(std::abs(at - 18000) <= 1, "tempo sync snaps 400 ms to the sixteenth-note grid, 375 ms at 120 BPM (found " + juce::String(at / 48.0, 1) + " ms)");
    }
    {
        // A tap on its own time, quieter than the main echo, panned hard left.
        auto p = fresh();
        prepareOrbit(*p, 400.0f, 0.0f, false);
        set(*p, "tap.02.enabled", 1.0f);
        set(*p, "tap.02.time", 150.0f);
        set(*p, "tap.02.level", -6.0f);
        set(*p, "tap.02.pan", -100.0f);
        set(*p, "tap.02.filter", 20000.0f);
        p->prepareToPlay(rate, 256);
        const auto out = run(*p, impulse(1.0));
        const auto tapLeft = peakIn(out, 100 + 7200 - 20, 100 + 7200 + 20, 0);
        const auto tapRight = peakIn(out, 100 + 7200 - 20, 100 + 7200 + 20, 1);
        check(tapLeft > 0.05f && tapRight < tapLeft * 0.05f, "a tap plays at its own time and pans (left " + juce::String(tapLeft, 3) + ", right " + juce::String(tapRight, 3) + ")");
    }
    {
        // Modulation must bend pitch smoothly, never step.
        auto p = fresh();
        prepareOrbit(*p, 120.0f, 0.0f, false);
        set(*p, "mod_depth", 10.0f);
        set(*p, "mod_rate", 2.0f);
        p->prepareToPlay(rate, 256);
        const auto out = run(*p, sine(2.0, 1000.0f, 0.3f));
        const auto rough = roughness(out, 24000, out.getNumSamples());
        check(rough < 0.012f, "modulated echo is smooth, no stepping (roughness " + juce::String(rough, 4) + ", a one-sample jump would be 0.039)");
    }
    {
        // Turning TIME while playing glides instead of clicking.
        auto p = fresh();
        prepareOrbit(*p, 200.0f, 30.0f, false);
        auto audio = sine(3.0, 1000.0f, 0.3f);
        juce::MidiBuffer midi;
        for (int start = 0; start + 256 <= audio.getNumSamples(); start += 256)
        {
            if (start == 256 * 200) set(*p, "time", 450.0f);
            juce::AudioBuffer<float> view(audio.getArrayOfWritePointers(), 2, start, 256);
            p->processBlock(view, midi);
        }
        // Only whole blocks were processed; the remainder is untouched input.
        const auto processed = audio.getNumSamples() / 256 * 256;
        const auto spike = spikiness(audio, 256 * 190, processed);
        check(spike < 2.5f, "changing TIME while playing bends pitch and does not click (worst spike " + juce::String(spike, 2) + "x the sustained level, a click scores 5x or more)");
    }
    {
        auto p = fresh();
        prepareOrbit(*p, 250.0f, 40.0f, false);
        auto audio = noise(6.0, 0.3f, false);
        for (int i = static_cast<int>(rate); i < audio.getNumSamples(); ++i) { audio.setSample(0, i, 0.0f); audio.setSample(1, i, 0.0f); }
        juce::MidiBuffer midi;
        for (int start = 0; start + 256 <= audio.getNumSamples(); start += 256)
        {
            if (start == 256 * 180) set(*p, "freeze", 1.0f);
            juce::AudioBuffer<float> view(audio.getArrayOfWritePointers(), 2, start, 256);
            p->processBlock(view, midi);
        }
        const auto early = rms(audio, static_cast<int>(rate * 1.2), static_cast<int>(rate * 1.7));
        const auto late = rms(audio, static_cast<int>(rate * 5.2), static_cast<int>(rate * 5.7));
        check(late > early * 0.7, "freeze holds the loop (level after four seconds " + juce::String(late / juce::jmax(1.0e-9, early) * 100.0, 0) + " % of where it froze)");
    }
    {
        auto p = fresh();
        prepareOrbit(*p, 90.0f, 98.0f, false);
        for (int tap = 1; tap <= 8; ++tap)
        {
            const auto prefix = "tap." + juce::String(tap).paddedLeft('0', 2);
            set(*p, prefix + ".enabled", 1.0f);
            set(*p, prefix + ".level", 6.0f);
        }
        set(*p, "mod_depth", 50.0f);
        p->prepareToPlay(rate, 256);
        float peak = 0.0f;
        const auto ok = finiteAndBounded(run(*p, noise(6.0, 0.9f, false)), 6.0f, peak);
        check(ok, "stays finite and bounded with everything at maximum (peak " + juce::String(peak, 2) + ")");
    }
    {
        auto p = fresh();
        prepareOrbit(*p, 300.0f, 60.0f, false);
        set(*p, "mix", 0.0f);
        p->prepareToPlay(rate, 256);
        const auto input = noise(0.5, 0.3f, false);
        const auto out = run(*p, input);
        float worst = 0.0f;
        for (int i = 0; i < out.getNumSamples(); ++i) worst = juce::jmax(worst, std::abs(out.getSample(0, i) - input.getSample(0, i)));
        check(worst < 1.0e-6f, "MIX at 0 % is the untouched input");
    }
}

// ================================================================== SPACEVERB
void prepareVerb(PluginProcessor& p, float decay)
{
    set(p, "decay", decay);
    set(p, "pre_delay", 0.0f);
    set(p, "size", 70.0f);
    set(p, "early_late", 100.0f);
    set(p, "mod_depth", 0.0f);
    set(p, "high_damp", 2.0f);
    set(p, "low_damp", 1.0f);
    set(p, "duck", 0.0f);
    set(p, "mix", 100.0f);
    p.prepareToPlay(rate, 256);
}

/** T60 from the backward-integrated energy curve, fitted from -5 to -35 dB. */
double measuredT60(const juce::AudioBuffer<float>& tail, int from)
{
    const auto n = tail.getNumSamples();
    std::vector<double> energy(static_cast<size_t>(n), 0.0);
    double sum = 0.0;
    for (int i = n - 1; i >= from; --i)
    {
        sum += static_cast<double>(tail.getSample(0, i)) * tail.getSample(0, i);
        energy[static_cast<size_t>(i)] = sum;
    }
    const auto total = energy[static_cast<size_t>(from)];
    if (total <= 0.0) return 0.0;
    int at5 = -1, at35 = -1;
    for (int i = from; i < n; ++i)
    {
        const auto db = 10.0 * std::log10(juce::jmax(1.0e-30, energy[static_cast<size_t>(i)] / total));
        if (at5 < 0 && db <= -5.0) at5 = i;
        if (at35 < 0 && db <= -35.0) { at35 = i; break; }
    }
    if (at5 < 0 || at35 < 0) return 0.0;
    return (at35 - at5) / rate * 2.0;
}

void verbChecks()
{
    for (const auto decay : { 0.8f, 2.0f, 5.0f })
    {
        auto p = fresh();
        prepareVerb(*p, decay);
        const auto out = run(*p, impulse(decay * 2.2 + 1.0));
        const auto t60 = measuredT60(out, 200);
        check(t60 > decay * 0.75 && t60 < decay * 1.25,
              "DECAY " + juce::String(decay, 1) + " s really takes " + juce::String(t60, 2) + " s to fall 60 dB");
    }
    {
        // A mono source must still come back wide.
        auto p = fresh();
        prepareVerb(*p, 2.4f);
        const auto out = run(*p, noise(2.0, 0.3f, true));
        double lr = 0.0, ll = 0.0, rr = 0.0;
        for (int i = static_cast<int>(rate); i < out.getNumSamples(); ++i)
        {
            const double l = out.getSample(0, i), r = out.getSample(1, i);
            lr += l * r; ll += l * l; rr += r * r;
        }
        const auto correlation = lr / juce::jmax(1.0e-30, std::sqrt(ll * rr));
        check(std::abs(correlation) < 0.6, "a mono source returns in stereo (left/right correlation " + juce::String(correlation, 2) + ")");
    }
    {
        auto p = fresh();
        prepareVerb(*p, 2.4f);
        set(*p, "width", 0.0f);
        p->prepareToPlay(rate, 256);
        const auto out = run(*p, noise(1.0, 0.3f, false));
        float worst = 0.0f;
        for (int i = 0; i < out.getNumSamples(); ++i) worst = juce::jmax(worst, std::abs(out.getSample(0, i) - out.getSample(1, i)));
        check(worst < 1.0e-5f, "WIDTH at 0 % is mono");
    }
    {
        // A dense tail has no strong repeating echo in it.
        auto p = fresh();
        prepareVerb(*p, 2.4f);
        const auto out = run(*p, impulse(2.0));
        const auto from = static_cast<int>(rate * 0.3), length = static_cast<int>(rate * 0.5);
        double zero = 0.0, worst = 0.0;
        for (int i = 0; i < length; ++i) zero += static_cast<double>(out.getSample(0, from + i)) * out.getSample(0, from + i);
        for (int lag = static_cast<int>(rate * 0.004); lag < static_cast<int>(rate * 0.12); ++lag)
        {
            double sum = 0.0;
            for (int i = 0; i < length; i += 3) sum += static_cast<double>(out.getSample(0, from + i)) * out.getSample(0, from + i + lag);
            worst = juce::jmax(worst, std::abs(sum * 3.0) / juce::jmax(1.0e-30, zero));
        }
        check(worst < 0.5, "the tail is dense, not a fluttering echo (strongest repeat " + juce::String(worst, 2) + ")");
    }
    {
        auto p = fresh();
        prepareVerb(*p, 2.4f);
        set(*p, "mod_depth", 100.0f);
        set(*p, "mod_rate", 2.0f);
        p->prepareToPlay(rate, 256);
        const auto out = run(*p, sine(3.0, 500.0f, 0.3f));
        const auto rough = roughness(out, static_cast<int>(rate * 1.5), out.getNumSamples());
        check(rough < 0.02f, "modulated tail is smooth, no stepping (roughness " + juce::String(rough, 4) + ")");
    }
    {
        // MIX should mean the same thing whatever the decay.
        juce::String levels;
        bool inRange = true;
        for (const auto decay : { 0.5f, 2.4f, 10.0f })
        {
            auto p = fresh();
            prepareVerb(*p, decay);
            const auto input = noise(decay * 1.5 + 3.0, 0.2f, false);
            const auto out = run(*p, input);
            const auto from = out.getNumSamples() - static_cast<int>(rate);
            const auto ratio = rms(out, from, out.getNumSamples()) / rms(input, from, out.getNumSamples());
            levels << juce::String(ratio, 2) << " ";
            inRange = inRange && ratio > 0.3 && ratio < 1.7;
        }
        check(inRange, "wet level stays comparable from short rooms to long halls (ratios " + levels.trim() + ")");
    }
    {
        auto p = fresh();
        prepareVerb(*p, 3.0f);
        auto audio = noise(7.0, 0.3f, false);
        for (int i = static_cast<int>(rate); i < audio.getNumSamples(); ++i) { audio.setSample(0, i, 0.0f); audio.setSample(1, i, 0.0f); }
        juce::MidiBuffer midi;
        for (int start = 0; start + 256 <= audio.getNumSamples(); start += 256)
        {
            if (start == 256 * 190) set(*p, "freeze", 1.0f);
            juce::AudioBuffer<float> view(audio.getArrayOfWritePointers(), 2, start, 256);
            p->processBlock(view, midi);
        }
        const auto early = rms(audio, static_cast<int>(rate * 1.2), static_cast<int>(rate * 1.7));
        const auto late = rms(audio, static_cast<int>(rate * 6.2), static_cast<int>(rate * 6.7));
        check(late > early * 0.6, "freeze holds the space (level after five seconds " + juce::String(late / juce::jmax(1.0e-9, early) * 100.0, 0) + " % of where it froze)");
    }
    {
        auto p = fresh();
        prepareVerb(*p, 40.0f);
        set(*p, "size", 100.0f);
        set(*p, "density", 100.0f);
        set(*p, "diffusion", 100.0f);
        set(*p, "mod_depth", 100.0f);
        set(*p, "low_damp", 2.0f);
        set(*p, "width", 200.0f);
        p->prepareToPlay(rate, 256);
        float peak = 0.0f;
        const auto ok = finiteAndBounded(run(*p, noise(8.0, 0.9f, false)), 12.0f, peak);
        check(ok, "stays finite and bounded with everything at maximum (peak " + juce::String(peak, 2) + ")");
    }
    {
        auto p = fresh();
        prepareVerb(*p, 2.4f);
        juce::AudioBuffer<float> silence(2, static_cast<int>(rate));
        silence.clear();
        float peak = 0.0f;
        finiteAndBounded(run(*p, silence), 1.0f, peak);
        check(peak < 1.0e-7f, "silence in gives silence out");
    }
    {
        auto p = fresh();
        prepareVerb(*p, 2.4f);
        set(*p, "mix", 0.0f);
        p->prepareToPlay(rate, 256);
        const auto input = noise(0.5, 0.3f, false);
        const auto out = run(*p, input);
        float worst = 0.0f;
        for (int i = 0; i < out.getNumSamples(); ++i) worst = juce::jmax(worst, std::abs(out.getSample(0, i) - input.getSample(0, i)));
        check(worst < 1.0e-6f, "MIX at 0 % is the untouched input");
    }
    {
        // Early reflections arrive before the tail builds, after the pre-delay.
        auto p = fresh();
        prepareVerb(*p, 2.4f);
        set(*p, "pre_delay", 50.0f);
        set(*p, "early_late", -100.0f);
        p->prepareToPlay(rate, 256);
        const auto out = run(*p, impulse(0.5));
        const auto before = peakIn(out, 110, 100 + static_cast<int>(rate * 0.05));
        const auto after = peakIn(out, 100 + static_cast<int>(rate * 0.05), 100 + static_cast<int>(rate * 0.13));
        check(before < 1.0e-4f && after > 0.05f, "early reflections start after the pre-delay (before " + juce::String(before, 5) + ", after " + juce::String(after, 3) + ")");
    }
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    const auto id = fresh()->spec.id;
    std::cout << std::unitbuf << "== " << id << " time-effect measurements ==\n";

   #if AMANORSAC_LICENSING_ENABLED
    std::cout << "  note  this build enforces the licence, so output may be muted; run the test build\n";
   #endif

    if (id == "D08") orbitChecks();
    else if (id == "D09") verbChecks();
    else { std::cout << "  this product has no time-effect measurements\n"; }

    std::cout << (failures == 0 ? "PASS" : "FAIL") << ": " << id << " measurements, " << failures << " failure(s)\n";
    return failures == 0 ? 0 : 1;
}
