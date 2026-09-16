#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <vector>

namespace amanorsac::vocalrack
{
/** RBJ biquad, transposed direct form II, plain doubles so coefficients can
    change on the audio thread without allocating. */
struct Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double z1[2] {}, z2[2] {};

    void reset() noexcept { z1[0] = z1[1] = z2[0] = z2[1] = 0.0; }
    float process(float x, int channel) noexcept
    {
        const double y = b0 * x + z1[channel];
        z1[channel] = b1 * x - a1 * y + z2[channel];
        z2[channel] = b2 * x - a2 * y;
        return static_cast<float>(y);
    }
    void peak(double rate, double f, double q, double db) noexcept;
    void lowShelf(double rate, double f, double db) noexcept;
    void highShelf(double rate, double f, double db) noexcept;
    void lowPass(double rate, double f, double q = 0.7071) noexcept;
    void highPass(double rate, double f, double q = 0.7071) noexcept;
    void bandPass(double rate, double f, double q) noexcept;
    [[nodiscard]] double magnitude(double rate, double f) const noexcept;
};

/** Fractional circular delay, sized once. */
struct DelayLine
{
    std::vector<float> data;
    int write = 0;

    void prepare(int length) { data.assign(static_cast<size_t>(juce::jmax(8, length)), 0.0f); write = 0; }
    void clear() noexcept { std::fill(data.begin(), data.end(), 0.0f); }
    void push(float x) noexcept
    {
        data[static_cast<size_t>(write)] = x;
        if (++write == static_cast<int>(data.size())) write = 0;
    }
    [[nodiscard]] float read(double delay) const noexcept
    {
        const auto size = static_cast<int>(data.size());
        delay = juce::jlimit(1.0, static_cast<double>(size - 2), delay);
        auto position = static_cast<double>(write) - delay;
        if (position < 0.0) position += size;
        const auto i0 = static_cast<int>(position);
        const auto i1 = i0 + 1 == size ? 0 : i0 + 1;
        const auto frac = static_cast<float>(position - i0);
        return data[static_cast<size_t>(i0)] + frac * (data[static_cast<size_t>(i1)] - data[static_cast<size_t>(i0)]);
    }
};

struct FactoryPreset
{
    juce::String name, category;
    std::vector<std::pair<const char*, float>> values;
};

const std::vector<FactoryPreset>& factoryPresets();
const juce::StringArray& presetCategories();

const juce::StringArray& dynamicsStyles();
const juce::StringArray& toneStyles();
const juce::StringArray& spaceStyles();
const juce::StringArray& fxStyles();
const juce::StringArray& delayRates();

/** Where each tone character puts its low, mid, high and air bands, and the
    cut limits it enforces. Shared by the engine and the analyser. */
struct ToneVoice { float low, mid, high, air, hpMin, lpMax, saturation; };
const ToneVoice& toneVoice(int style);

/** The four modules, in signal order. */
enum Module { dynamics = 0, tone, space, fx };
inline constexpr const char* modulePrefix[4] { "dyn", "tone", "space", "fx" };

class VocalRackProcessor final : public juce::AudioProcessor
{
public:
    VocalRackProcessor();
    ~VocalRackProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "VOCAL RACK"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 12.0; }

    juce::AudioProcessorParameter* getBypassParameter() const override { return state.getParameter("bypass"); }

    int getNumPrograms() override { return static_cast<int>(factoryPresets().size()); }
    int getCurrentProgram() override { return juce::jmax(0, currentFactory); }
    void setCurrentProgram(int index) override { loadFactory(index); }
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // presets --------------------------------------------------------------
    void loadFactory(int index);
    void stepPreset(int delta);
    [[nodiscard]] juce::String presetName() const { return currentName; }
    [[nodiscard]] bool presetModified() const { return modified.load(); }
    static juce::File userPresetFolder();
    [[nodiscard]] static juce::Array<juce::File> userPresets();
    bool saveUserPreset(const juce::String& name);
    bool loadUserPreset(const juce::File&);

    // meters, read by the window -------------------------------------------
    struct Meters
    {
        std::atomic<float> input { 0.0f }, output { 0.0f };          // peak, linear
        std::atomic<float> gateLevel { -90.0f };                      // detector, dB
        std::atomic<float> gateOpen { 1.0f };                         // 0..1
        std::atomic<float> compReduction { 0.0f }, deessReduction { 0.0f };   // dB, positive
        std::atomic<float> verbSend { 0.0f }, verbOut { 0.0f }, verbDuck { 0.0f };
        std::atomic<float> delaySend { 0.0f }, delayOut { 0.0f }, delayDuck { 0.0f };
        std::atomic<float> fxActivity { 0.0f };
    } meters;

    /** Mono feed of the output for the tone analyser. */
    juce::AbstractFifo analyserFifo { 1 << 14 };
    std::vector<float> analyserBuffer = std::vector<float>(static_cast<size_t>(1 << 14), 0.0f);
    std::atomic<bool> analyserWanted { false };

    juce::UndoManager undo;
    juce::AudioProcessorValueTreeState state;

private:
    struct Values;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void applyValues(const std::vector<std::pair<const char*, float>>&);
    void readTargets(Values&);
    void pushAnalyser(const float* left, const float* right, int count) noexcept;

    class ChangeWatcher;
    std::unique_ptr<ChangeWatcher> watcher;

    double rate = 48000.0;
    static constexpr int chunk = 32;

    // shared state for the modules
    juce::SmoothedValue<float> inGain, outGain, bypassMix;
    std::array<juce::SmoothedValue<float>, 4> enable, trim;

    // dynamics
    float gateEnvelope = 0.0f, gateGain = 1.0f, compEnvelope = 0.0f, deessEnvelope = 0.0f, deessApplied = 0.0f;
    Biquad deessDetector, deessShelf;
    double deessShelfFrequency = -1.0;

    // tone
    std::array<Biquad, 6> toneFilters;   // hp, low, mid, high, air, lp
    std::array<float, 10> toneApplied {};

    // fx
    DelayLine fxLine[2];
    double fxPhase = 0.0, fxPhase2 = 0.0;
    float holdL = 0.0f, holdR = 0.0f;
    int holdCounter = 0;
    Biquad fxBandLimit, fxLowCut;
    float fxBandApplied = -1.0f;

    // space
    juce::Reverb reverb;
    juce::Reverb::Parameters reverbApplied;
    DelayLine echo[2];
    Biquad echoFilter, wetLow, wetHigh;
    float wetToneApplied = -1000.0f;
    juce::SmoothedValue<float> echoSamples, reverbMix, delayMix, feedback, verbDuckAmount, delayDuckAmount;
    float duckEnvelope = 0.0f;

    std::array<float, chunk> dryL {}, dryR {}, revL {}, revR {}, revGain {}, spaceDryL {}, spaceDryR {},
                             echoL {}, echoR {}, echoGain {}, spaceEnable {};

    int currentFactory = 0;
    juce::String currentName;
    std::atomic<bool> modified { false };
    bool applying = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VocalRackProcessor)
};
}
