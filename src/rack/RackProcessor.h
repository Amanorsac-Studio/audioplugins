#pragma once

#include "common/dsp/AnchorDSP.h"
#include "common/dsp/AnalogFrontEnd.h"
#include "common/presets/PresetManager.h"
#include "common/state/PluginSpec.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <vector>

namespace amanorsac
{
/** ANALOG MIX RACK: a host the ten analog processors live in.

    Every module is a slot the engineer can switch in, solo, drag to a new
    position, and send down either of two parallel lanes. Lane A and lane B are
    summed with their own level and polarity, and the lanes are latency aligned
    so a module that reports latency cannot smear the parallel path.

    The rack keeps one parameter tree holding its own controls plus every
    module's contract under an "A01." ... "A10." prefix, so each module control
    is a real host parameter: automatable, saved with the session, and reachable
    by the shared preset system. Each slot runs the same AnchorDSP engine and
    AnalogFrontEnd console strip the individual plugin uses, so a module in the
    rack is the module.
*/
class RackProcessor final : public juce::AudioProcessor
{
public:
    static constexpr int moduleCount = 10;
    enum class Lane { a = 0, b = 1 };

    RackProcessor();
    ~RackProcessor() override;

    void prepareToPlay(double, int) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return spec.displayName; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int) override;
    const juce::String getProgramName(int) override;
    void changeProgramName(int, const juce::String&) override;

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorParameter* getBypassParameter() const override
    {
        return state.getParameter("bypass");
    }

    /** The R01 contract plus every module's, under its prefix. */
    const PluginSpec spec;
    juce::UndoManager undoManager;
    juce::AudioProcessorValueTreeState state;
    std::unique_ptr<presets::PresetManager> presetManager;

    [[nodiscard]] const PluginSpec& moduleSpec(int slot) const;
    [[nodiscard]] static juce::String modulePrefix(int slot);

    // ------------------------------------------------------------ arrangement

    [[nodiscard]] bool isSplit() const;
    [[nodiscard]] Lane laneOf(int slot) const;
    [[nodiscard]] int positionOf(int slot) const;
    [[nodiscard]] bool isEnabled(int slot) const;
    [[nodiscard]] bool isSoloed(int slot) const;
    [[nodiscard]] bool anySoloed() const;
    /** True when this slot is actually in the signal path right now. */
    [[nodiscard]] bool isActive(int slot) const;

    /** Slots on a lane, in playing order. */
    [[nodiscard]] std::vector<int> laneOrder(Lane) const;

    /** Moves a slot to a lane at a position, then renumbers the whole rack so
        the arrangement stays unambiguous. Called from the editor when a module
        is dragged. */
    void moveSlot(int slot, Lane targetLane, int targetIndex);

    // ---------------------------------------------------------------- metering

    [[nodiscard]] float getInputPeak(int channel) const noexcept
    {
        return inputPeaks[static_cast<size_t>(juce::jlimit(0, 1, channel))].load();
    }
    [[nodiscard]] float getOutputPeak(int channel) const noexcept
    {
        return outputPeaks[static_cast<size_t>(juce::jlimit(0, 1, channel))].load();
    }
    /** Deepest gain reduction of the compressor slots, for the rack meter. */
    [[nodiscard]] float getGainReductionDb() const noexcept { return gainReduction.load(); }
    /** Peak leaving a slot, for its strip meter. */
    [[nodiscard]] float getSlotPeak(int slot) const noexcept
    {
        return slotPeaks[static_cast<size_t>(juce::jlimit(0, moduleCount - 1, slot))].load();
    }

private:
    struct Slot;
    static PluginSpec buildSpec();

    /** Runs one lane of the chain and returns the latency it accumulated. */
    int processLane(juce::AudioBuffer<float>&, Lane, bool split, bool soloing, int channels);
    static void delayBuffer(juce::AudioBuffer<float>&, juce::AudioBuffer<float>& ring, int& position,
                            int channels, int samples);
    [[nodiscard]] float parameter(const juce::String& id, float fallback = 0.0f) const;

    std::array<std::unique_ptr<Slot>, moduleCount> slots;
    juce::AudioBuffer<float> slotDry, rackDry;
    std::array<juce::AudioBuffer<float>, 2> laneBuffer;
    std::array<juce::AudioBuffer<float>, 2> laneAlign;
    std::array<int, 2> lanePosition { 0, 0 };
    juce::LinearSmoothedValue<float> rackBypass;

    std::array<std::atomic<float>, 2> inputPeaks {};
    std::array<std::atomic<float>, 2> outputPeaks {};
    std::array<std::atomic<float>, moduleCount> slotPeaks {};
    std::atomic<float> gainReduction { 0.0f };

    JUCE_DECLARE_WEAK_REFERENCEABLE(RackProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RackProcessor)
};
}
