#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "DynamicsPrimitives.h"

namespace amanorsac
{
class AnchorDSP
{
public:
    void prepare(double newSampleRate, int maximumBlockSize, int channels);
    void reset();
    void process(juce::AudioBuffer<float>&, const juce::AudioProcessorValueTreeState&, const juce::String& pluginId);

    /** Samples of delay the current settings introduce, for host reporting. */
    [[nodiscard]] int latencySamples() const noexcept { return heritageLatency; }
    /** Deepest gain reduction (dB, <= 0) applied by the analog compressors in the last block. */
    [[nodiscard]] float gainReductionDb() const noexcept { return analogGainReduction.load(); }

    /** The gain a dynamic band is applying right now, in dB, for the display
        (PRISM dynamic bands, FLUX bands, SPECTRA bands). Zero when idle. */
    [[nodiscard]] float bandActivityDb(int index) const noexcept
    {
        return juce::isPositiveAndBelow(index, static_cast<int>(bandActivity.size()))
                   ? bandActivity[static_cast<size_t>(index)].load(std::memory_order_relaxed) : 0.0f;
    }

    /** Reads its parameters under this prefix, so several engines can share one
        parameter tree (the rack: "A01.", "A02.", ...). Empty for a plugin. */
    void setParameterPrefix(juce::String prefix) { parameterPrefix = std::move(prefix); }

    /** The host tempo, for the tempo-synced delay. Called once per block. */
    void setHostTempo(double bpm) noexcept { hostBpm = bpm; }

private:
    std::array<std::atomic<float>, 24> bandActivity {};

    struct Biquad
    {
        void reset() noexcept;
        void setPeak(double sampleRate, float frequency, float q, float gainLinear) noexcept;
        void setBandPass(double sampleRate, float frequency, float q) noexcept;
        void setNotch(double sampleRate, float frequency, float q) noexcept;
        void setLowPass(double sampleRate, float frequency, float q = 0.70710678f) noexcept;
        void setHighPass(double sampleRate, float frequency, float q = 0.70710678f) noexcept;
        void setLowShelf(double sampleRate, float frequency, float gainLinear) noexcept;
        void setHighShelf(double sampleRate, float frequency, float gainLinear) noexcept;
        /** RBJ shelves with an explicit slope: 1.0 is the steepest monotonic
            curve, lower softens the transition, higher adds a resonant corner. */
        void setLowShelf(double sampleRate, float frequency, float gainLinear, float slope) noexcept;
        void setHighShelf(double sampleRate, float frequency, float gainLinear, float slope) noexcept;
        void setFirstOrderLowPass(double sampleRate, float frequency) noexcept;
        void setFirstOrderHighPass(double sampleRate, float frequency) noexcept;
        void setIdentity() noexcept;
        float process(float input) noexcept;

        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
        float a1 = 0.0f, a2 = 0.0f;
        float z1 = 0.0f, z2 = 0.0f;
    };

    static constexpr size_t maxChannels = 2;
    static constexpr size_t heritageBands = 9;
    static constexpr size_t prismBands = 24;
    static constexpr size_t fluxBands = 12;
    static constexpr size_t spectraBands = 6;
    static constexpr size_t spectraCrossovers = spectraBands - 1;
    static constexpr size_t crossoverStages = 2;
    static constexpr size_t spectraLinearTaps = 63;

    std::array<std::array<Biquad, heritageBands>, maxChannels> heritageFilters;
    std::array<float, maxChannels> heritagePrevious {};
    juce::LinearSmoothedValue<float> heritageInputGain { 1.0f }, heritageOutputGain { 1.0f };
    juce::LinearSmoothedValue<float> heritageCompensation { 1.0f };   // auto gain, wet path only
    juce::LinearSmoothedValue<float> heritageMix { 1.0f }, heritageBypass { 0.0f };
    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, 2> heritageOversampling;
    int heritageOversampleChoice = -1;
    int heritageLatency = 0;
    juce::String parameterPrefix;
    bool heritagePrimed = false;   // smoothers jump to their first targets instead of ramping from silence
    static constexpr size_t heritageDryCapacity = 1024;
    std::array<std::vector<float>, maxChannels> heritageDryDelay;
    std::array<size_t, maxChannels> heritageDryPosition {};
    std::array<std::array<Biquad, prismBands>, maxChannels> prismFilters;
    std::array<std::array<Biquad, prismBands>, maxChannels> prismDetectors;
    std::array<std::array<EnvelopeFollower, maxChannels>, prismBands> prismEnvelopes;
    std::array<std::array<Biquad, fluxBands>, maxChannels> fluxFilters;
    std::array<std::array<Biquad, fluxBands>, maxChannels> fluxDetectors;
    std::array<std::array<EnvelopeFollower, maxChannels>, fluxBands> fluxEnvelopes;
    std::array<std::array<std::array<Biquad, crossoverStages>, spectraCrossovers>, maxChannels> spectraLowPass;
    std::array<std::array<std::array<Biquad, crossoverStages>, spectraCrossovers>, maxChannels> spectraHighPass;
    std::array<std::array<EnvelopeFollower, maxChannels>, spectraBands> spectraEnvelopes;
    std::array<std::array<float, spectraLinearTaps>, maxChannels> spectraLinearHistory {};
    std::array<size_t, maxChannels> spectraLinearPosition {};
    std::array<std::array<Biquad, 3>, maxChannels> ironFilters;
    std::array<std::array<Biquad, 4>, maxChannels> consoleFilters;
    std::array<std::array<Biquad, 3>, maxChannels> tapeFilters;
    std::array<std::array<Biquad, 2>, maxChannels> valveFilters;
    std::array<Biquad, maxChannels> dynamicsSidechainFilters;
    std::array<EnvelopeFollower, maxChannels> analogDynamicsEnvelopes;
    std::atomic<float> analogGainReduction { 0.0f };
    // low boost, low cut, mid presence, high boost, high cut: boost and cut are
    // separate curves, as on the passive hardware, so they never cancel.
    std::array<std::array<Biquad, 5>, maxChannels> passiveEqFilters;
    std::array<std::array<Biquad, 3>, maxChannels> plateToneFilters;   // bass cut, treble shelf, vintage roll-off
    juce::LinearSmoothedValue<float> plateBypass;
    static constexpr size_t plateLineCount = 4;
    std::array<std::array<std::vector<float>, plateLineCount>, maxChannels> plateLines;
    std::array<std::array<size_t, plateLineCount>, maxChannels> platePositions {};
    std::array<std::array<float, plateLineCount>, maxChannels> plateDampingState {};
    std::array<std::vector<float>, maxChannels> platePreDelay;
    std::array<size_t, maxChannels> platePreDelayPosition {};
    std::array<std::vector<float>, maxChannels> limiterDelay;
    std::array<size_t, maxChannels> limiterPosition {};
    std::array<float, maxChannels> limiterGain { 1.0f, 1.0f };
    std::array<float, maxChannels> limiterPrevious {};
    std::array<Biquad, maxChannels> deesserDetectors;
    std::array<Biquad, maxChannels> deesserFilters;
    std::array<EnvelopeFollower, maxChannels> deesserEnvelopes;
    static constexpr size_t resonanceBandCount = 4;
    std::array<std::array<Biquad, resonanceBandCount>, maxChannels> resonanceDetectors;
    std::array<std::array<Biquad, resonanceBandCount>, maxChannels> resonanceFilters;
    std::array<std::array<EnvelopeFollower, maxChannels>, resonanceBandCount> resonanceEnvelopes;
    std::array<float, resonanceBandCount> resonanceProfile {};
    bool resonanceProfileValid = false;
    std::array<std::vector<float>, maxChannels> orbitDelay;
    std::array<size_t, maxChannels> orbitPosition {};
    std::array<std::array<Biquad, 8>, maxChannels> orbitTapFilters;
    std::array<EnvelopeFollower, maxChannels> orbitDuckEnvelopes;
    double orbitPhase = 0.0;
    std::array<Biquad, maxChannels> orbitMainFilter;
    float orbitBaseSmooth = 0.0f;
    std::array<float, 8> orbitTapSmooth {};
    float orbitBaseVelocity = 0.0f;
    std::array<float, 8> orbitTapVelocity {};
    double hostBpm = 120.0;

    // SPACEVERB: its own network, so the analog plate is untouched.
    static constexpr size_t spaceLineCount = 8;
    std::array<std::vector<float>, spaceLineCount> spaceLines;
    std::array<size_t, spaceLineCount> spacePositions {};
    std::array<float, spaceLineCount> spaceDampState {}, spaceLowState {};
    std::array<std::array<std::vector<float>, 4>, 2> spaceDiffusers;
    std::array<std::array<size_t, 4>, 2> spaceDiffuserPositions {};
    std::array<std::vector<float>, 2> spacePre;
    std::array<size_t, 2> spacePrePosition {};
    double spacePhase = 0.0;
    float spaceSizeSmooth = 0.0f, spaceOutScale = 1.0f;
    std::array<std::vector<float>, maxChannels> tapeDelay;
    std::array<size_t, maxChannels> tapeWritePosition {};
    double tapeWowPhase = 0.0;
    double tapeFlutterPhase = 0.0;
    std::array<uint32_t, maxChannels> noiseState { 0x12345678u, 0x9abcdef0u };
    std::vector<float> dryBuffer;
    std::vector<float> spectraBuffer;
    size_t spectraStride = 0;
    double sampleRate = 44100.0;

    float value(const juce::AudioProcessorValueTreeState&, const juce::String&, float fallback = 0.0f) const;
    static void applyInputAndOutput(juce::AudioBuffer<float>&, float inputDb, float outputDb);
    void processPrism(juce::AudioBuffer<float>&, const juce::AudioProcessorValueTreeState&);
    void processFlux(juce::AudioBuffer<float>&, const juce::AudioProcessorValueTreeState&);
    void processSpectra(juce::AudioBuffer<float>&, const juce::AudioProcessorValueTreeState&);
    void processHeritage(juce::AudioBuffer<float>&, const juce::AudioProcessorValueTreeState&);
    void processIron(juce::AudioBuffer<float>&, const juce::AudioProcessorValueTreeState&);
    void processConsole(juce::AudioBuffer<float>&, const juce::AudioProcessorValueTreeState&);
    void processTape(juce::AudioBuffer<float>&, const juce::AudioProcessorValueTreeState&);
    void processValve(juce::AudioBuffer<float>&, const juce::AudioProcessorValueTreeState&);
    void processAnalogCompressor(juce::AudioBuffer<float>&, const juce::AudioProcessorValueTreeState&,
                                 const juce::String& pluginId);
    void processPassiveEq(juce::AudioBuffer<float>&, const juce::AudioProcessorValueTreeState&);
    void processPlate(juce::AudioBuffer<float>&, const juce::AudioProcessorValueTreeState&);
    void processLimiter(juce::AudioBuffer<float>&, const juce::AudioProcessorValueTreeState&);
    void processDeesser(juce::AudioBuffer<float>&, const juce::AudioProcessorValueTreeState&);
    void processResonance(juce::AudioBuffer<float>&, const juce::AudioProcessorValueTreeState&);
    void processFrequencyShaper(juce::AudioBuffer<float>&, const juce::AudioProcessorValueTreeState&);
    void processOrbit(juce::AudioBuffer<float>&, const juce::AudioProcessorValueTreeState&);
    void processSpaceReverb(juce::AudioBuffer<float>&, const juce::AudioProcessorValueTreeState&);
    void processImager(juce::AudioBuffer<float>&, const juce::AudioProcessorValueTreeState&);
    void processGeneric(juce::AudioBuffer<float>&, const juce::AudioProcessorValueTreeState&);
};
}
