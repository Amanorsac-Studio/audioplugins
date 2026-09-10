// Audit of the whole analog line and the rack that hosts it.
//
// Every product is driven through the same engine and console strip the plugin
// uses, so anything this file proves is true of the shipped product. The audit
// answers the questions that decide whether the bundle can go on sale: does
// every control do something, is the output always finite, is silence silent,
// is bypass really bypass, does a compressor actually compress, and does a
// module inside the rack sound exactly like the module on its own.

#include "common/dsp/AnalogFrontEnd.h"
#include "common/dsp/AnchorDSP.h"
#include "common/presets/PresetManager.h"
#include "common/state/PluginSpec.h"
#include "rack/RackProcessor.h"

#include "BinaryData.h"
#include "RackModuleSpecs.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <cmath>
#include <iostream>
#include <vector>

namespace
{
int failures = 0;
juce::String section;

void beginSection(const juce::String& name)
{
    section = name;
    std::cout << "\n== " << name << " ==" << std::endl;
}

void check(bool condition, const juce::String& what)
{
    if (condition) { std::cout << "  ok    " << what << std::endl; return; }
    std::cout << "  FAIL  " << what << std::endl;
    ++failures;
}

class Harness final : public juce::AudioProcessor
{
public:
    explicit Harness(const amanorsac::PluginSpec& spec)
        : AudioProcessor(BusesProperties().withInput("In", juce::AudioChannelSet::stereo(), true)
                                          .withOutput("Out", juce::AudioChannelSet::stereo(), true)),
          state(*this, nullptr, "AUDIT", amanorsac::PluginSpec::createParameterLayout(spec)) {}

    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& l) const override
    { return l.getMainInputChannelSet() == l.getMainOutputChannelSet(); }
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    const juce::String getName() const override { return "Audit"; }
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

/** One analog product, engine plus console strip, exactly as the plugin runs it. */
struct Product
{
    explicit Product(const juce::String& json)
        : spec(amanorsac::PluginSpec::fromJson(json)), harness(spec)
    {
        frontEnd.configure(spec);
    }

    void prepare(double rate, int block)
    {
        sampleRate = rate;
        dsp.prepare(rate, block, 2);
        frontEnd.prepare(rate, block);
    }

    void run(juce::AudioBuffer<float>& buffer)
    {
        const auto channels = juce::jmin(2, buffer.getNumChannels());
        frontEnd.processFront(buffer, harness.state, channels);
        dsp.process(buffer, harness.state, spec.id);
        frontEnd.processBack(buffer, harness.state, channels);
    }

    void set(const juce::String& id, float value)
    {
        if (auto* parameter = harness.state.getParameter(id))
            parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
    }

    void setDefaults()
    {
        for (const auto& descriptor : spec.parameters) set(descriptor.id, descriptor.defaultValue);
    }

    /** Every stage switched in and driven, which is the only state in which a
        frequency or Q control can be heard at all: on a flat band they are
        correctly silent. Bypass stays off and mix stays wet. */
    void setEngaged()
    {
        for (const auto& descriptor : spec.parameters)
        {
            // Switches that override another control stay off, or the control
            // they override would look dead.
            if (descriptor.id == "bypass" || descriptor.id == "auto_release")
            { set(descriptor.id, 0.0f); continue; }
            if (descriptor.id == "mix") { set(descriptor.id, descriptor.maximum); continue; }
            if (descriptor.kind == amanorsac::ParameterDescriptor::Kind::boolean)
            { set(descriptor.id, 1.0f); continue; }
            set(descriptor.id, juce::jmap(0.62f, descriptor.minimum, descriptor.maximum));
        }
    }

    void restart()
    {
        dsp.reset();
        frontEnd.reset();
    }

    [[nodiscard]] const amanorsac::ParameterDescriptor* find(const juce::String& id) const
    {
        for (const auto& descriptor : spec.parameters)
            if (descriptor.id == id) return &descriptor;
        return nullptr;
    }

    amanorsac::PluginSpec spec;
    Harness harness;
    amanorsac::AnchorDSP dsp;
    amanorsac::AnalogFrontEnd frontEnd;
    double sampleRate = 48000.0;
};

// ------------------------------------------------------------------- signals

void fillTone(juce::AudioBuffer<float>& buffer, double rate, double hz, float amplitude = 0.25f)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample(channel, i, amplitude * static_cast<float>(
                std::sin(juce::MathConstants<double>::twoPi * hz * i / rate)));
}

/** Wide, dense material: every engine has something to work on at every band. */
void fillProgramme(juce::AudioBuffer<float>& buffer, double rate, int startSample = 0)
{
    static const std::array<double, 5> partials { 55.0, 220.0, 997.0, 3200.0, 9000.0 };
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto n = static_cast<double>(startSample + i) + channel * 13.0;
            double value = 0.0;
            for (const auto hz : partials)
                value += 0.11 * std::sin(juce::MathConstants<double>::twoPi * hz * n / rate);
            buffer.setSample(channel, i, static_cast<float>(value));
        }
}

bool finite(const juce::AudioBuffer<float>& buffer)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (! std::isfinite(buffer.getSample(channel, i))) return false;
    return true;
}

double energy(const juce::AudioBuffer<float>& buffer)
{
    double sum = 0.0;
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto v = static_cast<double>(buffer.getSample(channel, i));
            sum += v * v;
        }
    return sum;
}

double difference(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    double sum = 0.0;
    for (int channel = 0; channel < a.getNumChannels(); ++channel)
        for (int i = 0; i < a.getNumSamples(); ++i)
        {
            const auto d = static_cast<double>(a.getSample(channel, i) - b.getSample(channel, i));
            sum += d * d;
        }
    return sum;
}

/** The reference a latency-reporting engine must return: the input, delayed by
    exactly the number of samples it declares to the host. */
juce::AudioBuffer<float> delayed(const juce::AudioBuffer<float>& source,
                                 const juce::AudioBuffer<float>& previous, int samples)
{
    juce::AudioBuffer<float> result(source.getNumChannels(), source.getNumSamples());
    for (int channel = 0; channel < source.getNumChannels(); ++channel)
        for (int i = 0; i < source.getNumSamples(); ++i)
            result.setSample(channel, i, i >= samples
                ? source.getSample(channel, i - samples)
                : previous.getSample(channel, previous.getNumSamples() - samples + i));
    return result;
}

float peak(const juce::AudioBuffer<float>& buffer)
{
    return buffer.getMagnitude(0, buffer.getNumSamples());
}

/** Settles the engine, then returns the steady-state block. */
juce::AudioBuffer<float> render(Product& product, int blocks = 32, int block = 512)
{
    juce::AudioBuffer<float> buffer(2, block);
    for (int i = 0; i < blocks; ++i)
    {
        fillProgramme(buffer, product.sampleRate, i * block);
        product.run(buffer);
    }
    return buffer;
}

// ------------------------------------------------------------------- audits

/** Every parameter in the contract must change the sound. A control that does
    nothing is a promise the product does not keep. */
void auditEveryParameterIsAudible(Product& product)
{
    juce::StringArray dead;
    for (const auto& descriptor : product.spec.parameters)
    {
        // The external sidechain needs a sidechain bus; it is audited separately.
        if (descriptor.id == "external_sc") continue;

        product.setEngaged();
        product.set(descriptor.id, descriptor.minimum);
        product.restart();
        const auto low = render(product);

        product.setEngaged();
        product.set(descriptor.id, descriptor.maximum);
        product.restart();
        const auto high = render(product);

        if (difference(low, high) <= 1.0e-9) dead.add(descriptor.id);
    }
    check(dead.isEmpty(), product.spec.displayName + ": every parameter changes the audio"
                              + (dead.isEmpty() ? juce::String() : " (dead: " + dead.joinIntoString(", ") + ")"));
}

/** No NaN or Inf anywhere, at any rate, at any block size, with everything at
    its extremes. This is the difference between a plugin and a crash. */
void auditNumericalSafety(Product& product)
{
    bool safe = true, bounded = true;
    for (const auto rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (const auto block : { 16, 64, 512, 2048 })
            for (const auto extreme : { false, true })
            {
                product.prepare(rate, block);
                product.setDefaults();
                if (extreme)
                    for (const auto& descriptor : product.spec.parameters)
                    {
                        // Trims are the engineer's gain staging, not a stability
                        // question: +24 in and +24 out is a loud setting, not a bug.
                        if (descriptor.id == "mix" || descriptor.id == "bypass"
                            || descriptor.id == "input" || descriptor.id == "output"
                            || descriptor.id == "makeup" || descriptor.id == "gain") continue;
                        product.set(descriptor.id, descriptor.maximum);
                    }

                juce::AudioBuffer<float> buffer(2, block);
                for (int i = 0; i < 16; ++i)
                {
                    fillProgramme(buffer, rate, i * block);
                    buffer.applyGain(2.0f);   // hot input, above unity
                    product.run(buffer);
                    if (! finite(buffer)) safe = false;
                    if (peak(buffer) > 64.0f) bounded = false;
                }
            }
    product.prepare(48000.0, 512);
    check(safe, product.spec.displayName + ": finite at 44.1/48/96/192 kHz, 16-2048 samples, all controls maxed");
    check(bounded, product.spec.displayName + ": output stays bounded on a hot input");
}

/** Silence in, silence out. Analog noise is a feature where the contract has
    it, so it is turned off for this check; nothing else may hiss. */
void auditSilence(Product& product)
{
    product.prepare(48000.0, 512);
    product.setDefaults();
    if (product.find("noise") != nullptr) product.set("noise", 0.0f);
    if (product.find("hiss") != nullptr) product.set("hiss", 0.0f);
    product.dsp.reset();
    product.frontEnd.reset();

    juce::AudioBuffer<float> buffer(2, 512);
    float worst = 0.0f;
    for (int i = 0; i < 24; ++i)
    {
        buffer.clear();
        product.run(buffer);
        worst = juce::jmax(worst, peak(buffer));
    }
    check(worst < 1.0e-4f, product.spec.displayName + ": silence in gives silence out (peak "
                               + juce::String(worst, 8) + ")");
}

/** Bypass must return the input, and it must get there without a click. */
void auditBypass(Product& product)
{
    if (product.find("bypass") == nullptr) return;
    product.prepare(48000.0, 512);
    product.setDefaults();
    for (const auto& descriptor : product.spec.parameters)
        if (descriptor.id != "bypass") product.set(descriptor.id, descriptor.maximum);
    product.set("bypass", 1.0f);
    product.dsp.reset();
    product.frontEnd.reset();

    juce::AudioBuffer<float> buffer(2, 512), reference(2, 512), earlier(2, 512);
    earlier.clear();
    double worst = 0.0;
    for (int i = 0; i < 8; ++i)
    {
        fillProgramme(buffer, 48000.0, i * 512);
        reference.makeCopyOf(buffer);
        product.run(buffer);
        const auto aligned = delayed(reference, earlier, product.dsp.latencySamples());
        earlier.makeCopyOf(reference);
        if (i >= 2) worst = juce::jmax(worst, difference(buffer, aligned) / juce::jmax(1.0e-12, energy(aligned)));
    }
    check(worst < 1.0e-6, product.spec.displayName
              + ": bypass returns the input untouched (allowing its reported "
              + juce::String(product.dsp.latencySamples()) + " samples of latency)");

    // engaging bypass must not step the signal
    product.setDefaults();
    product.set("bypass", 0.0f);
    product.dsp.reset();
    product.frontEnd.reset();
    for (int i = 0; i < 4; ++i) { fillProgramme(buffer, 48000.0, i * 512); product.run(buffer); }
    // A saturator's own output is steep, so the question is not how big the
    // steps are but whether switching added one.
    auto largestStep = [](const juce::AudioBuffer<float>& block)
    {
        auto largest = 0.0f;
        for (int channel = 0; channel < block.getNumChannels(); ++channel)
            for (int i = 1; i < block.getNumSamples(); ++i)
                largest = juce::jmax(largest, std::abs(block.getSample(channel, i)
                                                       - block.getSample(channel, i - 1)));
        return largest;
    };
    fillProgramme(buffer, 48000.0, 4 * 512);
    product.run(buffer);
    const auto settled = largestStep(buffer);

    product.set("bypass", 1.0f);
    fillProgramme(buffer, 48000.0, 5 * 512);
    product.run(buffer);
    const auto switching = largestStep(buffer);

    // A reverb tail is smoother than the dry signal it fades back to, so the
    // fair reference is the steeper of the two ends of the crossfade.
    for (int i = 6; i < 9; ++i) { fillProgramme(buffer, 48000.0, i * 512); product.run(buffer); }
    const auto bypassed = largestStep(buffer);
    const auto steepest = juce::jmax(settled, bypassed);
    check(switching <= steepest * 1.5f + 0.02f,
          product.spec.displayName + ": engaging bypass adds no step (" + juce::String(switching, 4)
              + " against " + juce::String(steepest, 4) + " at the ends of the crossfade)");
}

/** PHASE must invert, exactly. */
void auditPhase(Product& product)
{
    if (product.find("phase") == nullptr) return;
    product.prepare(48000.0, 512);

    auto capture = [&](float phase)
    {
        product.setDefaults();
        // Drive the engine linearly: a deliberately asymmetric valve or tape
        // stage does not, and should not, produce a mirrored output from a
        // mirrored input. What must hold is that PHASE itself inverts.
        for (const auto* id : { "drive", "saturation", "noise", "hiss", "crosstalk" })
            if (const auto* descriptor = product.find(id)) product.set(id, descriptor->minimum);
        if (product.find("bias") != nullptr) product.set("bias", 0.0f);
        if (const auto* symmetric = product.find("harmonic_balance"))
            product.set("harmonic_balance", symmetric->maximum);
        if (product.find("soft_clip") != nullptr) product.set("soft_clip", 0.0f);
        product.set("phase", phase);
        product.restart();
        return render(product);
    };
    const auto normal = capture(0.0f);
    auto inverted = capture(1.0f);
    inverted.applyGain(-1.0f);
    const auto residual = difference(normal, inverted) / juce::jmax(1.0e-12, energy(normal));
    check(residual < 1.0e-6, product.spec.displayName + ": PHASE inverts the signal (residual "
                                 + juce::String(residual, 9) + ")");
}

/** MIX at 0 % must be the dry signal. */
void auditDryMix(Product& product)
{
    const auto* mix = product.find("mix");
    if (mix == nullptr) return;
    product.prepare(48000.0, 512);
    product.setDefaults();
    product.set("mix", 0.0f);
    product.dsp.reset();
    product.frontEnd.reset();

    juce::AudioBuffer<float> buffer(2, 512), reference(2, 512), earlier(2, 512);
    earlier.clear();
    double worst = 0.0;
    for (int i = 0; i < 8; ++i)
    {
        fillProgramme(buffer, 48000.0, i * 512);
        reference.makeCopyOf(buffer);
        product.run(buffer);
        const auto aligned = delayed(reference, earlier, product.dsp.latencySamples());
        earlier.makeCopyOf(reference);
        if (i >= 2) worst = juce::jmax(worst, difference(buffer, aligned) / juce::jmax(1.0e-12, energy(aligned)));
    }
    check(worst < 1.0e-6, product.spec.displayName + ": MIX 0 % is the dry signal");
}

/** A high-pass must actually remove low end. */
void auditHighPass(Product& product)
{
    const auto* hpf = product.find("hpf");
    if (hpf == nullptr) return;
    product.prepare(48000.0, 1024);

    auto lowEnergy = [&](float setting)
    {
        product.setDefaults();
        product.set("hpf", setting);
        product.dsp.reset();
        product.frontEnd.reset();
        juce::AudioBuffer<float> buffer(2, 1024);
        for (int i = 0; i < 8; ++i)
        {
            fillTone(buffer, 48000.0, 40.0);
            product.run(buffer);
        }
        return energy(buffer);
    };

    const auto open = lowEnergy(hpf->minimum);
    const auto closed = lowEnergy(hpf->maximum);
    check(closed < open * 0.6, product.spec.displayName + ": HPF removes low end (40 Hz energy "
                                   + juce::String(closed / juce::jmax(1.0e-12, open), 3) + " of open)");
}

/** A compressor must reduce gain on a loud input, and must report what it did. */
void auditCompression(Product& product)
{
    const auto& id = product.spec.id;
    if (id != "A06" && id != "A07" && id != "A08") return;
    product.prepare(48000.0, 512);
    product.setDefaults();
    if (product.find("threshold") != nullptr) product.set("threshold", -30.0f);
    if (product.find("peak_reduction") != nullptr) product.set("peak_reduction", 90.0f);
    if (product.find("makeup") != nullptr) product.set("makeup", 0.0f);
    if (product.find("gain") != nullptr) product.set("gain", 0.0f);
    if (product.find("input") != nullptr) product.set("input", 0.0f);
    if (product.find("output") != nullptr) product.set("output", 0.0f);
    if (product.find("saturation") != nullptr) product.set("saturation", 0.0f);
    product.dsp.reset();
    product.frontEnd.reset();

    juce::AudioBuffer<float> quiet(2, 512), loud(2, 512);
    float quietOut = 0.0f, loudOut = 0.0f;
    for (int i = 0; i < 24; ++i)
    {
        fillTone(quiet, 48000.0, 220.0, 0.02f);
        product.run(quiet);
        quietOut = peak(quiet);
    }
    product.dsp.reset();
    product.frontEnd.reset();
    for (int i = 0; i < 24; ++i)
    {
        fillTone(loud, 48000.0, 220.0, 0.7f);
        product.run(loud);
        loudOut = peak(loud);
    }

    // 0.02 -> 0.7 is 31 dB in; a working compressor gives back much less than that
    const auto inputRatio = 0.7f / 0.02f;
    const auto outputRatio = loudOut / juce::jmax(1.0e-6f, quietOut);
    check(outputRatio < inputRatio * 0.7f,
          product.spec.displayName + ": compresses a 31 dB level rise to "
              + juce::String(juce::Decibels::gainToDecibels(outputRatio), 1) + " dB");
    check(product.dsp.gainReductionDb() < -0.5f,
          product.spec.displayName + ": reports gain reduction ("
              + juce::String(product.dsp.gainReductionDb(), 1) + " dB)");
}

/** STRIKE FET's external sidechain only exists when the host gives it a
    sidechain bus, so it is driven with one here. */
void auditExternalSidechain(Product& product)
{
    if (product.find("external_sc") == nullptr) return;
    product.prepare(48000.0, 512);

    auto run = [&](bool external)
    {
        product.setEngaged();
        product.set("external_sc", external ? 1.0f : 0.0f);
        product.restart();
        juce::AudioBuffer<float> buffer(4, 512);
        for (int i = 0; i < 16; ++i)
        {
            juce::AudioBuffer<float> main(2, 512);
            fillProgramme(main, 48000.0, i * 512);
            for (int channel = 0; channel < 2; ++channel)
            {
                buffer.copyFrom(channel, 0, main, channel, 0, 512);
                // a loud, unrelated key signal on the sidechain pair
                for (int n = 0; n < 512; ++n)
                    buffer.setSample(channel + 2, n, 0.9f * static_cast<float>(
                        std::sin(juce::MathConstants<double>::twoPi * 90.0 * (i * 512 + n) / 48000.0)));
            }
            product.run(buffer);
        }
        juce::AudioBuffer<float> result(2, 512);
        for (int channel = 0; channel < 2; ++channel) result.copyFrom(channel, 0, buffer, channel, 0, 512);
        return result;
    };

    const auto internal = run(false);
    const auto external = run(true);
    check(difference(internal, external) > 1.0e-9,
          product.spec.displayName + ": EXT SC keys the compressor from the sidechain bus");
}

/** The bank that ships with the product. Every preset must name real
    parameters, must actually change the sound, and must never be the thing
    that makes a customer's first five minutes go wrong. */
void auditFactoryBank(Product& product)
{
    int size = 0;
    const auto resource = product.spec.id + "_factory_json";
    const auto* data = AmanorsacBinaryData::getNamedResource(resource.toRawUTF8(), size);
    if (data == nullptr || size <= 0)
    {
        check(false, product.spec.displayName + ": ships a factory preset bank");
        return;
    }

    const auto bank = juce::JSON::parse(juce::String::fromUTF8(data, size));
    const auto* presets = bank.getProperty("presets", {}).getArray();
    check(presets != nullptr && presets->size() >= 10,
          product.spec.displayName + ": factory bank holds "
              + juce::String(presets != nullptr ? presets->size() : 0) + " presets");
    if (presets == nullptr) return;

    juce::StringArray names, unknown, silent, uncategorised, strangeCategory;
    amanorsac::presets::PresetManager manager(product.harness.state, product.spec);
    product.prepare(48000.0, 512);

    for (const auto& entry : *presets)
    {
        auto* object = entry.getDynamicObject();
        if (object == nullptr) continue;
        const auto name = object->getProperty("name").toString();
        names.add(name);

        // every preset must sit in a group a customer can find it in
        const auto category = object->getProperty("category").toString();
        if (category.isEmpty()) uncategorised.add(name);
        else if (! amanorsac::presets::PresetManager::categoryOrder().contains(category))
            strangeCategory.add(name + "/" + category);

        // every id in the bank must exist in this product's contract
        if (const auto* values = object->getProperty("values").getDynamicObject())
            for (const auto& property : values->getProperties())
                if (product.find(property.name.toString()) == nullptr)
                    unknown.add(name + "/" + property.name.toString());
    }

    check(uncategorised.isEmpty(), product.spec.displayName + ": every preset names a category"
              + (uncategorised.isEmpty() ? juce::String() : " (" + uncategorised.joinIntoString(", ") + ")"));
    check(strangeCategory.isEmpty(), product.spec.displayName + ": every category is one the menu offers"
              + (strangeCategory.isEmpty() ? juce::String() : " (" + strangeCategory.joinIntoString(", ") + ")"));

    juce::StringArray unique(names);
    unique.removeDuplicates(false);
    check(unique.size() == names.size(), product.spec.displayName + ": every preset name is unique");
    check(manager.categories().size() >= 6, product.spec.displayName + ": the bank spans "
              + juce::String(manager.categories().size()) + " categories ("
              + manager.categories().joinIntoString(", ") + ")");
    check(unknown.isEmpty(), product.spec.displayName + ": every preset names real parameters"
              + (unknown.isEmpty() ? juce::String() : " (unknown: " + unknown.joinIntoString(", ") + ")"));

    // and every one of them must be audible against the product's defaults
    product.setDefaults();
    product.restart();
    const auto reference = render(product, 16);

    const auto& listed = manager.presets();
    check(static_cast<int>(listed.size()) == names.size() + 1,
          product.spec.displayName + ": the bank is listed alongside Default ("
              + juce::String(static_cast<int>(listed.size())) + " entries)");

    for (int i = 1; i < static_cast<int>(listed.size()); ++i)
    {
        if (! manager.load(i)) { silent.add(listed[static_cast<size_t>(i)].name + " (would not load)"); continue; }
        product.restart();
        const auto rendered = render(product, 16);
        if (! finite(rendered)) silent.add(listed[static_cast<size_t>(i)].name + " (not finite)");
        else if (difference(rendered, reference) <= 1.0e-9)
            silent.add(listed[static_cast<size_t>(i)].name + " (no change)");
    }
    check(silent.isEmpty(), product.spec.displayName + ": every factory preset loads and changes the sound"
              + (silent.isEmpty() ? juce::String() : " (" + silent.joinIntoString(", ") + ")"));
}

/** Settings must survive a session save and reload. */
void auditStateRecall(Product& product)
{
    product.setDefaults();
    for (const auto& descriptor : product.spec.parameters)
        product.set(descriptor.id, juce::jmap(0.37f, descriptor.minimum, descriptor.maximum));
    const auto saved = product.harness.state.copyState();
    product.setDefaults();
    product.harness.state.replaceState(saved.createCopy());

    bool recalled = true;
    for (const auto& descriptor : product.spec.parameters)
    {
        const auto* raw = product.harness.state.getRawParameterValue(descriptor.id);
        const auto expected = juce::jmap(0.37f, descriptor.minimum, descriptor.maximum);
        if (raw == nullptr) { recalled = false; continue; }
        if (std::abs(raw->load() - expected) > juce::jmax(0.51f, std::abs(expected) * 0.02f)) recalled = false;
    }
    check(recalled, product.spec.displayName + ": every parameter survives a state round-trip");
}

// ------------------------------------------------------------------- rack

/** A module inside the rack must be the same processor as the module on its
    own. Anything else and a customer's rack recall does not match their track. */
void auditRackFidelity()
{
    beginSection("rack: a module in the rack is the module");

    for (int slot = 0; slot < amanorsac::RackProcessor::moduleCount; ++slot)
    {
        amanorsac::RackProcessor rack;
        rack.setPlayConfigDetails(2, 2, 48000.0, 512);
        rack.prepareToPlay(48000.0, 512);

        const auto& module = rack.moduleSpec(slot);
        const auto prefix = amanorsac::RackProcessor::modulePrefix(slot);

        Product standalone(juce::String::fromUTF8(amanorsac::generated::rackModuleJson[static_cast<size_t>(slot)]));
        standalone.prepare(48000.0, 512);
        standalone.setDefaults();

        // Same non-default settings on both sides.
        if (auto* enable = rack.state.getParameter("slot." + module.id + ".enabled"))
            enable->setValueNotifyingHost(1.0f);
        for (const auto& descriptor : module.parameters)
        {
            const auto target = juce::jmap(0.62f, descriptor.minimum, descriptor.maximum);
            standalone.set(descriptor.id, target);
            if (auto* parameter = rack.state.getParameter(prefix + descriptor.id))
                parameter->setValueNotifyingHost(parameter->convertTo0to1(target));
        }

        juce::MidiBuffer midi;
        juce::AudioBuffer<float> rackBuffer(2, 512), soloBuffer(2, 512);
        double worst = 0.0;
        for (int i = 0; i < 8; ++i)
        {
            fillProgramme(rackBuffer, 48000.0, i * 512);
            soloBuffer.makeCopyOf(rackBuffer);
            rack.processBlock(rackBuffer, midi);
            standalone.run(soloBuffer);
            // The first blocks include the rack's 20 ms enable ramp.
            if (i >= 4) worst = juce::jmax(worst, difference(rackBuffer, soloBuffer)
                                                      / juce::jmax(1.0e-12, energy(soloBuffer)));
        }
        check(worst < 1.0e-6, module.displayName + " in slot " + juce::String(slot + 1)
                                  + " matches the plugin (residual " + juce::String(worst, 9) + ")");
    }
}

/** The routing the rack exists for: reordering, solo, and two parallel lanes
    that stay aligned when one of them reports latency. */
void auditRackRouting()
{
    beginSection("rack: routing");

    auto make = []
    {
        auto rack = std::make_unique<amanorsac::RackProcessor>();
        rack->setPlayConfigDetails(2, 2, 48000.0, 512);
        rack->prepareToPlay(48000.0, 512);
        return rack;
    };
    auto enable = [](amanorsac::RackProcessor& rack, const juce::String& id, float value)
    {
        if (auto* parameter = rack.state.getParameter(id))
            parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
    };
    auto run = [](amanorsac::RackProcessor& rack, int blocks = 8)
    {
        juce::MidiBuffer midi;
        juce::AudioBuffer<float> buffer(2, 512);
        for (int i = 0; i < blocks; ++i)
        {
            fillProgramme(buffer, 48000.0, i * 512);
            rack.processBlock(buffer, midi);
        }
        return buffer;
    };

    // 1. order changes the sound: a saturator into an EQ is not an EQ into a
    //    saturator, and the rack must honour which came first.
    {
        auto first = make();
        enable(*first, "slot.A05.enabled", 1.0f);   // VALVE DRIVE
        enable(*first, "slot.A09.enabled", 1.0f);   // SILK PASSIVE EQ
        enable(*first, "A05.drive", 18.0f);
        enable(*first, "A09.low_boost", 9.0f);
        first->moveSlot(4, amanorsac::RackProcessor::Lane::a, 0);
        const auto driveFirst = run(*first);

        auto second = make();
        enable(*second, "slot.A05.enabled", 1.0f);
        enable(*second, "slot.A09.enabled", 1.0f);
        enable(*second, "A05.drive", 18.0f);
        enable(*second, "A09.low_boost", 9.0f);
        second->moveSlot(8, amanorsac::RackProcessor::Lane::a, 0);
        const auto eqFirst = run(*second);

        check(difference(driveFirst, eqFirst) > 1.0e-7, "module order changes the result");
        check(second->positionOf(8) < second->positionOf(4), "a dragged module keeps its new position");
    }

    // 2. solo overrides the switches and leaves only the soloed module in
    {
        auto soloed = make();
        for (const auto* id : { "slot.A05.enabled", "slot.A09.enabled", "slot.A01.enabled" })
            enable(*soloed, id, 1.0f);
        enable(*soloed, "A05.drive", 18.0f);
        enable(*soloed, "slot.A05.solo", 1.0f);
        const auto solo = run(*soloed);

        auto alone = make();
        enable(*alone, "slot.A05.enabled", 1.0f);
        enable(*alone, "A05.drive", 18.0f);
        const auto only = run(*alone);

        check(difference(solo, only) / juce::jmax(1.0e-12, energy(only)) < 1.0e-6,
              "solo leaves only the soloed module in the chain");
        check(soloed->isActive(4) && ! soloed->isActive(8), "solo overrides the module switches");
    }

    // 3. splitting with nothing in lane B must not change the level
    {
        auto series = make();
        enable(*series, "slot.A05.enabled", 1.0f);
        enable(*series, "A05.drive", 18.0f);
        const auto before = run(*series);
        enable(*series, "split", 1.0f);
        const auto after = run(*series);
        check(difference(before, after) / juce::jmax(1.0e-12, energy(before)) < 1.0e-6,
              "split with an empty lane B is still one chain");
    }

    // 4. two lanes sum: lane A processed against a clean parallel lane B
    {
        auto split = make();
        enable(*split, "split", 1.0f);
        enable(*split, "slot.A05.enabled", 1.0f);
        enable(*split, "A05.drive", 18.0f);
        split->moveSlot(9, amanorsac::RackProcessor::Lane::b, 0);   // PLATE FOUR to lane B
        check(split->laneOf(9) == amanorsac::RackProcessor::Lane::b, "a module can be moved to lane B");
        check(split->laneOrder(amanorsac::RackProcessor::Lane::a).size() == 9,
              "the other modules stay on lane A");

        const auto both = run(*split);
        check(finite(both), "the split rack stays finite");

        // Muting lane B must remove its contribution entirely.
        enable(*split, "lane_b_level", -24.0f);
        const auto quietB = run(*split);
        enable(*split, "lane_b_level", 0.0f);
        const auto loudB = run(*split);
        check(difference(quietB, loudB) > 1.0e-9, "the lane B level changes what lane B contributes");

        // Polarity on lane B against an identical lane A cancels.
        auto cancel = make();
        enable(*cancel, "split", 1.0f);
        cancel->moveSlot(9, amanorsac::RackProcessor::Lane::b, 0);
        enable(*cancel, "lane_b_phase", 1.0f);
        juce::MidiBuffer midi;
        juce::AudioBuffer<float> buffer(2, 512);
        float worst = 0.0f;
        for (int i = 0; i < 8; ++i)
        {
            fillProgramme(buffer, 48000.0, i * 512);
            cancel->processBlock(buffer, midi);
            if (i >= 2) worst = juce::jmax(worst, peak(buffer));
        }
        check(worst < 1.0e-4f, "flipping lane B polarity cancels two identical lanes (peak "
                                   + juce::String(worst, 8) + ")");
    }

    // 5. parallel lanes are latency aligned, so the split cannot comb
    {
        auto aligned = make();
        enable(*aligned, "split", 1.0f);
        enable(*aligned, "slot.A01.enabled", 1.0f);
        enable(*aligned, "A01.oversampling", 2.0f);   // HERITAGE EQ reports latency at 4x
        aligned->moveSlot(9, amanorsac::RackProcessor::Lane::b, 0);
        run(*aligned);
        const auto reported = aligned->getLatencySamples();
        check(reported > 0, "the rack reports the latency of its longest lane ("
                                + juce::String(reported) + " samples)");

        // Where the two lanes actually land in time. An impulse through the
        // oversampled lane arrives late by exactly the reported latency; the
        // clean lane must be delayed to meet it, or the split combs.
        auto impulsePeak = [&](float laneALevel, float laneBLevel)
        {
            auto rack = make();
            enable(*rack, "split", 1.0f);
            enable(*rack, "slot.A01.enabled", 1.0f);
            enable(*rack, "A01.oversampling", 2.0f);
            enable(*rack, "A01.eq_in", 0.0f);
            enable(*rack, "A01.filter_in", 0.0f);
            enable(*rack, "A01.character", 0.0f);
            rack->moveSlot(9, amanorsac::RackProcessor::Lane::b, 0);
            enable(*rack, "slot.A10.enabled", 0.0f);
            enable(*rack, "lane_a_level", laneALevel);
            enable(*rack, "lane_b_level", laneBLevel);

            juce::MidiBuffer midi;
            juce::AudioBuffer<float> buffer(2, 512);
            for (int i = 0; i < 4; ++i) { buffer.clear(); rack->processBlock(buffer, midi); }
            buffer.clear();
            buffer.setSample(0, 100, 1.0f);
            buffer.setSample(1, 100, 1.0f);
            rack->processBlock(buffer, midi);

            auto peakIndex = 0;
            auto largest = 0.0f;
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                if (const auto magnitude = std::abs(buffer.getSample(0, i)); magnitude > largest)
                { largest = magnitude; peakIndex = i; }
            return peakIndex;
        };

        const auto laneAArrival = impulsePeak(0.0f, -24.0f);
        const auto laneBArrival = impulsePeak(-24.0f, 0.0f);
        check(std::abs(laneAArrival - laneBArrival) <= 1,
              "both lanes deliver a transient at the same sample (lane A at " + juce::String(laneAArrival)
                  + ", lane B at " + juce::String(laneBArrival) + ")");
        check(laneBArrival > 100, "the clean lane is delayed to match the lane that reports latency");
    }

    // 6. the arrangement is part of the session
    {
        auto arranged = make();
        enable(*arranged, "split", 1.0f);
        arranged->moveSlot(9, amanorsac::RackProcessor::Lane::b, 0);
        arranged->moveSlot(0, amanorsac::RackProcessor::Lane::a, 4);
        const auto laneOfPlate = arranged->laneOf(9);
        const auto positionOfHeritage = arranged->positionOf(0);

        juce::MemoryBlock saved;
        arranged->getStateInformation(saved);
        arranged->moveSlot(9, amanorsac::RackProcessor::Lane::a, 0);
        arranged->setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));

        check(arranged->laneOf(9) == laneOfPlate && arranged->positionOf(0) == positionOfHeritage,
              "the arrangement survives a session save and reload");
    }
}

void auditRackChain()
{
    beginSection("rack: chain behaviour");

    amanorsac::RackProcessor rack;
    rack.setPlayConfigDetails(2, 2, 48000.0, 512);
    rack.prepareToPlay(48000.0, 512);
    juce::MidiBuffer midi;

    // 1. every slot off is a wire
    juce::AudioBuffer<float> buffer(2, 512), reference(2, 512);
    double worst = 0.0;
    for (int i = 0; i < 8; ++i)
    {
        fillProgramme(buffer, 48000.0, i * 512);
        reference.makeCopyOf(buffer);
        rack.processBlock(buffer, midi);
        if (i >= 2) worst = juce::jmax(worst, difference(buffer, reference) / juce::jmax(1.0e-12, energy(reference)));
    }
    check(worst < 1.0e-9, "all slots off passes the input through untouched");

    // 2. every slot on stays finite and bounded
    for (int slot = 0; slot < amanorsac::RackProcessor::moduleCount; ++slot)
        if (auto* enable = rack.state.getParameter("slot.A" + juce::String(slot + 1).paddedLeft('0', 2) + ".enabled"))
            enable->setValueNotifyingHost(1.0f);

    bool safe = true, bounded = true;
    for (int i = 0; i < 48; ++i)
    {
        fillProgramme(buffer, 48000.0, i * 512);
        rack.processBlock(buffer, midi);
        if (! finite(buffer)) safe = false;
        if (peak(buffer) > 32.0f) bounded = false;
    }
    check(safe, "all ten modules in series stay finite");
    check(bounded, "all ten modules in series stay bounded");

    // 3. enabling a slot mid-stream does not click
    amanorsac::RackProcessor smooth;
    smooth.setPlayConfigDetails(2, 2, 48000.0, 512);
    smooth.prepareToPlay(48000.0, 512);
    for (int i = 0; i < 4; ++i) { fillProgramme(buffer, 48000.0, i * 512); smooth.processBlock(buffer, midi); }
    if (auto* enable = smooth.state.getParameter("slot.A05.enabled")) enable->setValueNotifyingHost(1.0f);
    fillProgramme(buffer, 48000.0, 4 * 512);
    smooth.processBlock(buffer, midi);
    auto biggestStep = 0.0f;
    for (int i = 1; i < buffer.getNumSamples(); ++i)
        biggestStep = juce::jmax(biggestStep, std::abs(buffer.getSample(0, i) - buffer.getSample(0, i - 1)));
    fillProgramme(reference, 48000.0, 5 * 512);
    smooth.processBlock(reference, midi);
    auto settledStep = 0.0f;
    for (int i = 1; i < reference.getNumSamples(); ++i)
        settledStep = juce::jmax(settledStep, std::abs(reference.getSample(0, i) - reference.getSample(0, i - 1)));
    check(biggestStep <= settledStep * 1.5f + 0.02f,
          "enabling a slot mid-stream adds no step (" + juce::String(biggestStep, 4)
              + " against " + juce::String(settledStep, 4) + " while running)");

    // 4. the rack's own bypass and mix
    amanorsac::RackProcessor wet;
    wet.setPlayConfigDetails(2, 2, 48000.0, 512);
    wet.prepareToPlay(48000.0, 512);
    if (auto* enable = wet.state.getParameter("slot.A05.enabled")) enable->setValueNotifyingHost(1.0f);
    if (auto* mix = wet.state.getParameter("mix")) mix->setValueNotifyingHost(0.0f);
    worst = 0.0;
    for (int i = 0; i < 8; ++i)
    {
        fillProgramme(buffer, 48000.0, i * 512);
        reference.makeCopyOf(buffer);
        wet.processBlock(buffer, midi);
        if (i >= 2) worst = juce::jmax(worst, difference(buffer, reference) / juce::jmax(1.0e-12, energy(reference)));
    }
    check(worst < 1.0e-9, "rack MIX 0 % is the dry signal");

    // 5. every module parameter is a real host parameter and is saved
    amanorsac::RackProcessor recall;
    const auto expected = 14 + 169;
    juce::ignoreUnused(expected);
    int moduleParameters = 0;
    for (int slot = 0; slot < amanorsac::RackProcessor::moduleCount; ++slot)
    {
        const auto prefix = amanorsac::RackProcessor::modulePrefix(slot);
        for (const auto& descriptor : recall.moduleSpec(slot).parameters)
            if (recall.state.getParameter(prefix + descriptor.id) != nullptr) ++moduleParameters;
            else check(false, "missing rack parameter " + prefix + descriptor.id);
    }
    check(moduleParameters > 150, "every module control is a host parameter ("
                                      + juce::String(moduleParameters) + " module parameters)");

    if (auto* parameter = recall.state.getParameter("A01.band.low.gain"))
        parameter->setValueNotifyingHost(parameter->convertTo0to1(7.5f));
    juce::MemoryBlock saved;
    recall.getStateInformation(saved);
    if (auto* parameter = recall.state.getParameter("A01.band.low.gain"))
        parameter->setValueNotifyingHost(parameter->convertTo0to1(0.0f));
    recall.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
    const auto* restored = recall.state.getRawParameterValue("A01.band.low.gain");
    check(restored != nullptr && std::abs(restored->load() - 7.5f) < 0.05f,
          "module settings survive a session save and reload");
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI initialiseJuce;

    for (size_t i = 0; i < 10; ++i)
    {
        Product product(juce::String::fromUTF8(amanorsac::generated::rackModuleJson[i]));
        beginSection(product.spec.displayName + " (" + juce::String(product.spec.parameters.size())
                     + " parameters)");
        product.prepare(48000.0, 512);
        auditEveryParameterIsAudible(product);
        auditNumericalSafety(product);
        auditSilence(product);
        auditBypass(product);
        auditPhase(product);
        auditDryMix(product);
        auditHighPass(product);
        auditCompression(product);
        auditExternalSidechain(product);
        auditFactoryBank(product);
        auditStateRecall(product);
    }

    auditRackFidelity();
    auditRackChain();
    auditRackRouting();

    std::cout << std::endl
              << (failures == 0 ? "PASS" : "FAIL") << ": analog engine audit, " << failures
              << " failure(s)" << std::endl;
    return failures == 0 ? 0 : 1;
}

