// Heritage EQ (A01) behavioural audit.
//
// Two questions, answered numerically:
//   1. Does every parameter in the contract audibly change the output?
//   2. Do the specific behaviours the faceplate promises actually hold:
//      shelves shelve, PHASE inverts, EQ IN / FILTER IN / BYPASS defeat their
//      sections cleanly, MIX at 0 % is a latency-aligned passthrough, the
//      reported latency is the real delay, and nothing produces NaNs across
//      sample rates and block sizes.

#include "common/dsp/AnchorDSP.h"
#include "common/state/PluginSpec.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <iomanip>
#include <iostream>

namespace
{
class HarnessProcessor final : public juce::AudioProcessor
{
public:
    explicit HarnessProcessor(const amanorsac::PluginSpec& spec)
        : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
          state(*this, nullptr, "AMANORSAC_TEST_STATE", amanorsac::PluginSpec::createParameterLayout(spec)) {}
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override
    { return layouts.getMainInputChannelSet() == layouts.getMainOutputChannelSet(); }
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    const juce::String getName() const override { return "HeritageHarness"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}
    juce::AudioProcessorValueTreeState state;
};

int failures = 0;

void check(bool condition, const juce::String& what)
{
    std::cout << (condition ? "  ok    " : "  FAIL  ") << what << std::endl;
    if (! condition) ++failures;
}

bool set(juce::AudioProcessorValueTreeState& state, const juce::String& id, float value)
{
    if (auto* parameter = state.getParameter(id))
    {
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
        return true;
    }
    return false;
}

// Deterministic broadband test signal with distinct left and right content.
juce::AudioBuffer<float> makeSignal(double rate, int samples, float lowHz = 60.0f, float highHz = 5000.0f,
                                    float lowLevel = 0.25f, float highLevel = 0.20f, float noiseLevel = 0.15f)
{
    juce::AudioBuffer<float> buffer(2, samples);
    juce::uint32 seed = 0x2545F491u;
    for (int n = 0; n < samples; ++n)
    {
        seed = seed * 1664525u + 1013904223u;
        const auto noise = (static_cast<float>(seed >> 8) / 16777216.0f) * 2.0f - 1.0f;
        const auto t = static_cast<double>(n) / rate;
        const auto low = static_cast<float>(std::sin(juce::MathConstants<double>::twoPi * lowHz * t));
        const auto high = static_cast<float>(std::sin(juce::MathConstants<double>::twoPi * highHz * t));
        const auto mid = static_cast<float>(std::sin(juce::MathConstants<double>::twoPi * 997.0 * t));
        buffer.setSample(0, n, lowLevel * low + highLevel * high + 0.10f * mid + noiseLevel * noise);
        buffer.setSample(1, n, lowLevel * 0.7f * low - highLevel * high + 0.10f * mid + noiseLevel * noise * 0.5f);
    }
    return buffer;
}

struct Rendered
{
    juce::AudioBuffer<float> out;
    int latency = 0;
};

Rendered render(juce::AudioProcessorValueTreeState& state, const juce::AudioBuffer<float>& input,
                double rate, int blockSize)
{
    amanorsac::AnchorDSP dsp;
    dsp.prepare(rate, blockSize, 2);
    Rendered result;
    result.out.makeCopyOf(input);
    const auto total = input.getNumSamples();
    for (int start = 0; start < total; start += blockSize)
    {
        const auto length = juce::jmin(blockSize, total - start);
        juce::AudioBuffer<float> block(result.out.getArrayOfWritePointers(), 2, start, length);
        dsp.process(block, state, "A01");
    }
    result.latency = dsp.latencySamples();
    return result;
}

double energy(const juce::AudioBuffer<float>& a, int from)
{
    double e = 0.0;
    for (int c = 0; c < a.getNumChannels(); ++c)
        for (int n = from; n < a.getNumSamples(); ++n)
            e += static_cast<double>(a.getSample(c, n)) * a.getSample(c, n);
    return e;
}

/** Energy of (a[n] - sign * b[n - offset]) from a start index. */
double differenceEnergy(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b,
                        int from, int offset = 0, float sign = 1.0f)
{
    double e = 0.0;
    for (int c = 0; c < a.getNumChannels(); ++c)
        for (int n = from; n < a.getNumSamples(); ++n)
        {
            const auto m = n - offset;
            const auto reference = m >= 0 && m < b.getNumSamples() ? b.getSample(c, m) : 0.0f;
            const auto d = a.getSample(c, n) - sign * reference;
            e += static_cast<double>(d) * d;
        }
    return e;
}

bool finite(const juce::AudioBuffer<float>& buffer)
{
    for (int c = 0; c < buffer.getNumChannels(); ++c)
        for (int n = 0; n < buffer.getNumSamples(); ++n)
            if (! std::isfinite(buffer.getSample(c, n))) return false;
    return true;
}

/** A setting where every control has something to act on. */
void applyRichBaseline(juce::AudioProcessorValueTreeState& s)
{
    set(s, "input_trim", 0.0f);      set(s, "output_trim", 0.0f);
    set(s, "phase", 0.0f);           set(s, "bypass", 0.0f);
    set(s, "hpf", 120.0f);           set(s, "lpf", 8000.0f);
    set(s, "filter_in", 1.0f);       set(s, "filter_slope", 0.0f);
    set(s, "eq_in", 1.0f);
    set(s, "band.low.freq", 80.0f);       set(s, "band.low.gain", 6.0f);   set(s, "band.low.q", 0.7f);
    set(s, "band.lowmid.freq", 250.0f);   set(s, "band.lowmid.gain", -4.0f); set(s, "band.lowmid.q", 0.9f);
    set(s, "band.mid.freq", 1000.0f);     set(s, "band.mid.gain", 5.0f);   set(s, "band.mid.q", 0.9f);
    set(s, "band.highmid.freq", 3500.0f); set(s, "band.highmid.gain", -3.0f); set(s, "band.highmid.q", 0.9f);
    set(s, "band.high.freq", 10000.0f);   set(s, "band.high.gain", 6.0f);  set(s, "band.high.slope", 0.7f);
    for (const auto* band : { "low", "lowmid", "mid", "highmid", "high" })
        set(s, juce::String("band.") + band + ".enabled", 1.0f);
    set(s, "character", 1.0f);       set(s, "drive", 6.0f);
    set(s, "auto_gain", 1.0f);       set(s, "stereo_mode", 0.0f);
    set(s, "oversampling", 1.0f);    set(s, "mix", 100.0f);
}

/** Everything flat: a transparent chain apart from what a test switches on. */
void applyFlatBaseline(juce::AudioProcessorValueTreeState& s)
{
    applyRichBaseline(s);
    set(s, "hpf", 20.0f); set(s, "lpf", 40000.0f);
    for (const auto* band : { "low", "lowmid", "mid", "highmid", "high" })
        set(s, juce::String("band.") + band + ".gain", 0.0f);
    set(s, "character", 0.0f); set(s, "drive", 0.0f);
    set(s, "auto_gain", 0.0f); set(s, "oversampling", 0.0f);
}
}

int main()
{
    const auto spec = amanorsac::PluginSpec::fromEmbeddedJson();
    if (spec.id != "A01")
    {
        std::cerr << "FAIL: expected A01, got " << spec.id << std::endl;
        return 1;
    }

    HarnessProcessor harness(spec);
    auto& state = harness.state;

    constexpr double rate = 48000.0;
    constexpr int block = 512;
    constexpr int blocks = 12;
    constexpr int warmup = 4 * block;          // past the 20 ms gain ramps at any setting
    const auto input = makeSignal(rate, block * blocks);
    const auto inputEnergy = energy(input, warmup);

    // ------------------------------------------------------------------
    std::cout << "\n== 1. every parameter changes the output ==\n";
    for (const auto& p : spec.parameters)
    {
        float lo = 0.0f, hi = 1.0f;
        using Kind = amanorsac::ParameterDescriptor::Kind;
        if (p.kind == Kind::floating || p.kind == Kind::integer) { lo = p.minimum; hi = p.maximum; }
        else if (p.kind == Kind::choice) { lo = 0.0f; hi = static_cast<float>(juce::jmax(1, p.choices.size() - 1)); }

        applyRichBaseline(state);
        set(state, p.id, lo);
        const auto a = render(state, input, rate, block);
        applyRichBaseline(state);
        set(state, p.id, hi);
        const auto b = render(state, input, rate, block);

        const auto ratio = differenceEnergy(a.out, b.out, warmup) / juce::jmax(1.0e-12, inputEnergy);
        std::ostringstream line;
        line << std::left << std::setw(22) << p.id.toStdString() << " min->max delta " << std::scientific
             << std::setprecision(2) << ratio;
        check(finite(a.out) && finite(b.out) && ratio > 1.0e-5, line.str());
    }

    // ------------------------------------------------------------------
    std::cout << "\n== 2. shelves shelve (and are not bells) ==\n";
    {
        // A shelf is flat beyond its corner, so a tone deep in the plateau
        // receives the full gain and a tone at the corner about half of it. A
        // bell centred on the same frequency would give the plateau tone LESS
        // than the corner tone, which is exactly what distinguishes the two.
        // A pure sine, so nothing but the probe frequency is present. The
        // broadband makeSignal() carries a fixed 997 Hz component that would
        // otherwise sit on top of the 1 kHz bell and contaminate every probe.
        auto tone = [&](float hz)
        {
            juce::AudioBuffer<float> buffer(2, block * blocks);
            for (int n = 0; n < buffer.getNumSamples(); ++n)
            {
                const auto v = 0.3f * static_cast<float>(
                    std::sin(juce::MathConstants<double>::twoPi * hz * static_cast<double>(n) / rate));
                buffer.setSample(0, n, v);
                buffer.setSample(1, n, v);
            }
            return buffer;
        };
        auto gainAt = [&](float hz)
        {
            const auto probe = tone(hz);
            return energy(render(state, probe, rate, block).out, warmup) / energy(probe, warmup);
        };
        const auto full = std::pow(10.0, 12.0 / 10.0);   // +12 dB in energy

        applyFlatBaseline(state); set(state, "band.low.freq", 80.0f); set(state, "band.low.gain", 12.0f);
        const auto lowPlateau = gainAt(25.0f), lowCorner = gainAt(80.0f), lowFar = gainAt(8000.0f);
        check(lowPlateau > 0.7 * full && lowPlateau > lowCorner && lowFar < 1.3,
              "LOW +12 dB @80 Hz: plateau 25 Hz x" + juce::String(lowPlateau, 1) + " > corner 80 Hz x"
                  + juce::String(lowCorner, 1) + ", 8 kHz x" + juce::String(lowFar, 2));

        applyFlatBaseline(state); set(state, "band.high.freq", 8000.0f); set(state, "band.high.gain", 12.0f);
        const auto highPlateau = gainAt(16000.0f), highCorner = gainAt(8000.0f), highFar = gainAt(60.0f);
        check(highPlateau > 0.7 * full && highPlateau > highCorner && highFar < 1.3,
              "HIGH +12 dB @8 kHz: plateau 16 kHz x" + juce::String(highPlateau, 1) + " > corner 8 kHz x"
                  + juce::String(highCorner, 1) + ", 60 Hz x" + juce::String(highFar, 2));

        // the mid band must still be a bell: energy falls away on both sides
        applyFlatBaseline(state); set(state, "band.mid.freq", 1000.0f); set(state, "band.mid.gain", 12.0f);
        set(state, "band.mid.q", 1.5f);
        const auto midCentre = gainAt(1000.0f), midBelow = gainAt(125.0f), midAbove = gainAt(8000.0f);
        check(midCentre > 0.7 * full && midBelow < 1.5 && midAbove < 1.5,
              "MID +12 dB @1 kHz is a bell: centre x" + juce::String(midCentre, 1) + ", 125 Hz x"
                  + juce::String(midBelow, 2) + ", 8 kHz x" + juce::String(midAbove, 2));
    }

    // ------------------------------------------------------------------
    std::cout << "\n== 3. switches defeat their sections cleanly ==\n";
    {
        applyFlatBaseline(state);
        const auto flat = render(state, input, rate, block);
        check(differenceEnergy(flat.out, input, warmup) / inputEnergy < 1.0e-3,
              "flat chain is transparent");

        applyFlatBaseline(state); set(state, "phase", 1.0f);
        const auto inverted = render(state, input, rate, block);
        check(differenceEnergy(inverted.out, input, warmup, 0, -1.0f) / inputEnergy < 1.0e-3,
              "PHASE inverts polarity exactly");

        applyRichBaseline(state); set(state, "oversampling", 0.0f); set(state, "character", 0.0f);
        set(state, "hpf", 20.0f); set(state, "lpf", 40000.0f); set(state, "auto_gain", 0.0f);
        set(state, "eq_in", 0.0f);
        const auto eqOut = render(state, input, rate, block);
        check(differenceEnergy(eqOut.out, input, warmup) / inputEnergy < 1.0e-3,
              "EQ IN off removes all five bands");

        applyFlatBaseline(state); set(state, "hpf", 300.0f); set(state, "lpf", 3000.0f); set(state, "filter_in", 0.0f);
        const auto filterOut = render(state, input, rate, block);
        check(differenceEnergy(filterOut.out, input, warmup) / inputEnergy < 1.0e-3,
              "FILTER IN off removes HPF and LPF");

        applyRichBaseline(state); set(state, "oversampling", 0.0f); set(state, "bypass", 1.0f);
        const auto bypassed = render(state, input, rate, block);
        check(differenceEnergy(bypassed.out, input, warmup) / inputEnergy < 1.0e-3,
              "BYPASS returns the untouched input");
    }

    // ------------------------------------------------------------------
    std::cout << "\n== 4. latency is reported and the dry path is aligned to it ==\n";
    {
        applyRichBaseline(state); set(state, "oversampling", 0.0f);
        const auto none = render(state, input, rate, block);
        applyRichBaseline(state); set(state, "oversampling", 1.0f);
        const auto twice = render(state, input, rate, block);
        applyRichBaseline(state); set(state, "oversampling", 2.0f);
        const auto quad = render(state, input, rate, block);
        check(none.latency == 0, "oversampling off reports 0 samples");
        check(twice.latency > 0, "2x reports " + juce::String(twice.latency) + " samples");
        check(quad.latency >= twice.latency, "4x reports " + juce::String(quad.latency) + " samples");

        // MIX 0 % through the 4x path must equal the input delayed by exactly
        // the reported latency; if the dry delay were wrong this combs.
        applyRichBaseline(state); set(state, "oversampling", 2.0f); set(state, "mix", 0.0f);
        set(state, "auto_gain", 0.0f);
        const auto dryPath = render(state, input, rate, block);
        const auto aligned = differenceEnergy(dryPath.out, input, warmup, dryPath.latency) / inputEnergy;
        const auto misaligned = differenceEnergy(dryPath.out, input, warmup, 0) / inputEnergy;
        check(aligned < 1.0e-3, "MIX 0 % equals input delayed by reported latency (residual "
                                    + juce::String(aligned, 6) + ")");
        check(dryPath.latency == 0 || misaligned > aligned * 10.0,
              "...and that alignment is meaningful, not coincidental");
    }

    // ------------------------------------------------------------------
    std::cout << "\n== 5. safety ==\n";
    {
        applyRichBaseline(state);
        juce::AudioBuffer<float> silence(2, block * blocks);
        silence.clear();
        const auto quiet = render(state, silence, rate, block);
        check(finite(quiet.out) && quiet.out.getMagnitude(0, quiet.out.getNumSamples()) < 1.0e-6f,
              "silence in, silence out");

        bool allFinite = true;
        for (const auto testRate : { 44100.0, 96000.0, 192000.0 })
            for (const auto testBlock : { 32, 256, 1024 })
            {
                applyRichBaseline(state);
                set(state, "drive", 10.0f); set(state, "character", 3.0f); set(state, "oversampling", 2.0f);
                set(state, "input_trim", 24.0f); set(state, "filter_slope", 1.0f);
                set(state, "band.low.gain", 15.0f); set(state, "band.high.gain", 15.0f);
                set(state, "band.low.q", 3.0f); set(state, "band.high.slope", 3.0f);
                const auto extreme = render(state, makeSignal(testRate, testBlock * 8), testRate, testBlock);
                allFinite = allFinite && finite(extreme.out);
            }
        check(allFinite, "no NaN/Inf at 44.1/96/192 kHz x 32/256/1024 samples under extreme settings");
    }

    // ------------------------------------------------------------------
    std::cout << "\n== 6. state recall ==\n";
    {
        applyRichBaseline(state);
        set(state, "band.mid.q", 2.5f); set(state, "filter_slope", 1.0f); set(state, "band.high.enabled", 0.0f);
        const auto saved = state.copyState();
        applyFlatBaseline(state);
        state.replaceState(saved.createCopy());
        const auto q = state.getRawParameterValue("band.mid.q")->load();
        const auto slope = state.getRawParameterValue("filter_slope")->load();
        const auto enabled = state.getRawParameterValue("band.high.enabled")->load();
        check(std::abs(q - 2.5f) < 0.01f && slope > 0.5f && enabled < 0.5f,
              "new parameters survive a state round-trip");
    }

    std::cout << "\n" << (failures == 0 ? "PASS" : "FAIL") << ": Heritage EQ audit, "
              << failures << " failure(s)" << std::endl;
    return failures == 0 ? 0 : 1;
}
