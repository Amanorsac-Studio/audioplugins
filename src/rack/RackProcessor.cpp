#include "RackProcessor.h"
#include "RackEditor.h"
#include "RackModuleSpecs.h"

#include <algorithm>

namespace amanorsac
{
namespace
{
/** Module display names carry their product name, not the planning code, so a
    host's automation list reads "HERITAGE EQ Low Gain". */
juce::String moduleNamePrefix(const PluginSpec& module)
{
    return module.displayName + " ";
}

constexpr int alignmentCapacity = 4096;
}

struct RackProcessor::Slot
{
    explicit Slot(const juce::String& json, const juce::String& prefix)
        : spec(PluginSpec::fromJson(json))
    {
        dsp.setParameterPrefix(prefix);
        frontEnd.configure(spec, prefix);
        enabledId = "slot." + spec.id + ".enabled";
        soloId = "slot." + spec.id + ".solo";
        orderId = "slot." + spec.id + ".order";
        laneId = "slot." + spec.id + ".lane";
    }

    PluginSpec spec;
    AnchorDSP dsp;
    AnalogFrontEnd frontEnd;
    juce::String enabledId, soloId, orderId, laneId;

    // Click-free switching, and a dry reference delayed by whatever latency the
    // engine reports so the crossfade stays phase aligned.
    juce::LinearSmoothedValue<float> engage { 0.0f };
    juce::AudioBuffer<float> dryDelay;
    int dryPosition = 0;

    void prepare(double sampleRate, int maximumBlockSize, int channels)
    {
        dsp.prepare(sampleRate, maximumBlockSize, channels);
        frontEnd.prepare(sampleRate, maximumBlockSize);
        engage.reset(sampleRate, 0.02);
        dryDelay.setSize(2, 256);
        dryDelay.clear();
        dryPosition = 0;
    }

    void reset()
    {
        dsp.reset();
        frontEnd.reset();
        dryDelay.clear();
        dryPosition = 0;
        engage.setCurrentAndTargetValue(engage.getTargetValue());
    }
};

// ------------------------------------------------------------------- contract

PluginSpec RackProcessor::buildSpec()
{
    auto rack = PluginSpec::fromEmbeddedJson();
    for (int i = 0; i < moduleCount; ++i)
    {
        const auto module = PluginSpec::fromJson(juce::String::fromUTF8(generated::rackModuleJson[i]));
        const auto prefix = modulePrefix(i);
        const auto namePrefix = moduleNamePrefix(module);
        for (const auto& descriptor : module.parameters)
        {
            auto copy = descriptor;
            copy.id = prefix + descriptor.id;
            copy.name = namePrefix + descriptor.name;
            rack.parameters.push_back(copy);
        }
    }
    return rack;
}

juce::String RackProcessor::modulePrefix(int slot)
{
    return "A" + juce::String(juce::jlimit(1, moduleCount, slot + 1)).paddedLeft('0', 2) + ".";
}

const PluginSpec& RackProcessor::moduleSpec(int slot) const
{
    return slots[static_cast<size_t>(juce::jlimit(0, moduleCount - 1, slot))]->spec;
}

RackProcessor::RackProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      spec(buildSpec()),
      state(*this, &undoManager, "AMANORSAC_RACK_STATE", PluginSpec::createParameterLayout(spec))
{
    for (int i = 0; i < moduleCount; ++i)
        slots[static_cast<size_t>(i)] =
            std::make_unique<Slot>(juce::String::fromUTF8(generated::rackModuleJson[static_cast<size_t>(i)]),
                                   modulePrefix(i));

    state.state.setProperty("schema_version", 1, nullptr);
    state.state.setProperty("plugin_id", "R01", nullptr);
    presetManager = std::make_unique<presets::PresetManager>(state, spec);
}

RackProcessor::~RackProcessor() = default;

// ---------------------------------------------------------------- arrangement

float RackProcessor::parameter(const juce::String& id, float fallback) const
{
    if (const auto* value = state.getRawParameterValue(id))
        return value->load(std::memory_order_relaxed);
    return fallback;
}

bool RackProcessor::isSplit() const { return parameter("split") > 0.5f; }

RackProcessor::Lane RackProcessor::laneOf(int slot) const
{
    const auto index = static_cast<size_t>(juce::jlimit(0, moduleCount - 1, slot));
    return parameter(slots[index]->laneId) > 0.5f ? Lane::b : Lane::a;
}

int RackProcessor::positionOf(int slot) const
{
    const auto index = static_cast<size_t>(juce::jlimit(0, moduleCount - 1, slot));
    return static_cast<int>(std::lround(parameter(slots[index]->orderId, static_cast<float>(slot + 1))));
}

bool RackProcessor::isEnabled(int slot) const
{
    return parameter(slots[static_cast<size_t>(juce::jlimit(0, moduleCount - 1, slot))]->enabledId) > 0.5f;
}

bool RackProcessor::isSoloed(int slot) const
{
    return parameter(slots[static_cast<size_t>(juce::jlimit(0, moduleCount - 1, slot))]->soloId) > 0.5f;
}

bool RackProcessor::anySoloed() const
{
    for (int i = 0; i < moduleCount; ++i) if (isSoloed(i)) return true;
    return false;
}

bool RackProcessor::isActive(int slot) const
{
    // Solo overrides the switches, as it does on a console.
    return anySoloed() ? isSoloed(slot) : isEnabled(slot);
}

std::vector<int> RackProcessor::laneOrder(Lane lane) const
{
    std::vector<int> order;
    const auto split = isSplit();
    for (int i = 0; i < moduleCount; ++i)
        if (! split ? lane == Lane::a : laneOf(i) == lane) order.push_back(i);

    std::stable_sort(order.begin(), order.end(), [this](int left, int right)
    {
        return positionOf(left) < positionOf(right);
    });
    return order;
}

void RackProcessor::moveSlot(int slot, Lane targetLane, int targetIndex)
{
    slot = juce::jlimit(0, moduleCount - 1, slot);

    // Take the current arrangement apart, move the one slot, then renumber
    // everything so no two modules can claim the same position.
    std::array<std::vector<int>, 2> lanes;
    for (int laneIndex = 0; laneIndex < 2; ++laneIndex)
        for (int i = 0; i < moduleCount; ++i)
            if (static_cast<int>(laneOf(i)) == laneIndex) lanes[static_cast<size_t>(laneIndex)].push_back(i);

    for (auto& lane : lanes)
    {
        std::stable_sort(lane.begin(), lane.end(), [this](int left, int right)
        {
            return positionOf(left) < positionOf(right);
        });
        lane.erase(std::remove(lane.begin(), lane.end(), slot), lane.end());
    }

    auto& destination = lanes[static_cast<size_t>(targetLane)];
    destination.insert(destination.begin() + juce::jlimit(0, static_cast<int>(destination.size()), targetIndex),
                       slot);

    auto assign = [this](const juce::String& id, float value)
    {
        if (auto* target = state.getParameter(id))
        {
            target->beginChangeGesture();
            target->setValueNotifyingHost(target->convertTo0to1(value));
            target->endChangeGesture();
        }
    };

    undoManager.beginNewTransaction("Reorder rack");
    auto position = 1;
    for (int laneIndex = 0; laneIndex < 2; ++laneIndex)
        for (const auto member : lanes[static_cast<size_t>(laneIndex)])
        {
            assign(slots[static_cast<size_t>(member)]->orderId, static_cast<float>(position++));
            assign(slots[static_cast<size_t>(member)]->laneId, static_cast<float>(laneIndex));
        }
}

// ------------------------------------------------------------------ processing

void RackProcessor::prepareToPlay(double rate, int maximumBlockSize)
{
    const auto channels = juce::jmax(1, getTotalNumOutputChannels());
    for (auto& slot : slots) slot->prepare(rate, maximumBlockSize, channels);
    slotDry.setSize(2, juce::jmax(1, maximumBlockSize));
    rackDry.setSize(2, juce::jmax(1, maximumBlockSize));
    for (auto& lane : laneBuffer) lane.setSize(2, juce::jmax(1, maximumBlockSize));
    for (auto& align : laneAlign) { align.setSize(2, alignmentCapacity); align.clear(); }
    lanePosition = { 0, 0 };
    rackBypass.reset(rate, 0.02);
    rackBypass.setCurrentAndTargetValue(0.0f);
}

void RackProcessor::releaseResources()
{
    for (auto& slot : slots) slot->reset();
    for (auto& align : laneAlign) align.clear();
    lanePosition = { 0, 0 };
}

bool RackProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto output = layouts.getMainOutputChannelSet();
    return (output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo())
           && layouts.getMainInputChannelSet() == output;
}

void RackProcessor::delayBuffer(juce::AudioBuffer<float>& buffer, juce::AudioBuffer<float>& ring,
                                int& position, int channels, int samples)
{
    const auto capacity = ring.getNumSamples();
    if (capacity <= 0) return;
    const auto delay = juce::jlimit(0, capacity - 1, samples);
    auto committed = position;
    for (int channel = 0; channel < channels; ++channel)
    {
        auto local = position;
        auto* data = buffer.getWritePointer(channel);
        auto* line = ring.getWritePointer(channel);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            line[local] = data[i];
            data[i] = line[(local + capacity - delay) % capacity];
            local = (local + 1) % capacity;
        }
        committed = local;
    }
    position = committed;
}

int RackProcessor::processLane(juce::AudioBuffer<float>& buffer, Lane lane, bool split, bool soloing,
                               int channels)
{
    const auto samples = buffer.getNumSamples();
    auto latency = 0;

    for (const auto index : laneOrder(lane))
    {
        auto& slot = *slots[static_cast<size_t>(index)];
        const auto active = soloing ? isSoloed(index) : isEnabled(index);
        slot.engage.setTargetValue(active ? 1.0f : 0.0f);

        // A slot that is fully off costs nothing; one that is fading still runs
        // so the crossfade has something to fade to.
        if (! active && ! slot.engage.isSmoothing())
        {
            slot.dryDelay.clear();
            slotPeaks[static_cast<size_t>(index)].store(0.0f);
            continue;
        }

        for (int channel = 0; channel < channels; ++channel)
            slotDry.copyFrom(channel, 0, buffer, channel, 0, samples);

        slot.frontEnd.processFront(buffer, state, channels);
        slot.dsp.process(buffer, state, slot.spec.id);
        slot.frontEnd.processBack(buffer, state, channels);

        const auto slotLatency = juce::jlimit(0, slot.dryDelay.getNumSamples() - 1, slot.dsp.latencySamples());
        latency += slotLatency;

        // Delay the untouched slot input by the engine's own latency so
        // switching a module in cannot smear the signal against itself.
        const auto ringLength = slot.dryDelay.getNumSamples();
        auto position = slot.dryPosition;
        auto ramp = slot.engage;
        for (int channel = 0; channel < channels; ++channel)
        {
            auto local = position;
            auto localRamp = slot.engage;
            auto* wet = buffer.getWritePointer(channel);
            const auto* dry = slotDry.getReadPointer(channel);
            auto* ring = slot.dryDelay.getWritePointer(channel);
            for (int i = 0; i < samples; ++i)
            {
                ring[local] = dry[i];
                const auto aligned = ring[(local + ringLength - slotLatency) % ringLength];
                local = (local + 1) % ringLength;
                const auto amount = localRamp.getNextValue();
                wet[i] = aligned + amount * (wet[i] - aligned);
            }
            position = local;
            ramp = localRamp;
        }
        slot.dryPosition = position;
        slot.engage = ramp;

        slotPeaks[static_cast<size_t>(index)].store(buffer.getMagnitude(0, 0, samples));
        if (slot.spec.id == "A06" || slot.spec.id == "A07" || slot.spec.id == "A08")
            gainReduction.store(juce::jmin(gainReduction.load(), slot.dsp.gainReductionDb()));
    }

    juce::ignoreUnused(split);
    return latency;
}

void RackProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals guard;
    const auto samples = buffer.getNumSamples();
    const auto channels = juce::jmin(2, buffer.getNumChannels());
    if (samples <= 0 || channels <= 0) return;

    for (int channel = getMainBusNumInputChannels(); channel < getMainBusNumOutputChannels(); ++channel)
        buffer.clear(channel, 0, samples);

    for (int channel = 0; channel < 2; ++channel)
        inputPeaks[static_cast<size_t>(channel)].store(
            channel < channels ? buffer.getMagnitude(channel, 0, samples) : 0.0f);

    rackDry.setSize(2, samples, false, false, true);
    for (int channel = 0; channel < channels; ++channel)
        rackDry.copyFrom(channel, 0, buffer, channel, 0, samples);

    buffer.applyGain(juce::Decibels::decibelsToGain(parameter("input")));

    slotDry.setSize(2, samples, false, false, true);
    gainReduction.store(0.0f);
    const auto soloing = anySoloed();
    // Splitting with nothing routed to lane B would sum the dry signal back in
    // and jump the level. The rack only splits once B actually holds a module.
    const auto split = isSplit() && ! laneOrder(Lane::b).empty();
    auto latency = 0;

    if (! split)
    {
        latency = processLane(buffer, Lane::a, false, soloing, channels);
    }
    else
    {
        // Two lanes fed from the same signal, summed with their own level and
        // polarity. The shorter lane is delayed to match the longer one, so a
        // module that reports latency cannot comb the parallel path.
        for (auto& lane : laneBuffer)
        {
            lane.setSize(2, samples, false, false, true);
            for (int channel = 0; channel < channels; ++channel)
                lane.copyFrom(channel, 0, buffer, channel, 0, samples);
        }

        const auto latencyA = processLane(laneBuffer[0], Lane::a, true, soloing, channels);
        const auto latencyB = processLane(laneBuffer[1], Lane::b, true, soloing, channels);
        latency = juce::jmax(latencyA, latencyB);

        delayBuffer(laneBuffer[0], laneAlign[0], lanePosition[0], channels, latency - latencyA);
        delayBuffer(laneBuffer[1], laneAlign[1], lanePosition[1], channels, latency - latencyB);

        const auto levelA = juce::Decibels::decibelsToGain(parameter("lane_a_level"));
        const auto levelB = juce::Decibels::decibelsToGain(parameter("lane_b_level"))
                            * (parameter("lane_b_phase") > 0.5f ? -1.0f : 1.0f);
        for (int channel = 0; channel < channels; ++channel)
        {
            auto* out = buffer.getWritePointer(channel);
            const auto* a = laneBuffer[0].getReadPointer(channel);
            const auto* b = laneBuffer[1].getReadPointer(channel);
            for (int i = 0; i < samples; ++i) out[i] = a[i] * levelA + b[i] * levelB;
        }
    }

    if (latency != getLatencySamples()) setLatencySamples(latency);

    buffer.applyGain(juce::Decibels::decibelsToGain(parameter("output")));

    // Global wet/dry against the untouched rack input, then a click-free bypass.
    const auto mix = juce::jlimit(0.0f, 1.0f, parameter("mix", 100.0f) * 0.01f);
    if (mix < 1.0f)
        for (int channel = 0; channel < channels; ++channel)
        {
            auto* wet = buffer.getWritePointer(channel);
            const auto* dry = rackDry.getReadPointer(channel);
            for (int i = 0; i < samples; ++i) wet[i] = dry[i] + mix * (wet[i] - dry[i]);
        }

    rackBypass.setTargetValue(parameter("bypass") > 0.5f ? 1.0f : 0.0f);
    if (rackBypass.isSmoothing() || rackBypass.getTargetValue() > 0.5f)
        for (int channel = 0; channel < channels; ++channel)
        {
            auto ramp = rackBypass;
            auto* wet = buffer.getWritePointer(channel);
            const auto* dry = rackDry.getReadPointer(channel);
            for (int i = 0; i < samples; ++i) wet[i] += ramp.getNextValue() * (dry[i] - wet[i]);
            if (channel == channels - 1) rackBypass = ramp;
        }

    for (int channel = 0; channel < 2; ++channel)
        outputPeaks[static_cast<size_t>(channel)].store(
            channel < channels ? buffer.getMagnitude(channel, 0, samples) : 0.0f);
}

// ------------------------------------------------------------------ programs

int RackProcessor::getNumPrograms()
{
    return juce::jmax(1, static_cast<int>(presetManager->presets().size()));
}

int RackProcessor::getCurrentProgram() { return presetManager->currentIndex(); }

void RackProcessor::setCurrentProgram(int index)
{
    juce::WeakReference<RackProcessor> self(this);
    juce::MessageManager::callAsync([self, index]
    {
        if (self != nullptr) self->presetManager->load(index);
    });
}

const juce::String RackProcessor::getProgramName(int index)
{
    const auto& list = presetManager->presets();
    if (index < 0 || index >= static_cast<int>(list.size())) return {};
    return list[static_cast<size_t>(index)].name;
}

void RackProcessor::changeProgramName(int index, const juce::String& newName)
{
    presetManager->rename(index, newName);
}

juce::AudioProcessorEditor* RackProcessor::createEditor() { return new RackEditor(*this); }

void RackProcessor::getStateInformation(juce::MemoryBlock& data)
{
    if (auto xml = state.copyState().createXml()) copyXmlToBinary(*xml, data);
}

void RackProcessor::setStateInformation(const void* data, int size)
{
    if (auto xml = getXmlFromBinary(data, size); xml != nullptr && xml->hasTagName(state.state.getType()))
        state.replaceState(juce::ValueTree::fromXml(*xml));
}
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new amanorsac::RackProcessor(); }
