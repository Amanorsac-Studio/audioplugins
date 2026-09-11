#include "PluginProcessor.h"
#include "common/ui/PluginEditor.h"
#include "common/ui/HeritageEditor.h"
#include "common/ui/AnalogProductEditor.h"
#include "common/ui/IronPreEditor.h"
#include "common/ui/AnalogPageEditor.h"
#include "common/ui/FaceplateEditor.h"
#include "common/ui/PrismEditor.h"
#include "common/licensing/LicenseClient.h"

namespace amanorsac
{
PluginProcessor::BusesProperties PluginProcessor::makeBusesProperties()
{
    auto buses = BusesProperties()
                     .withInput("Input", juce::AudioChannelSet::stereo(), true)
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true);
    const auto id = PluginSpec::fromEmbeddedJson().id;
    if (id == "D02" || id == "A06")
        buses = buses.withInput("Sidechain", juce::AudioChannelSet::stereo(), false);
    return buses;
}

PluginProcessor::PluginProcessor()
    : AudioProcessor(makeBusesProperties()),
      spec(PluginSpec::fromEmbeddedJson()),
      state(*this, &undoManager, "AMANORSAC_STATE", PluginSpec::createParameterLayout(spec))
{
    state.state.setProperty("schema_version", 1, nullptr);
    state.state.setProperty("plugin_id", spec.id, nullptr);
    presetManager = std::make_unique<presets::PresetManager>(state, spec);
    frontEnd.configure(spec);
}

// The host program list mirrors the user preset folder.
int PluginProcessor::getNumPrograms()
{
    return juce::jmax(1, static_cast<int>(presetManager->presets().size()));
}

int PluginProcessor::getCurrentProgram()
{
    return presetManager->currentIndex();
}

void PluginProcessor::setCurrentProgram(int index)
{
    // Hosts may call this from any thread; preset loading writes parameters
    // and touches the undo manager, which belong to the message thread.
    juce::WeakReference<PluginProcessor> self(this);
    juce::MessageManager::callAsync([self, index]
    {
        if (self != nullptr) self->presetManager->load(index);
    });
}

const juce::String PluginProcessor::getProgramName(int index)
{
    const auto& list = presetManager->presets();
    if (index < 0 || index >= static_cast<int>(list.size())) return {};
    return list[static_cast<size_t>(index)].name;
}

void PluginProcessor::changeProgramName(int index, const juce::String& newName)
{
    presetManager->rename(index, newName);
}

void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    dsp.prepare(sampleRate, samplesPerBlock, getTotalNumOutputChannels());
    frontEnd.prepare(sampleRate, samplesPerBlock);
    entitlement.reset(sampleRate, 0.05);
    entitlement.setCurrentAndTargetValue(licensing::LicenseClient::getInstance().isLicensed() ? 1.0f : 0.0f);
}

void PluginProcessor::releaseResources()
{
    dsp.reset();
    frontEnd.reset();
}

bool PluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto output = layouts.getMainOutputChannelSet();
    if (output != juce::AudioChannelSet::mono() && output != juce::AudioChannelSet::stereo())
        return false;
    if (layouts.getMainInputChannelSet() != output) return false;
    if (layouts.inputBuses.size() > 1)
    {
        const auto sidechain = layouts.getChannelSet(true, 1);
        if (!sidechain.isDisabled() && sidechain != juce::AudioChannelSet::mono()
                                    && sidechain != juce::AudioChannelSet::stereo()) return false;
    }
    return true;
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    int requestedLatency = 0;
    if (spec.id == "D03" && state.getRawParameterValue("split_phase")->load() > 0.5f)
        requestedLatency = 31;
    else if (spec.id == "D04")
        requestedLatency = static_cast<int>(std::lround(state.getRawParameterValue("lookahead")->load()
                                                        * 0.001 * getSampleRate()));
    else if (spec.isAnalog())
        requestedLatency = dsp.latencySamples();   // oversampling delay from the previous block, stable per setting
    if (requestedLatency != getLatencySamples()) setLatencySamples(requestedLatency);
    for (int channel = getMainBusNumInputChannels(); channel < getMainBusNumOutputChannels(); ++channel)
        buffer.clear(channel, 0, buffer.getNumSamples());
    for (int channel = 0; channel < 2; ++channel)
        inputPeaks[static_cast<size_t>(channel)].store(
            channel < buffer.getNumChannels() ? buffer.getMagnitude(channel, 0, buffer.getNumSamples()) : 0.0f);
    const auto mainChannels = juce::jmin(2, getMainBusNumOutputChannels(), buffer.getNumChannels());
    frontEnd.processFront(buffer, state, mainChannels);
    dsp.process(buffer, state, spec.id);
    frontEnd.processBack(buffer, state, mainChannels);

    // An unlicensed bundle produces no output. The flag is atomic and the
    // ramp is smooth, so this never allocates, blocks or clicks on the audio
    // thread whatever the licence state is doing elsewhere.
    {
        entitlement.setTargetValue(licensing::LicenseClient::getInstance().isLicensed() ? 1.0f : 0.0f);
        if (entitlement.isSmoothing() || entitlement.getTargetValue() < 0.5f)
            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            {
                auto ramp = entitlement;
                auto* samples = buffer.getWritePointer(channel);
                for (int i = 0; i < buffer.getNumSamples(); ++i) samples[i] *= ramp.getNextValue();
                if (channel == buffer.getNumChannels() - 1) entitlement = ramp;
            }
    }

    for (int channel = 0; channel < 2; ++channel)
        outputPeaks[static_cast<size_t>(channel)].store(
            channel < buffer.getNumChannels() ? buffer.getMagnitude(channel, 0, buffer.getNumSamples()) : 0.0f);
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    if (spec.id == "D01")
        return new PrismEditor(*this);
    // Every analog product is drawn by the shared analog chassis. Its layout is
    // derived from the product parameter contract, so only controls the engine
    // actually implements are placed.
    // A01 is built directly to its approved faceplate layout.
    if (spec.isAnalog())
    {
        // The analog editors are written against a host rather than this class,
        // so the very same faceplate serves the plugin and a rack slot.
        ProductHost host;
        host.spec = &spec;
        host.state = &state;
        host.undoManager = &undoManager;
        host.presetManager = presetManager.get();
        host.inputPeak = [this](int channel) { return getInputPeak(channel); };
        host.outputPeak = [this](int channel) { return getOutputPeak(channel); };
        host.gainReduction = [this] { return getGainReductionDb(); };
        host.sampleRate = [this] { return getSampleRate(); };
        host.latency = [this] { return getLatencySamples(); };

        if (spec.id == "A01") return new HeritageEditor(*this, std::move(host));
        return new AnalogPageEditor(*this, std::move(host));
    }
    return new PluginEditor(*this);
}

void PluginProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    if (auto xml = state.copyState().createXml())
        copyXmlToBinary(*xml, destination);
}

void PluginProcessor::setStateInformation(const void* data, int size)
{
    if (auto xml = getXmlFromBinary(data, size))
        if (xml->hasTagName(state.state.getType()))
            state.replaceState(juce::ValueTree::fromXml(*xml));
}
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new amanorsac::PluginProcessor();
}
