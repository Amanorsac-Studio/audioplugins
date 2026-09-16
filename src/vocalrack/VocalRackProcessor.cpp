#include "VocalRackProcessor.h"
#include "VocalRackEditor.h"

#include <cmath>
#include <complex>

namespace amanorsac::vocalrack
{
namespace
{
constexpr double pi = juce::MathConstants<double>::pi;
constexpr double rateBeats[] { 2.0, 1.0, 1.5, 0.5, 0.75, 1.0 / 3.0, 0.25 };

void assign(Biquad& f, double b0, double b1, double b2, double a0, double a1, double a2) noexcept
{
    f.b0 = b0 / a0; f.b1 = b1 / a0; f.b2 = b2 / a0; f.a1 = a1 / a0; f.a2 = a2 / a0;
}

double omega(double rate, double f) noexcept { return 2.0 * pi * juce::jlimit(10.0, rate * 0.49, f) / rate; }

float softClip(float x, float drive) noexcept
{
    return drive <= 1.0001f ? x : std::tanh(x * drive) / drive;
}
}

void Biquad::peak(double r, double f, double q, double db) noexcept
{
    const auto A = std::pow(10.0, db / 40.0), w = omega(r, f), alpha = std::sin(w) / (2.0 * q), cs = std::cos(w);
    assign(*this, 1 + alpha * A, -2 * cs, 1 - alpha * A, 1 + alpha / A, -2 * cs, 1 - alpha / A);
}

void Biquad::lowShelf(double r, double f, double db) noexcept
{
    const auto A = std::pow(10.0, db / 40.0), w = omega(r, f), cs = std::cos(w);
    const auto s = 2.0 * std::sqrt(A) * std::sin(w) / std::sqrt(2.0);
    assign(*this, A * ((A + 1) - (A - 1) * cs + s), 2 * A * ((A - 1) - (A + 1) * cs), A * ((A + 1) - (A - 1) * cs - s),
           (A + 1) + (A - 1) * cs + s, -2 * ((A - 1) + (A + 1) * cs), (A + 1) + (A - 1) * cs - s);
}

void Biquad::highShelf(double r, double f, double db) noexcept
{
    const auto A = std::pow(10.0, db / 40.0), w = omega(r, f), cs = std::cos(w);
    const auto s = 2.0 * std::sqrt(A) * std::sin(w) / std::sqrt(2.0);
    assign(*this, A * ((A + 1) + (A - 1) * cs + s), -2 * A * ((A - 1) + (A + 1) * cs), A * ((A + 1) + (A - 1) * cs - s),
           (A + 1) - (A - 1) * cs + s, 2 * ((A - 1) - (A + 1) * cs), (A + 1) - (A - 1) * cs - s);
}

void Biquad::lowPass(double r, double f, double q) noexcept
{
    const auto w = omega(r, f), cs = std::cos(w), alpha = std::sin(w) / (2.0 * q);
    assign(*this, (1 - cs) * 0.5, 1 - cs, (1 - cs) * 0.5, 1 + alpha, -2 * cs, 1 - alpha);
}

void Biquad::highPass(double r, double f, double q) noexcept
{
    const auto w = omega(r, f), cs = std::cos(w), alpha = std::sin(w) / (2.0 * q);
    assign(*this, (1 + cs) * 0.5, -(1 + cs), (1 + cs) * 0.5, 1 + alpha, -2 * cs, 1 - alpha);
}

void Biquad::bandPass(double r, double f, double q) noexcept
{
    const auto w = omega(r, f), cs = std::cos(w), alpha = std::sin(w) / (2.0 * q);
    assign(*this, alpha, 0, -alpha, 1 + alpha, -2 * cs, 1 - alpha);
}

double Biquad::magnitude(double r, double f) const noexcept
{
    const auto w = 2.0 * pi * f / r;
    const std::complex<double> e1 = std::polar(1.0, -w), e2 = std::polar(1.0, -2.0 * w);
    return std::abs((b0 + b1 * e1 + b2 * e2) / (1.0 + a1 * e1 + a2 * e2));
}

const ToneVoice& toneVoice(int style)
{
    static constexpr ToneVoice voices[] { { 120, 900, 3500, 11000, 20, 20000, 0.0f }, { 160, 650, 2800, 9000, 20, 16000, 0.15f },
                                          { 100, 1100, 5000, 14000, 20, 20000, 0.0f }, { 250, 1600, 3000, 8000, 300, 5000, 0.35f },
                                          { 110, 1200, 4200, 16000, 20, 20000, 0.05f } };
    return voices[juce::jlimit(0, 4, style)];
}

// ------------------------------------------------------------------ names
const juce::StringArray& dynamicsStyles() { static const juce::StringArray s { "Smooth", "Punchy", "Vintage", "Upfront", "Broadcast" }; return s; }
const juce::StringArray& toneStyles() { static const juce::StringArray s { "Clean", "Warm", "Bright", "Radio", "Silk" }; return s; }
const juce::StringArray& spaceStyles() { static const juce::StringArray s { "Room", "Hall", "Plate", "Chamber", "Cathedral" }; return s; }
const juce::StringArray& fxStyles() { static const juce::StringArray s { "Doubler", "Chorus", "Lo-Fi", "Tremolo", "Wobble", "Drive" }; return s; }
const juce::StringArray& delayRates() { static const juce::StringArray s { "1/2", "1/4", "1/4D", "1/8", "1/8D", "1/8T", "1/16" }; return s; }
const juce::StringArray& presetCategories()
{
    static const juce::StringArray s { "General", "Pop", "R&B", "Hip-Hop", "Rock", "Worship & Gospel", "Speech", "Live", "Creative" };
    return s;
}

// ------------------------------------------------------------------ presets
const std::vector<FactoryPreset>& factoryPresets()
{
    // Values in real units; styles and rates by index. Anything left out
    // loads at its default, which is itself a usable vocal chain.
    static const std::vector<FactoryPreset> bank = []
    {
        std::vector<FactoryPreset> b;
        auto add = [&b](const char* name, const char* category, std::vector<std::pair<const char*, float>> v)
        { b.push_back({ name, category, std::move(v) }); };

        add("Ready To Sing", "General", {});

        add("Glass Pop Lead", "Pop", { { "dyn_style", 3 }, { "comp", 55 }, { "color", 80 }, { "deess", 45 }, { "ds_focus", 7200 },
            { "tone_style", 2 }, { "low", -1.5f }, { "mid", -1 }, { "high", 2.5f }, { "air", 5 }, { "hp", 100 },
            { "space_style", 2 }, { "reverb", 16 }, { "verb_time", 1.6f }, { "delay", 14 }, { "dly_rate", 3 }, { "dly_feedback", 25 },
            { "verb_duck", 40 }, { "dly_duck", 45 }, { "fx_style", 0 }, { "fx_amount", 25 } });
        add("Airy Chorus Stack", "Pop", { { "dyn_style", 0 }, { "comp", 45 }, { "deess", 50 },
            { "tone_style", 4 }, { "low", -3 }, { "mid", -2 }, { "high", 1 }, { "air", 7 }, { "hp", 140 },
            { "space_style", 1 }, { "reverb", 28 }, { "verb_time", 3.2f }, { "delay", 10 }, { "dly_rate", 2 }, { "dly_feedback", 35 },
            { "verb_duck", 30 }, { "fx_style", 1 }, { "fx_amount", 35 }, { "fx_rate", 0.6f } });
        add("Intimate Verse", "Pop", { { "dyn_style", 0 }, { "comp", 35 }, { "color", 60 }, { "deess", 35 },
            { "tone_style", 1 }, { "low", 1 }, { "mid", -1.5f }, { "high", 1 }, { "air", 2 },
            { "space_style", 0 }, { "reverb", 12 }, { "verb_time", 0.9f }, { "delay", 0 }, { "verb_duck", 20 } });

        add("Silk Runs", "R&B", { { "dyn_style", 0 }, { "comp", 50 }, { "color", 110 }, { "deess", 45 }, { "ds_focus", 6800 },
            { "tone_style", 4 }, { "low", -1 }, { "mid", -1 }, { "high", 1.5f }, { "air", 4 },
            { "space_style", 1 }, { "reverb", 24 }, { "verb_time", 2.8f }, { "delay", 18 }, { "dly_rate", 4 }, { "dly_feedback", 38 },
            { "verb_duck", 35 }, { "dly_duck", 40 }, { "space_tone", -20 }, { "fx_style", 1 }, { "fx_amount", 15 }, { "fx_rate", 0.4f } });
        add("Late Night Falsetto", "R&B", { { "dyn_style", 0 }, { "comp", 40 }, { "deess", 55 }, { "ds_focus", 8000 },
            { "tone_style", 4 }, { "low", -3 }, { "air", 6 }, { "hp", 150 },
            { "space_style", 4 }, { "reverb", 30 }, { "verb_time", 4.5f }, { "delay", 20 }, { "dly_rate", 1 }, { "dly_feedback", 45 },
            { "verb_duck", 45 }, { "dly_duck", 50 }, { "space_tone", -30 }, { "fx_style", 4 }, { "fx_amount", 12 }, { "fx_rate", 0.5f } });
        add("Neo Soul Warmth", "R&B", { { "dyn_style", 2 }, { "comp", 45 }, { "color", 150 },
            { "tone_style", 1 }, { "low", 2 }, { "high", -1 }, { "air", 1 }, { "lp", 14000 },
            { "space_style", 3 }, { "reverb", 18 }, { "verb_time", 1.8f }, { "delay", 10 }, { "dly_rate", 4 },
            { "fx_style", 5 }, { "fx_amount", 15 } });

        add("Hard Rap Upfront", "Hip-Hop", { { "dyn_style", 4 }, { "comp", 70 }, { "color", 120 }, { "deess", 50 }, { "gate", -50 },
            { "tone_style", 2 }, { "low", -3 }, { "mid", -1 }, { "high", 3 }, { "air", 3 }, { "hp", 120 },
            { "space_style", 0 }, { "reverb", 6 }, { "verb_time", 0.6f }, { "delay", 8 }, { "dly_rate", 3 }, { "dly_feedback", 15 },
            { "dly_duck", 70 }, { "fx_style", 0 }, { "fx_amount", 20 } });
        add("Trap Ad-Libs", "Hip-Hop", { { "dyn_style", 3 }, { "comp", 60 }, { "deess", 40 },
            { "tone_style", 3 }, { "mid", 2 }, { "high", 2 }, { "hp", 300 }, { "lp", 6000 },
            { "space_style", 2 }, { "reverb", 20 }, { "verb_time", 1.5f }, { "delay", 30 }, { "dly_rate", 5 }, { "dly_feedback", 40 },
            { "verb_duck", 30 }, { "dly_duck", 20 }, { "space_tone", 30 }, { "fx_style", 2 }, { "fx_amount", 30 } });
        add("Melodic Hook", "Hip-Hop", { { "dyn_style", 3 }, { "comp", 55 }, { "deess", 45 },
            { "tone_style", 2 }, { "low", -2 }, { "high", 2 }, { "air", 4 },
            { "space_style", 1 }, { "reverb", 22 }, { "verb_time", 2.4f }, { "delay", 20 }, { "dly_rate", 4 }, { "dly_feedback", 35 },
            { "verb_duck", 40 }, { "dly_duck", 45 }, { "fx_style", 1 }, { "fx_amount", 20 }, { "fx_rate", 0.8f } });

        add("Scream Control", "Rock", { { "dyn_style", 1 }, { "comp", 75 }, { "color", 140 }, { "deess", 35 }, { "gate", -45 },
            { "tone_style", 1 }, { "low", -2 }, { "mid", 2 }, { "high", 2 }, { "air", 1 }, { "hp", 110 }, { "lp", 16000 },
            { "space_style", 0 }, { "reverb", 10 }, { "verb_time", 1.0f }, { "delay", 12 }, { "dly_rate", 6 }, { "dly_feedback", 5 },
            { "dly_duck", 30 }, { "fx_style", 5 }, { "fx_amount", 25 } });
        add("Indie Slapback", "Rock", { { "dyn_style", 2 }, { "comp", 45 }, { "color", 130 },
            { "tone_style", 1 }, { "mid", 1.5f }, { "high", 1 }, { "air", 0 }, { "lp", 12000 },
            { "space_style", 3 }, { "reverb", 12 }, { "verb_time", 1.2f }, { "delay", 28 }, { "dly_rate", 6 }, { "dly_feedback", 8 },
            { "dly_duck", 15 }, { "fx_style", 5 }, { "fx_amount", 10 } });
        add("Arena Anthem", "Rock", { { "dyn_style", 1 }, { "comp", 60 }, { "color", 110 }, { "deess", 40 },
            { "tone_style", 2 }, { "low", -2 }, { "high", 2 }, { "air", 3 },
            { "space_style", 1 }, { "reverb", 26 }, { "verb_time", 3.0f }, { "delay", 18 }, { "dly_rate", 1 }, { "dly_feedback", 30 },
            { "verb_duck", 45 }, { "dly_duck", 50 }, { "fx_style", 0 }, { "fx_amount", 30 } });

        add("Worship Leader", "Worship & Gospel", { { "dyn_style", 0 }, { "comp", 45 }, { "deess", 40 }, { "gate", -60 },
            { "tone_style", 4 }, { "low", -2 }, { "high", 1 }, { "air", 3 }, { "hp", 100 },
            { "space_style", 1 }, { "reverb", 30 }, { "verb_time", 3.6f }, { "delay", 16 }, { "dly_rate", 2 }, { "dly_feedback", 40 },
            { "verb_duck", 40 }, { "dly_duck", 45 }, { "space_tone", -10 } });
        add("Gospel Power Lead", "Worship & Gospel", { { "dyn_style", 3 }, { "comp", 65 }, { "color", 120 }, { "deess", 45 },
            { "tone_style", 2 }, { "mid", -1 }, { "high", 2 }, { "air", 3 },
            { "space_style", 2 }, { "reverb", 22 }, { "verb_time", 2.2f }, { "delay", 10 }, { "dly_rate", 3 }, { "dly_feedback", 25 },
            { "verb_duck", 35 }, { "fx_style", 0 }, { "fx_amount", 10 } });
        add("Choir Wash", "Worship & Gospel", { { "dyn_style", 0 }, { "comp", 35 }, { "deess", 40 },
            { "tone_style", 1 }, { "low", -4 }, { "mid", -1 }, { "air", 2 }, { "hp", 160 },
            { "space_style", 4 }, { "reverb", 40 }, { "verb_time", 5.0f }, { "delay", 0 }, { "verb_duck", 20 }, { "space_tone", -20 },
            { "fx_style", 1 }, { "fx_amount", 20 }, { "fx_rate", 0.3f } });

        add("Podcast Clear", "Speech", { { "dyn_style", 4 }, { "comp", 60 }, { "color", 60 }, { "deess", 45 }, { "gate", -50 },
            { "tone_style", 0 }, { "low", -1 }, { "mid", 1 }, { "high", 2 }, { "air", 1 }, { "hp", 90 },
            { "space_on", 0 }, { "fx_on", 0 } });
        add("Live MC", "Speech", { { "dyn_style", 4 }, { "comp", 65 }, { "deess", 40 }, { "gate", -45 },
            { "tone_style", 2 }, { "low", -3 }, { "high", 2 }, { "air", 1 }, { "hp", 130 },
            { "space_style", 0 }, { "reverb", 6 }, { "verb_time", 0.7f }, { "delay", 0 }, { "fx_on", 0 } });
        add("Radio Voice", "Speech", { { "dyn_style", 4 }, { "comp", 75 }, { "color", 140 },
            { "tone_style", 3 }, { "mid", 3 }, { "high", 1 }, { "hp", 300 }, { "lp", 5000 },
            { "space_on", 0 }, { "fx_style", 5 }, { "fx_amount", 10 } });

        add("Stage Lead Safe", "Live", { { "dyn_style", 1 }, { "comp", 50 }, { "deess", 40 }, { "gate", -55 },
            { "tone_style", 0 }, { "low", -2 }, { "mid", -1 }, { "high", 1.5f }, { "air", 2 }, { "hp", 100 },
            { "space_style", 1 }, { "reverb", 16 }, { "verb_time", 1.8f }, { "delay", 10 }, { "dly_rate", 3 }, { "dly_feedback", 25 },
            { "verb_duck", 45 }, { "dly_duck", 50 } });
        add("Backing Vocal Blend", "Live", { { "dyn_style", 0 }, { "comp", 55 }, { "deess", 50 },
            { "tone_style", 4 }, { "low", -5 }, { "mid", -1 }, { "high", 0 }, { "air", 2 }, { "hp", 180 },
            { "space_style", 1 }, { "reverb", 24 }, { "verb_time", 2.2f }, { "delay", 0 }, { "fx_style", 0 }, { "fx_amount", 35 } });
        add("Loud Room Cut Through", "Live", { { "dyn_style", 3 }, { "comp", 65 }, { "color", 100 }, { "deess", 40 }, { "gate", -40 },
            { "tone_style", 2 }, { "low", -4 }, { "mid", 1 }, { "high", 3 }, { "air", 2 }, { "hp", 150 },
            { "space_style", 0 }, { "reverb", 5 }, { "verb_time", 0.5f }, { "delay", 0 }, { "fx_on", 0 } });

        add("Telephone Chorus", "Creative", { { "dyn_style", 4 }, { "comp", 60 },
            { "tone_style", 3 }, { "mid", 4 }, { "hp", 400 }, { "lp", 3500 },
            { "space_style", 3 }, { "reverb", 15 }, { "verb_time", 1.2f }, { "delay", 15 }, { "dly_rate", 3 }, { "dly_feedback", 30 },
            { "fx_style", 2 }, { "fx_amount", 45 } });
        add("Dream Haze", "Creative", { { "dyn_style", 0 }, { "comp", 40 },
            { "tone_style", 4 }, { "low", -2 }, { "air", 6 },
            { "space_style", 4 }, { "reverb", 45 }, { "verb_time", 7.0f }, { "delay", 30 }, { "dly_rate", 2 }, { "dly_feedback", 55 },
            { "verb_duck", 25 }, { "dly_duck", 25 }, { "space_tone", -40 }, { "fx_style", 4 }, { "fx_amount", 35 }, { "fx_rate", 0.4f } });
        add("Tremolo Ghost", "Creative", { { "dyn_style", 0 }, { "comp", 35 }, { "tone_style", 1 },
            { "space_style", 1 }, { "reverb", 30 }, { "verb_time", 3.0f }, { "delay", 15 }, { "dly_rate", 1 }, { "dly_feedback", 40 },
            { "fx_style", 3 }, { "fx_amount", 55 }, { "fx_rate", 5.5f } });
        add("Lo-Fi Bedroom", "Creative", { { "dyn_style", 2 }, { "comp", 50 }, { "color", 160 },
            { "tone_style", 1 }, { "low", 1 }, { "high", -2 }, { "air", 0 }, { "lp", 9000 }, { "hp", 120 },
            { "space_style", 3 }, { "reverb", 14 }, { "verb_time", 1.2f }, { "delay", 12 }, { "dly_rate", 4 }, { "dly_feedback", 30 },
            { "fx_style", 2 }, { "fx_amount", 40 } });
        add("Crushed Megaphone", "Creative", { { "dyn_style", 4 }, { "comp", 80 }, { "color", 200 },
            { "tone_style", 3 }, { "mid", 5 }, { "hp", 500 }, { "lp", 4000 },
            { "space_style", 0 }, { "reverb", 8 }, { "verb_time", 0.6f }, { "delay", 0 }, { "fx_style", 5 }, { "fx_amount", 60 } });
        add("Wide Doubled Hook", "Creative", { { "dyn_style", 3 }, { "comp", 55 }, { "deess", 45 },
            { "tone_style", 2 }, { "low", -2 }, { "high", 1.5f }, { "air", 4 },
            { "space_style", 2 }, { "reverb", 18 }, { "verb_time", 1.8f }, { "delay", 16 }, { "dly_rate", 4 }, { "dly_feedback", 30 },
            { "verb_duck", 35 }, { "dly_duck", 40 }, { "fx_style", 0 }, { "fx_amount", 60 } });
        add("Endless Throw", "Creative", { { "dyn_style", 1 }, { "comp", 50 }, { "tone_style", 0 },
            { "space_style", 1 }, { "reverb", 20 }, { "verb_time", 3.0f }, { "delay", 45 }, { "dly_rate", 1 }, { "dly_feedback", 75 },
            { "verb_duck", 20 }, { "dly_duck", 60 }, { "space_tone", -35 }, { "fx_style", 1 }, { "fx_amount", 15 } });
        return b;
    }();
    return bank;
}

// ------------------------------------------------------------------ parameters
juce::AudioProcessorValueTreeState::ParameterLayout VocalRackProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    auto range = [](float lo, float hi, float centre, float step)
    {
        NormalisableRange<float> r(lo, hi, step);
        if (centre > lo && centre < hi) r.setSkewForCentre(centre);
        return r;
    };
    auto flt = [&layout](const char* id, const char* name, NormalisableRange<float> r, float def, const char* unit)
    { layout.add(std::make_unique<AudioParameterFloat>(ParameterID { id, 1 }, name, r, def, AudioParameterFloatAttributes().withLabel(unit))); };
    auto sw = [&layout](const char* id, const char* name, bool def)
    { layout.add(std::make_unique<AudioParameterBool>(ParameterID { id, 1 }, name, def)); };
    auto menu = [&layout](const char* id, const char* name, const StringArray& items, int def)
    { layout.add(std::make_unique<AudioParameterChoice>(ParameterID { id, 1 }, name, items, def)); };

    flt("in_gain", "Input", range(-24, 24, 0, 0.1f), 0, "dB");
    flt("out_gain", "Output", range(-24, 24, 0, 0.1f), 0, "dB");
    sw("bypass", "Bypass", false);

    for (int m = 0; m < 4; ++m)
    {
        const String p(modulePrefix[m]);
        const auto title = p == "dyn" ? String("Dynamics") : p == "tone" ? String("Tone") : p == "space" ? String("Space") : String("FX");
        sw((p + "_on").toRawUTF8(), (title + " On").toRawUTF8(), true);
        sw((p + "_solo").toRawUTF8(), (title + " Solo").toRawUTF8(), false);
        flt((p + "_trim").toRawUTF8(), (title + " Trim").toRawUTF8(), range(-12, 12, 0, 0.1f), 0, "dB");
    }
    menu("dyn_style", "Dynamics Style", dynamicsStyles(), 0);
    menu("tone_style", "Tone Style", toneStyles(), 0);
    menu("space_style", "Space Style", spaceStyles(), 1);
    menu("fx_style", "FX Style", fxStyles(), 0);

    flt("gate", "Gate", range(-80, 0, 0, 0.5f), -80, "dB");
    flt("comp", "Compression", range(0, 100, 0, 1), 40, "%");
    flt("color", "Colour", range(0, 200, 0, 1), 100, "%");
    flt("deess", "De-Ess", range(0, 100, 0, 1), 30, "%");
    flt("ds_focus", "De-Ess Focus", range(2000, 12000, 6000, 10), 6500, "Hz");

    flt("low", "Low", range(-12, 12, 0, 0.1f), 0, "dB");
    flt("mid", "Mid", range(-12, 12, 0, 0.1f), 0, "dB");
    flt("high", "High", range(-12, 12, 0, 0.1f), 1.5f, "dB");
    flt("air", "Air", range(0, 12, 0, 0.1f), 2, "dB");
    flt("hp", "Low Cut", range(20, 800, 120, 1), 80, "Hz");
    flt("lp", "High Cut", range(2000, 20000, 8000, 10), 20000, "Hz");

    flt("reverb", "Reverb", range(0, 100, 0, 1), 18, "%");
    flt("verb_time", "Reverb Time", range(0.3f, 10, 2, 0.1f), 2.3f, "s");
    flt("delay", "Delay", range(0, 100, 0, 1), 12, "%");
    menu("dly_rate", "Delay Rate", delayRates(), 4);
    flt("dly_feedback", "Delay Feedback", range(0, 90, 0, 1), 30, "%");
    flt("verb_duck", "Reverb Duck", range(0, 100, 0, 1), 25, "%");
    flt("dly_duck", "Delay Duck", range(0, 100, 0, 1), 30, "%");
    flt("space_tone", "Space Tone", range(-100, 100, 0, 1), 0, "");

    flt("fx_amount", "FX Amount", range(0, 100, 0, 1), 0, "%");
    flt("fx_rate", "FX Rate", range(0.1f, 10, 1, 0.01f), 1, "Hz");
    sw("fx_post", "FX After Space", true);
    return layout;
}

/** Marks the preset as changed when anyone moves a control. */
class VocalRackProcessor::ChangeWatcher final : public juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit ChangeWatcher(VocalRackProcessor& p) : owner(p)
    {
        for (auto* parameter : owner.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter))
                if (ranged->paramID != "bypass") owner.state.addParameterListener(ranged->paramID, this);
    }
    ~ChangeWatcher() override
    {
        for (auto* parameter : owner.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter))
                owner.state.removeParameterListener(ranged->paramID, this);
    }
    void parameterChanged(const juce::String&, float) override
    {
        if (! owner.applying) owner.modified.store(true);
    }

private:
    VocalRackProcessor& owner;
};

VocalRackProcessor::VocalRackProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state(*this, &undo, "VOCAL_RACK", createLayout())
{
    watcher = std::make_unique<ChangeWatcher>(*this);
    loadFactory(0);
    undo.clearUndoHistory();
}

VocalRackProcessor::~VocalRackProcessor() = default;

bool VocalRackProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return layouts.getMainInputChannelSet() == out;
}

// ------------------------------------------------------------------ per-block values
struct VocalRackProcessor::Values
{
    float inDb, outDb;
    bool bypass;
    std::array<bool, 4> on;
    std::array<float, 4> trimDb;
    int dynStyle, toneStyle, spaceStyle, fxStyle, rateIndex;
    float gate, comp, color, deess, focus;
    float low, mid, high, air, hp, lp;
    float reverb, time, delay, feedback, verbDuck, delayDuck, spaceTone;
    float amount, fxRate;
    bool fxPost;
};

void VocalRackProcessor::readTargets(Values& v)
{
    auto get = [this](const char* id) { return state.getRawParameterValue(id)->load(); };
    auto flag = [&get](const char* id) { return get(id) > 0.5f; };
    auto index = [&get](const char* id) { return juce::roundToInt(get(id)); };

    v.inDb = get("in_gain");
    v.outDb = get("out_gain");
    v.bypass = flag("bypass");

    bool anySolo = false;
    for (int m = 0; m < 4; ++m)
        anySolo = anySolo || state.getRawParameterValue(juce::String(modulePrefix[m]) + "_solo")->load() > 0.5f;
    for (int m = 0; m < 4; ++m)
    {
        const juce::String p(modulePrefix[m]);
        const auto on = state.getRawParameterValue(p + "_on")->load() > 0.5f;
        const auto solo = state.getRawParameterValue(p + "_solo")->load() > 0.5f;
        v.on[static_cast<size_t>(m)] = on && (! anySolo || solo);
        v.trimDb[static_cast<size_t>(m)] = state.getRawParameterValue(p + "_trim")->load();
    }

    v.dynStyle = index("dyn_style");
    v.toneStyle = index("tone_style");
    v.spaceStyle = index("space_style");
    v.fxStyle = index("fx_style");
    v.rateIndex = juce::jlimit(0, 6, index("dly_rate"));
    v.gate = get("gate"); v.comp = get("comp"); v.color = get("color"); v.deess = get("deess"); v.focus = get("ds_focus");
    v.low = get("low"); v.mid = get("mid"); v.high = get("high"); v.air = get("air"); v.hp = get("hp"); v.lp = get("lp");
    v.reverb = get("reverb"); v.time = get("verb_time"); v.delay = get("delay"); v.feedback = get("dly_feedback");
    v.verbDuck = get("verb_duck"); v.delayDuck = get("dly_duck"); v.spaceTone = get("space_tone");
    v.amount = get("fx_amount"); v.fxRate = get("fx_rate"); v.fxPost = flag("fx_post");
}

void VocalRackProcessor::prepareToPlay(double sampleRate, int)
{
    rate = sampleRate;
    // Nothing looks ahead or oversamples: zero latency, for singing live.
    setLatencySamples(0);

    for (auto* s : { &inGain, &outGain, &bypassMix, &reverbMix, &delayMix, &feedback, &verbDuckAmount, &delayDuckAmount })
        s->reset(rate, 0.03);
    for (auto& s : enable) s.reset(rate, 0.03);
    for (auto& s : trim) s.reset(rate, 0.03);
    echoSamples.reset(rate, 0.2);

    gateEnvelope = compEnvelope = deessEnvelope = deessApplied = duckEnvelope = 0.0f;
    gateGain = 1.0f;
    deessDetector.reset();
    deessShelf.reset();
    deessShelfFrequency = -1.0;
    for (auto& f : toneFilters) f.reset();
    toneApplied.fill(-99999.0f);
    for (auto& line : fxLine) line.prepare(static_cast<int>(rate * 0.06) + 8);
    fxBandLimit.reset();
    fxLowCut.reset();
    fxBandApplied = -1.0f;
    for (auto& line : echo) line.prepare(static_cast<int>(rate * 4.2) + 8);
    echoFilter.reset();
    wetLow.reset();
    wetHigh.reset();
    wetToneApplied = -1000.0f;
    reverb.setSampleRate(rate);
    reverb.reset();
    reverbApplied = {};
    reverbApplied.roomSize = -1.0f;

    Values v {};
    readTargets(v);
    inGain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(v.inDb));
    outGain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(v.outDb));
    bypassMix.setCurrentAndTargetValue(v.bypass ? 1.0f : 0.0f);
    for (size_t m = 0; m < 4; ++m)
    {
        enable[m].setCurrentAndTargetValue(v.on[m] ? 1.0f : 0.0f);
        trim[m].setCurrentAndTargetValue(juce::Decibels::decibelsToGain(v.trimDb[m]));
    }
    echoSamples.setCurrentAndTargetValue(static_cast<float>(rate * 0.375));
}

void VocalRackProcessor::pushAnalyser(const float* left, const float* right, int count) noexcept
{
    count = juce::jmin(count, analyserFifo.getFreeSpace());
    if (count <= 0) return;
    const auto scope = analyserFifo.write(count);
    auto copy = [&](int start, int size, int offset)
    {
        for (int i = 0; i < size; ++i)
            analyserBuffer[static_cast<size_t>(start + i)] = 0.5f * (left[offset + i] + (right != nullptr ? right[offset + i] : left[offset + i]));
    };
    copy(scope.startIndex1, scope.blockSize1, 0);
    copy(scope.startIndex2, scope.blockSize2, scope.blockSize1);
}

void VocalRackProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const auto samples = buffer.getNumSamples();
    for (int c = getTotalNumInputChannels(); c < buffer.getNumChannels(); ++c) buffer.clear(c, 0, samples);
    const auto channels = juce::jmin(2, buffer.getNumChannels());
    if (channels == 0 || samples == 0) return;

    Values v {};
    readTargets(v);

    inGain.setTargetValue(juce::Decibels::decibelsToGain(v.inDb));
    outGain.setTargetValue(juce::Decibels::decibelsToGain(v.outDb));
    bypassMix.setTargetValue(v.bypass ? 1.0f : 0.0f);
    for (size_t m = 0; m < 4; ++m)
    {
        enable[m].setTargetValue(v.on[m] ? 1.0f : 0.0f);
        trim[m].setTargetValue(juce::Decibels::decibelsToGain(v.trimDb[m]));
    }

    // ---- dynamics settings
    struct DynVoice { float ratio, attack, release, knee, saturation, makeup; };
    static constexpr DynVoice dynVoices[] { { 3.0f, 12.0f, 180.0f, 8.0f, 0.0f, 0.6f }, { 4.0f, 3.0f, 70.0f, 4.0f, 0.1f, 0.7f },
                                            { 4.0f, 10.0f, 250.0f, 10.0f, 0.35f, 0.65f }, { 6.0f, 1.5f, 90.0f, 6.0f, 0.2f, 0.8f },
                                            { 8.0f, 2.0f, 120.0f, 6.0f, 0.1f, 0.9f } };
    const auto& dyn = dynVoices[juce::jlimit(0, 4, v.dynStyle)];
    auto coefficient = [this](float ms) { return static_cast<float>(std::exp(-1.0 / (juce::jmax(0.05f, ms) * 0.001 * rate))); };
    const auto compAttack = coefficient(dyn.attack), compRelease = coefficient(dyn.release);
    const auto compThreshold = -4.0f - v.comp * 0.32f;
    const auto compSlope = 1.0f - 1.0f / dyn.ratio;
    const auto compMakeup = v.comp * 0.12f * dyn.makeup;
    const auto colourMix = juce::jlimit(0.0f, 1.0f, v.color / 200.0f);
    const auto colourDrive = 1.0f + (v.color / 100.0f) * (1.0f + 3.0f * dyn.saturation);
    const auto gateOff = v.gate <= -79.5f;
    const auto gateOpenCoef = coefficient(1.0f), gateCloseCoef = coefficient(60.0f);
    const auto detectAttack = coefficient(1.0f), detectRelease = coefficient(80.0f);
    const auto deessThreshold = -20.0f - v.deess * 0.25f;
    const auto deessLimit = v.deess * 0.15f;
    deessDetector.bandPass(rate, v.focus, 1.4);

    // ---- tone settings
    const auto& tv = toneVoice(v.toneStyle);
    const std::array<float, 10> toneWanted { static_cast<float>(v.toneStyle), v.low, v.mid, v.high, v.air,
                                             juce::jmax(v.hp, tv.hpMin), juce::jmin(v.lp, tv.lpMax), 0, 0, 0 };
    if (toneWanted != toneApplied)
    {
        toneFilters[0].highPass(rate, toneWanted[5]);
        toneFilters[1].lowShelf(rate, tv.low, v.low);
        toneFilters[2].peak(rate, tv.mid, 0.8, v.mid);
        toneFilters[3].peak(rate, tv.high, 0.9, v.high);
        toneFilters[4].highShelf(rate, tv.air, v.air);
        toneFilters[5].lowPass(rate, toneWanted[6]);
        toneApplied = toneWanted;
    }
    const auto toneDrive = 1.0f + tv.saturation * 3.0f;

    // ---- fx settings
    const auto fxAmount = v.amount * 0.01f;
    const auto fxStyle = juce::jlimit(0, 5, v.fxStyle);
    if (fxStyle == 2 && std::abs(fxAmount - fxBandApplied) > 0.005f)
    {
        fxBandLimit.lowPass(rate, 7000.0 - 4500.0 * fxAmount);
        fxLowCut.highPass(rate, 60.0 + 300.0 * fxAmount);
        fxBandApplied = fxAmount;
    }
    const auto phaseStep = 2.0 * pi * v.fxRate / rate;
    const auto msToSamples = static_cast<float>(rate * 0.001);
    float fxPeak = 0.0f;

    auto processFx = [&](float& l, float& r)
    {
        const auto inL = l, inR = r;
        float wetL = l, wetR = r, mix = fxAmount;
        switch (fxStyle)
        {
            case 0:   // doubler: two short, drifting voices spread wide
            {
                fxLine[0].push(0.5f * (l + r));
                fxLine[1].push(0.5f * (l + r));
                fxPhase += 2.0 * pi * 0.31 / rate;
                fxPhase2 += 2.0 * pi * 0.37 / rate;
                const auto a = fxLine[0].read((11.0 + 1.5 * std::sin(fxPhase)) * msToSamples);
                const auto b = fxLine[1].read((17.0 + 1.5 * std::sin(fxPhase2)) * msToSamples);
                const auto m = fxAmount * 0.7f;
                wetL = (l + m * a) / (1.0f + 0.5f * m);
                wetR = (r + m * b) / (1.0f + 0.5f * m);
                mix = 1.0f;
                break;
            }
            case 1:   // chorus
            {
                fxLine[0].push(l);
                fxLine[1].push(r);
                fxPhase += phaseStep;
                const auto depth = 2.5 * msToSamples;
                wetL = fxLine[0].read(7.0 * msToSamples + depth * (1.0 + std::sin(fxPhase)));
                wetR = fxLine[1].read(7.0 * msToSamples + depth * (1.0 + std::cos(fxPhase)));
                mix = fxAmount * 0.6f;
                break;
            }
            case 2:   // lo-fi: fewer bits, lower rate, narrower band
            {
                const auto hold = 1 + juce::roundToInt(fxAmount * 14.0f);
                if (++holdCounter >= hold) { holdCounter = 0; holdL = l; holdR = r; }
                const auto steps = std::pow(2.0f, 16.0f - fxAmount * 11.0f);
                wetL = std::round(holdL * steps) / steps;
                wetR = std::round(holdR * steps) / steps;
                wetL = fxLowCut.process(fxBandLimit.process(wetL, 0), 0);
                wetR = fxLowCut.process(fxBandLimit.process(wetR, 1), 1);
                break;
            }
            case 3:   // tremolo
            {
                fxPhase += phaseStep;
                wetL = l * (1.0f - fxAmount * static_cast<float>(0.5 + 0.5 * std::sin(fxPhase)));
                wetR = r * (1.0f - fxAmount * static_cast<float>(0.5 + 0.5 * std::sin(fxPhase + 0.6)));
                mix = 1.0f;
                break;
            }
            case 4:   // wobble: slow pitch drift like worn tape
            {
                fxLine[0].push(l);
                fxLine[1].push(r);
                fxPhase += phaseStep * 0.3;
                const auto depth = (0.5 + 3.0 * fxAmount) * msToSamples;
                wetL = fxLine[0].read(5.0 * msToSamples + depth * (1.0 + std::sin(fxPhase)));
                wetR = fxLine[1].read(5.0 * msToSamples + depth * (1.0 + std::sin(fxPhase + 0.3)));
                mix = juce::jmin(1.0f, fxAmount * 4.0f);
                break;
            }
            default:  // drive
            {
                const auto drive = 1.0f + fxAmount * 8.0f;
                const auto makeup = 1.0f / std::sqrt(drive) * 1.6f;
                wetL = std::tanh(l * drive) * makeup;
                wetR = std::tanh(r * drive) * makeup;
                mix = juce::jmin(1.0f, fxAmount * 3.0f);
                break;
            }
        }
        l = inL + mix * (wetL - inL);
        r = inR + mix * (wetR - inR);
        fxPeak = juce::jmax(fxPeak, std::abs(l - inL), std::abs(r - inR));
    };

    // ---- space settings
    struct SpaceVoice { float scale, damping, width; };
    static constexpr SpaceVoice spaceVoices[] { { 0.55f, 0.5f, 0.7f }, { 0.85f, 0.35f, 1.0f }, { 0.78f, 0.15f, 1.0f },
                                                { 0.70f, 0.45f, 0.9f }, { 0.95f, 0.30f, 1.0f } };
    const auto& sv = spaceVoices[juce::jlimit(0, 4, v.spaceStyle)];
    juce::Reverb::Parameters params;
    const auto timeNorm = static_cast<float>(std::log(v.time / 0.3) / std::log(10.0 / 0.3));
    params.roomSize = juce::jlimit(0.05f, 0.98f, sv.scale * (0.35f + 0.65f * timeNorm));
    params.damping = sv.damping;
    params.width = sv.width;
    params.wetLevel = 0.33f;
    params.dryLevel = 0.0f;
    params.freezeMode = 0.0f;
    if (params.roomSize != reverbApplied.roomSize || params.damping != reverbApplied.damping || params.width != reverbApplied.width)
    {
        reverb.setParameters(params);
        reverbApplied = params;
    }
    if (v.spaceTone != wetToneApplied)
    {
        wetLow.lowPass(rate, 18000.0 * std::pow(0.12, juce::jmax(0.0f, -v.spaceTone) / 100.0));
        wetHigh.highPass(rate, 40.0 * std::pow(15.0, juce::jmax(0.0f, v.spaceTone) / 100.0));
        echoFilter.lowPass(rate, 7000.0 * std::pow(0.4, juce::jmax(0.0f, -v.spaceTone) / 100.0));
        wetToneApplied = v.spaceTone;
    }
    reverbMix.setTargetValue(v.reverb * 0.01f);
    delayMix.setTargetValue(v.delay * 0.01f);
    feedback.setTargetValue(v.feedback * 0.01f);
    verbDuckAmount.setTargetValue(v.verbDuck * 0.01f);
    delayDuckAmount.setTargetValue(v.delayDuck * 0.01f);
    double bpm = 120.0;
    if (auto* head = getPlayHead())
        if (auto position = head->getPosition())
            if (auto tempo = position->getBpm()) bpm = juce::jlimit(20.0, 400.0, *tempo);
    echoSamples.setTargetValue(static_cast<float>(juce::jlimit(1.0, rate * 4.0, rateBeats[v.rateIndex] * 60.0 / bpm * rate)));
    const auto duckAttack = coefficient(5.0f), duckRelease = coefficient(250.0f);

    auto* left = buffer.getWritePointer(0);
    auto* right = channels > 1 ? buffer.getWritePointer(1) : nullptr;

    float inPeak = 0.0f, outPeak = 0.0f, reductionPeak = 0.0f, deessPeak = 0.0f, gateLevelDb = -90.0f;
    float verbIn = 0.0f, verbOutPeak = 0.0f, verbDuckPeak = 0.0f, delayIn = 0.0f, delayOutPeak = 0.0f, delayDuckPeak = 0.0f;

    for (int start = 0; start < samples; start += chunk)
    {
        const auto count = juce::jmin(chunk, samples - start);

        // The de-esser acts as a shelf whose depth follows the sibilance.
        const auto shelfFrequency = static_cast<double>(v.focus) * 0.75;
        if (std::abs(deessApplied) > 0.01f || deessShelfFrequency != shelfFrequency)
        {
            deessShelf.highShelf(rate, shelfFrequency, -deessApplied);
            deessShelfFrequency = shelfFrequency;
        }
        float chunkDeess = 0.0f;

        for (int i = 0; i < count; ++i)
        {
            const auto k = static_cast<size_t>(i);
            const auto index = start + i;
            auto l = left[index];
            auto r = right != nullptr ? right[index] : l;
            dryL[k] = l;
            dryR[k] = r;

            const auto gin = inGain.getNextValue();
            l *= gin;
            r *= gin;
            inPeak = juce::jmax(inPeak, std::abs(l), std::abs(r));

            // ---------------- dynamics
            {
                const auto level = juce::jmax(std::abs(l), std::abs(r));
                gateEnvelope = level > gateEnvelope ? detectAttack * gateEnvelope + (1 - detectAttack) * level
                                                    : detectRelease * gateEnvelope + (1 - detectRelease) * level;
                const auto levelDb = juce::Decibels::gainToDecibels(gateEnvelope, -90.0f);
                gateLevelDb = juce::jmax(gateLevelDb, levelDb);
                const auto target = gateOff || levelDb > v.gate ? 1.0f : 0.0f;
                gateGain = target > gateGain ? gateOpenCoef * gateGain + (1 - gateOpenCoef) * target
                                             : gateCloseCoef * gateGain + (1 - gateCloseCoef) * target;
                const auto gated = 0.01f + 0.99f * gateGain;

                auto dl = l * gated, dr = r * gated;
                const auto peak = juce::Decibels::gainToDecibels(juce::jmax(std::abs(dl), std::abs(dr)), -120.0f);
                const auto over = peak - compThreshold;
                const auto halfKnee = dyn.knee * 0.5f;
                const auto wanted = over <= -halfKnee ? 0.0f
                                  : over >= halfKnee ? over * compSlope
                                                     : compSlope * (over + halfKnee) * (over + halfKnee) / (2.0f * dyn.knee);
                compEnvelope = wanted > compEnvelope ? compAttack * compEnvelope + (1 - compAttack) * wanted
                                                     : compRelease * compEnvelope + (1 - compRelease) * wanted;
                const auto gain = juce::Decibels::decibelsToGain(compMakeup - compEnvelope);
                dl *= gain;
                dr *= gain;

                dl += colourMix * (softClip(dl, colourDrive) - dl);
                dr += colourMix * (softClip(dr, colourDrive) - dr);

                const auto sibilance = std::abs(deessDetector.process(0.5f * (dl + dr), 0));
                deessEnvelope = sibilance > deessEnvelope ? detectAttack * deessEnvelope + (1 - detectAttack) * sibilance
                                                          : detectRelease * deessEnvelope + (1 - detectRelease) * sibilance;
                const auto sDb = juce::Decibels::gainToDecibels(deessEnvelope, -120.0f);
                chunkDeess = juce::jmax(chunkDeess, juce::jlimit(0.0f, deessLimit, (sDb - deessThreshold) * 0.6f));
                dl = deessShelf.process(dl, 0);
                dr = deessShelf.process(dr, 1);

                const auto e = enable[dynamics].getNextValue();
                const auto t = trim[dynamics].getNextValue();
                l += e * (dl * t - l);
                r += e * (dr * t - r);
                if (e > 0.5f) reductionPeak = juce::jmax(reductionPeak, compEnvelope);
                if (e > 0.5f) deessPeak = juce::jmax(deessPeak, deessApplied);
            }

            // ---------------- tone
            {
                auto tl = l, tr = r;
                for (auto& f : toneFilters) { tl = f.process(tl, 0); tr = f.process(tr, 1); }
                tl = softClip(tl, toneDrive);
                tr = softClip(tr, toneDrive);
                const auto e = enable[tone].getNextValue();
                const auto t = trim[tone].getNextValue();
                l += e * (tl * t - l);
                r += e * (tr * t - r);
            }

            // ---------------- fx before the space
            const auto fxOn = enable[fx].getNextValue();
            const auto fxTrim = trim[fx].getNextValue();
            if (! v.fxPost)
            {
                auto fl = l, fr = r;
                processFx(fl, fr);
                l += fxOn * (fl * fxTrim - l);
                r += fxOn * (fr * fxTrim - r);
            }

            // ---------------- space sends
            const auto spaceOn = enable[space].getNextValue();
            spaceEnable[k] = spaceOn;
            spaceDryL[k] = l;
            spaceDryR[k] = r;

            const auto level = juce::jmax(std::abs(l), std::abs(r));
            duckEnvelope = level > duckEnvelope ? duckAttack * duckEnvelope + (1 - duckAttack) * level
                                                : duckRelease * duckEnvelope + (1 - duckRelease) * level;
            const auto loudness = juce::jlimit(0.0f, 1.0f, (juce::Decibels::gainToDecibels(duckEnvelope, -90.0f) + 40.0f) / 34.0f);
            const auto verbDuck = verbDuckAmount.getNextValue() * loudness;
            const auto delayDuck = delayDuckAmount.getNextValue() * loudness;
            verbDuckPeak = juce::jmax(verbDuckPeak, verbDuck);
            delayDuckPeak = juce::jmax(delayDuckPeak, delayDuck);

            revL[k] = l * spaceOn;
            revR[k] = r * spaceOn;
            revGain[k] = reverbMix.getNextValue() * (1.0f - verbDuck);
            verbIn = juce::jmax(verbIn, std::abs(revL[k]) * reverbMix.getCurrentValue());

            const auto time = echoSamples.getNextValue();
            const auto el = echoFilter.process(echo[0].read(time), 0);
            const auto er = echoFilter.process(echo[1].read(time), 1);
            const auto fb = feedback.getNextValue();
            const auto send = 0.5f * (l + r) * spaceOn;
            echo[0].push(send + fb * er);
            echo[1].push(fb * el + 0.35f * send);
            echoL[k] = el;
            echoR[k] = er;
            echoGain[k] = delayMix.getNextValue() * (1.0f - delayDuck);
            delayIn = juce::jmax(delayIn, std::abs(send) * delayMix.getCurrentValue());
        }

        deessApplied += (chunkDeess - deessApplied) * (chunkDeess > deessApplied ? 0.6f : 0.15f);

        reverb.processStereo(revL.data(), revR.data(), count);

        for (int i = 0; i < count; ++i)
        {
            const auto k = static_cast<size_t>(i);
            const auto index = start + i;
            const auto wl = wetHigh.process(wetLow.process(revL[k], 0), 0) * revGain[k];
            const auto wr = wetHigh.process(wetLow.process(revR[k], 1), 1) * revGain[k];
            const auto dl = echoL[k] * echoGain[k], dr = echoR[k] * echoGain[k];
            verbOutPeak = juce::jmax(verbOutPeak, std::abs(wl), std::abs(wr));
            delayOutPeak = juce::jmax(delayOutPeak, std::abs(dl), std::abs(dr));

            const auto spaceTrim = trim[space].getNextValue();
            const auto e = spaceEnable[k];
            auto l = spaceDryL[k] + e * ((spaceDryL[k] + wl + dl) * spaceTrim - spaceDryL[k]);
            auto r = spaceDryR[k] + e * ((spaceDryR[k] + wr + dr) * spaceTrim - spaceDryR[k]);

            if (v.fxPost)
            {
                auto fl = l, fr = r;
                processFx(fl, fr);
                // The fx enable and trim were advanced in the first pass; read their current values.
                const auto fxOn = enable[fx].getCurrentValue();
                const auto fxTrim = trim[fx].getCurrentValue();
                l += fxOn * (fl * fxTrim - l);
                r += fxOn * (fr * fxTrim - r);
            }

            const auto g = outGain.getNextValue();
            l *= g;
            r *= g;

            const auto b = bypassMix.getNextValue();
            l += b * (dryL[k] - l);
            r += b * (dryR[k] - r);

            if (right != nullptr) { left[index] = l; right[index] = r; }
            else left[index] = 0.5f * (l + r);
            outPeak = juce::jmax(outPeak, std::abs(l), std::abs(r));
        }
    }

    if (analyserWanted.load(std::memory_order_relaxed))
        pushAnalyser(buffer.getReadPointer(0), right != nullptr ? buffer.getReadPointer(1) : nullptr, samples);

    meters.input.store(juce::jmax(meters.input.load(), inPeak));
    meters.output.store(juce::jmax(meters.output.load(), outPeak));
    meters.gateLevel.store(gateLevelDb);
    meters.gateOpen.store(gateGain);
    meters.compReduction.store(reductionPeak);
    meters.deessReduction.store(deessPeak);
    meters.verbSend.store(verbIn);
    meters.verbOut.store(verbOutPeak);
    meters.verbDuck.store(verbDuckPeak);
    meters.delaySend.store(delayIn);
    meters.delayOut.store(delayOutPeak);
    meters.delayDuck.store(delayDuckPeak);
    meters.fxActivity.store(fxPeak);
}

// ------------------------------------------------------------------ presets and state
void VocalRackProcessor::applyValues(const std::vector<std::pair<const char*, float>>& values)
{
    const juce::ScopedValueSetter<bool> guard(applying, true);
    for (auto* parameter : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(parameter))
            if (ranged->paramID != "bypass") ranged->setValueNotifyingHost(ranged->getDefaultValue());
    for (const auto& [id, value] : values)
        if (auto* parameter = state.getParameter(id))
            parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
        else
            jassertfalse;
    modified.store(false);
}

void VocalRackProcessor::loadFactory(int index)
{
    const auto& bank = factoryPresets();
    if (! juce::isPositiveAndBelow(index, static_cast<int>(bank.size()))) return;
    undo.beginNewTransaction("Load preset");
    currentFactory = index;
    currentName = bank[static_cast<size_t>(index)].name;
    applyValues(bank[static_cast<size_t>(index)].values);
}

void VocalRackProcessor::stepPreset(int delta)
{
    const auto count = static_cast<int>(factoryPresets().size());
    loadFactory(((juce::jmax(0, currentFactory) + delta) % count + count) % count);
}

const juce::String VocalRackProcessor::getProgramName(int index)
{
    const auto& bank = factoryPresets();
    return juce::isPositiveAndBelow(index, static_cast<int>(bank.size())) ? bank[static_cast<size_t>(index)].name : juce::String();
}

juce::File VocalRackProcessor::userPresetFolder()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
               .getChildFile("Amanorsac Studio").getChildFile("Presets").getChildFile("VOCAL RACK");
}

juce::Array<juce::File> VocalRackProcessor::userPresets()
{
    auto files = userPresetFolder().findChildFiles(juce::File::findFiles, false, "*.amanorsacpreset");
    std::sort(files.begin(), files.end(), [](const juce::File& a, const juce::File& b)
              { return a.getFileName().compareIgnoreCase(b.getFileName()) < 0; });
    return files;
}

bool VocalRackProcessor::saveUserPreset(const juce::String& name)
{
    const auto clean = juce::File::createLegalFileName(name.trim());
    if (clean.isEmpty()) return false;
    const auto folder = userPresetFolder();
    if (! folder.createDirectory()) return false;
    const auto xml = state.copyState().createXml();
    if (xml == nullptr || ! xml->writeTo(folder.getChildFile(clean + ".amanorsacpreset"))) return false;
    currentName = clean;
    currentFactory = -1;
    modified.store(false);
    return true;
}

bool VocalRackProcessor::loadUserPreset(const juce::File& file)
{
    const auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr || ! xml->hasTagName(state.state.getType())) return false;
    const juce::ScopedValueSetter<bool> guard(applying, true);
    undo.beginNewTransaction("Load preset");
    state.replaceState(juce::ValueTree::fromXml(*xml));
    currentName = file.getFileNameWithoutExtension();
    currentFactory = -1;
    modified.store(false);
    return true;
}

void VocalRackProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    auto tree = state.copyState();
    tree.setProperty("preset", currentName, nullptr);
    tree.setProperty("factory", currentFactory, nullptr);
    if (const auto xml = tree.createXml()) copyXmlToBinary(*xml, destination);
}

void VocalRackProcessor::setStateInformation(const void* data, int size)
{
    const auto xml = getXmlFromBinary(data, size);
    if (xml == nullptr || ! xml->hasTagName(state.state.getType())) return;
    const auto tree = juce::ValueTree::fromXml(*xml);
    const juce::ScopedValueSetter<bool> guard(applying, true);
    currentName = tree.getProperty("preset", currentName).toString();
    currentFactory = static_cast<int>(tree.getProperty("factory", -1));
    state.replaceState(tree);
    modified.store(false);
}

juce::AudioProcessorEditor* VocalRackProcessor::createEditor() { return new VocalRackEditor(*this); }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new amanorsac::vocalrack::VocalRackProcessor(); }
