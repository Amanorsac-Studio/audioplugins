#include "FaceplateEditor.h"

#include "AnalogUI.h"
#include "BinaryData.h"

#include <cmath>

namespace amanorsac
{
namespace
{
using namespace analog;

constexpr float rotaryStart = juce::MathConstants<float>::pi * 1.24f;   // 223 degrees
constexpr float rotaryEnd = juce::MathConstants<float>::pi * 2.76f;     // 497 degrees

// ------------------------------------------------------------ binary lookup

juce::MemoryBlock namedResource(const juce::String& name)
{
    int size = 0;
    if (const auto* data = AmanorsacBinaryData::getNamedResource(name.toRawUTF8(), size))
        return { data, static_cast<size_t>(size) };
    return {};
}

juce::Image spriteImage(const juce::String& regionId)
{
    const auto block = namedResource(regionId + "_png");
    if (block.getSize() == 0) return {};
    return juce::ImageFileFormat::loadFrom(block.getData(), block.getSize());
}

juce::Image faceplateImage()
{
    for (int i = 0; i < AmanorsacBinaryData::namedResourceListSize; ++i)
    {
        const juce::String name(AmanorsacBinaryData::namedResourceList[i]);
        if (name.contains("FACEPLATE"))
        {
            const auto block = namedResource(name);
            if (block.getSize() > 0)
                return juce::ImageFileFormat::loadFrom(block.getData(), block.getSize());
        }
    }
    return {};
}

// ------------------------------------------------------------------ regions

struct Region
{
    juce::String id, kind, shape;
    juce::Rectangle<int> bounds;
    float radius = 0.0f;
};

struct Geometry
{
    int width = 0, height = 0;
    std::vector<Region> regions;
    bool valid() const noexcept { return width > 0 && height > 0 && ! regions.empty(); }
};

Geometry loadGeometry(const juce::String& pluginId)
{
    Geometry geometry;
    const auto block = namedResource(pluginId + "_json");
    if (block.getSize() == 0) return geometry;

    const auto parsed = juce::JSON::parse(block.toString());
    if (! parsed.isObject()) return geometry;

    geometry.width = static_cast<int>(parsed.getProperty("width", 0));
    geometry.height = static_cast<int>(parsed.getProperty("height", 0));

    if (const auto* list = parsed.getProperty("regions", {}).getArray())
    {
        for (const auto& entry : *list)
        {
            Region region;
            region.id = entry.getProperty("id", {}).toString();
            region.kind = entry.getProperty("kind", {}).toString();
            region.shape = entry.getProperty("shape", {}).toString();
            region.radius = static_cast<float>(static_cast<double>(entry.getProperty("r", 0.0)));

            if (const auto* bounds = entry.getProperty("bounds", {}).getArray())
                if (bounds->size() == 4)
                    region.bounds = { static_cast<int>((*bounds)[0]), static_cast<int>((*bounds)[1]),
                                      static_cast<int>((*bounds)[2]), static_cast<int>((*bounds)[3]) };

            if (region.id.isNotEmpty() && ! region.bounds.isEmpty())
                geometry.regions.push_back(std::move(region));
        }
    }
    return geometry;
}

// --------------------------------------------------------------- parameters

const ParameterDescriptor* findParameter(const PluginSpec& spec, const juce::String& id)
{
    for (const auto& descriptor : spec.parameters)
        if (descriptor.id == id)
            return &descriptor;
    return nullptr;
}

/** Region ids describe artwork, not DSP. This resolves the artwork name onto the
    authoritative parameter contract without inventing controls: duplicated
    artwork (a trim and a row knob for the same stage) drives the same
    parameter, and anything unmatched stays inert rather than being faked. */
juce::String parameterForRegion(const PluginSpec& spec, const juce::String& regionId)
{
    auto direct = [&spec](const juce::String& candidate) -> juce::String
    {
        return findParameter(spec, candidate) != nullptr ? candidate : juce::String();
    };

    if (auto hit = direct(regionId); hit.isNotEmpty()) return hit;

    static const char* suffixes[] = { "_trim", "_value", "_lower", "_on", "_drive", "_stage" };
    for (const auto* suffix : suffixes)
        if (regionId.endsWith(suffix))
            if (auto hit = direct(regionId.dropLastCharacters(static_cast<int>(std::strlen(suffix)))); hit.isNotEmpty())
                return hit;

    // artwork spellings that differ from the contract wording
    static const std::pair<const char*, const char*> aliases[] = {
        { "predelay", "pre_delay" }, { "damping", "damp" }, { "basscut", "bass_cut" },
        { "peak", "peak_reduction" }, { "sc_hpf", "sc_hpf" }, { "hpf", "hpf" },
        { "vu_gr", "" }, { "treble", "treble" }, { "gain", "gain" }
    };
    for (const auto& [artwork, contract] : aliases)
        if (regionId == artwork && juce::String(contract).isNotEmpty())
            if (auto hit = direct(contract); hit.isNotEmpty()) return hit;

    return {};
}

/** Choice buttons printed as separate keys (character_a/b/c) map onto one
    enum parameter plus the index that key selects. */
bool choiceForRegion(const PluginSpec& spec, const juce::String& regionId,
                     juce::String& parameterId, int& index)
{
    static const char* families[] = { "transformer_", "character_", "formula_", "plate_", "topology_" };
    for (const auto* family : families)
    {
        if (! regionId.startsWith(family)) continue;
        const auto key = regionId.fromFirstOccurrenceOf("_", false, false);
        const auto stem = juce::String(family).dropLastCharacters(1);

        const ParameterDescriptor* descriptor = findParameter(spec, stem);
        if (descriptor == nullptr)
            for (const auto& candidate : spec.parameters)
                if (candidate.kind == ParameterDescriptor::Kind::choice)
                    { descriptor = &candidate; break; }
        if (descriptor == nullptr || descriptor->kind != ParameterDescriptor::Kind::choice) return false;

        // a/b/c/d select by position; anything else matches the option text
        if (key.length() == 1 && key[0] >= 'a' && key[0] <= 'z')
            index = key[0] - 'a';
        else
            index = descriptor->choices.indexOf(key, true);

        if (index < 0 || index >= descriptor->choices.size()) return false;
        parameterId = descriptor->id;
        return true;
    }
    return false;
}

juce::String formatValue(const ParameterDescriptor& descriptor, float value)
{
    if (descriptor.kind == ParameterDescriptor::Kind::choice)
    {
        const auto index = juce::jlimit(0, descriptor.choices.size() - 1, static_cast<int>(std::round(value)));
        return descriptor.choices.isEmpty() ? juce::String() : descriptor.choices[index].toUpperCase();
    }
    if (descriptor.kind == ParameterDescriptor::Kind::boolean)
        return value > 0.5f ? "ON" : "OFF";

    const auto span = descriptor.maximum - descriptor.minimum;
    const auto decimals = span > 200.0f ? 0 : (span > 20.0f ? 1 : 2);
    return juce::String(value, decimals);
}

// ------------------------------------------------------------- sprite knob

/** Slider that renders the photographed knob, rotated by the delta between the
    value's angle and the pose the knob was photographed in. */
class SpriteKnob final : public juce::Slider
{
public:
    SpriteKnob(juce::Image sprite, float picturedPose)
        : art(std::move(sprite)), pose(picturedPose)
    {
        setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        setRotaryParameters(rotaryStart, rotaryEnd, true);
        setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
        setMouseDragSensitivity(210);
        setWantsKeyboardFocus(true);
    }

    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override
    {
        auto adjusted = wheel;
        if (event.mods.isShiftDown()) { adjusted.deltaY *= 0.15f; adjusted.deltaX *= 0.15f; }
        juce::Slider::mouseWheelMove(event, adjusted);
    }

    void paint(juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        const auto centre = bounds.getCentre();

        if (art.isValid())
        {
            const auto proportion = getRange().getLength() > 0.0
                ? static_cast<float>((getValue() - getMinimum()) / getRange().getLength()) : 0.0f;
            const auto target = rotaryStart + proportion * (rotaryEnd - rotaryStart);

            juce::Graphics::ScopedSaveState state(g);
            g.addTransform(juce::AffineTransform::rotation(target - pose, centre.x, centre.y));
            g.drawImage(art, bounds, juce::RectanglePlacement::centred);
        }

        if (isMouseOverOrDragging())
        {
            g.setColour(juce::Colours::white.withAlpha(0.10f));
            g.fillEllipse(bounds.reduced(bounds.getWidth() * 0.06f));
        }
    }

private:
    juce::Image art;
    float pose;
};

/** Sprite-backed key that either selects an enum value, toggles a boolean, or
    performs a host action such as undo. */
class SpriteButton final : public juce::Component
{
public:
    SpriteButton(juce::Image sprite, juce::Colour accentColour)
        : art(std::move(sprite)), accent(accentColour) {}

    std::function<void()> onClick;
    std::function<bool()> isLit;

    void mouseDown(const juce::MouseEvent&) override { pressed = true; repaint(); }
    void mouseUp(const juce::MouseEvent& event) override
    {
        pressed = false;
        if (onClick && getLocalBounds().contains(event.getPosition())) onClick();
        repaint();
    }
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }

    void paint(juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        if (art.isValid()) g.drawImage(art, bounds, juce::RectanglePlacement::stretchToFit);

        const auto lit = isLit && isLit();
        if (lit)
        {
            g.setColour(accent.withAlpha(0.34f));
            g.fillRoundedRectangle(bounds, 4.0f);
            g.setColour(accent.withAlpha(0.85f));
            g.drawRoundedRectangle(bounds.reduced(0.7f), 4.0f, 1.4f);
        }
        if (interactive())
        {
            if (pressed) { g.setColour(juce::Colours::black.withAlpha(0.30f)); g.fillRoundedRectangle(bounds, 4.0f); }
            else if (isMouseOver()) { g.setColour(juce::Colours::white.withAlpha(0.10f)); g.fillRoundedRectangle(bounds, 4.0f); }
        }
    }

    bool interactive() const noexcept { return onClick != nullptr; }

private:
    juce::Image art;
    juce::Colour accent;
    bool pressed = false;
};

/** Indicator lamp: pictured artwork plus a live bloom when its source is on. */
class SpriteLamp final : public juce::Component
{
public:
    SpriteLamp(juce::Image sprite, juce::Colour glowColour)
        : art(std::move(sprite)), glow(glowColour) { setInterceptsMouseClicks(false, false); }

    std::function<float()> intensity;

    void paint(juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        if (art.isValid()) g.drawImage(art, bounds, juce::RectanglePlacement::stretchToFit);

        const auto level = intensity ? juce::jlimit(0.0f, 1.0f, intensity()) : 1.0f;
        if (level > 0.01f)
        {
            g.setColour(glow.withAlpha(0.30f * level));
            g.fillEllipse(bounds.expanded(bounds.getWidth() * 0.35f));
            g.setColour(glow.withAlpha(0.55f * level));
            g.fillEllipse(bounds.reduced(bounds.getWidth() * 0.18f));
        }
    }

private:
    juce::Image art;
    juce::Colour glow;
};

/** Live numeric window drawn into a readout aperture. */
class ValueReadout final : public juce::Component, private juce::Timer
{
public:
    ValueReadout(juce::AudioProcessorValueTreeState& tree, ParameterDescriptor d, juce::Colour colour)
        : state(tree), descriptor(std::move(d)), text(colour)
    {
        setInterceptsMouseClicks(false, false);
        startTimerHz(20);
    }

    void paint(juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        g.setColour(text);
        g.setFont(labelFont(juce::jmin(bounds.getHeight() * 0.72f, bounds.getWidth() * 0.30f), true, 0.94f));
        g.drawText(current, bounds, juce::Justification::centred, false);
    }

private:
    void timerCallback() override
    {
        if (const auto* value = state.getRawParameterValue(descriptor.id))
        {
            const auto formatted = formatValue(descriptor, value->load());
            if (formatted != current) { current = formatted; repaint(); }
        }
    }

    juce::AudioProcessorValueTreeState& state;
    ParameterDescriptor descriptor;
    juce::Colour text;
    juce::String current;
};

/** Free-standing text window (preset name and similar). */
class StaticReadout final : public juce::Component
{
public:
    StaticReadout(juce::String value, juce::Colour colour) : content(std::move(value)), text(colour)
    {
        setInterceptsMouseClicks(false, false);
    }

    void paint(juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        g.setColour(text);
        g.setFont(labelFont(juce::jmin(bounds.getHeight() * 0.52f, 20.0f), false, 0.98f));
        g.drawText(content, bounds, juce::Justification::centred, false);
    }

private:
    juce::String content;
    juce::Colour text;
};

/** Cream VU face sized to an aperture; the surrounding bezel is approved art. */
class PlateVu final : public juce::Component, private juce::Timer
{
public:
    PlateVu(std::function<float()> source, juce::String caption, bool inverted)
        : level(std::move(source)), title(std::move(caption)), reverse(inverted)
    {
        setInterceptsMouseClicks(false, false);
        startTimerHz(30);
    }

    void paint(juce::Graphics& g) override
    {
        auto face = getLocalBounds().toFloat();
        juce::ColourGradient plate(juce::Colour(0xfff2dcae), face.getCentreX(), face.getY(),
                                   juce::Colour(0xffd9ab68), face.getCentreX(), face.getBottom(), false);
        g.setGradientFill(plate);
        g.fillRect(face);

        const auto sweep = 0.40f;
        const auto radius = (face.getWidth() * 0.84f) / (2.0f * std::sin(sweep));
        const juce::Point<float> pivot(face.getCentreX(), face.getY() + face.getHeight() * 0.34f + radius);
        const auto redFrom = 0.81f;

        auto polar = [](juce::Point<float> c, float a, float r)
        { return c + juce::Point<float>(std::sin(a), -std::cos(a)) * r; };

        juce::Graphics::ScopedSaveState clip(g);
        g.reduceClipRegion(getLocalBounds());

        for (int i = 0; i <= 44; ++i)
        {
            const auto t = static_cast<float>(i) / 44.0f;
            const auto a = juce::jmap(t, 0.0f, 1.0f, -sweep, sweep);
            g.setColour(((! reverse && t >= redFrom) ? juce::Colour(0xffa8291b)
                                                       : juce::Colour(0xff33220f)).withAlpha(0.5f));
            g.drawLine({ polar(pivot, a, radius * 0.972f), polar(pivot, a, radius * 0.998f) }, 1.0f);
        }

        // A level meter is printed -20..+3 with a red overload shoulder; a gain
        // reduction meter counts decibels of reduction from a resting zero.
        static const std::array<const char*, 11> levelMarks {
            "-20", "-10", "-7", "-5", "-3", "-2", "-1", "0", "+1", "+2", "+3" };
        static const std::array<float, 11> levelStops {
            0.00f, 0.26f, 0.39f, 0.49f, 0.60f, 0.67f, 0.74f, 0.81f, 0.88f, 0.94f, 1.00f };
        static const std::array<const char*, 6> grMarks { "0", "4", "8", "12", "16", "20" };
        static const std::array<float, 6> grStops { 0.00f, 0.20f, 0.40f, 0.60f, 0.80f, 1.00f };

        const auto count = reverse ? grMarks.size() : levelMarks.size();
        for (size_t i = 0; i < count; ++i)
        {
            const auto stop = reverse ? grStops[i] : levelStops[i];
            const auto* text = reverse ? grMarks[i] : levelMarks[i];
            const auto a = juce::jmap(stop, 0.0f, 1.0f, -sweep, sweep);
            const auto red = ! reverse && stop >= redFrom;
            g.setColour(red ? juce::Colour(0xffa8291b) : juce::Colour(0xff2b1c0c));
            g.drawLine({ polar(pivot, a, radius * 0.958f), polar(pivot, a, radius * 1.002f) }, 2.0f);
            g.setFont(labelFont(face.getHeight() * 0.105f, true, 0.92f));
            g.drawText(juce::String(text),
                       juce::Rectangle<float>(face.getWidth() * 0.085f, face.getHeight() * 0.14f)
                           .withCentre(polar(pivot, a, radius * 1.044f)),
                       juce::Justification::centred, false);
        }

        if (! reverse)
        {
            juce::Path redArc;
            redArc.addCentredArc(pivot.x, pivot.y, radius * 1.006f, radius * 1.006f, 0.0f,
                                 juce::jmap(redFrom, 0.0f, 1.0f, -sweep, sweep), sweep, true);
            g.setColour(juce::Colour(0xffb02a1a));
            g.strokePath(redArc, juce::PathStrokeType(3.4f));
        }

        g.setColour(juce::Colour(0xff3a2712));
        g.setFont(labelFont(face.getHeight() * 0.095f, true, 0.9f));
        g.drawText(title, face.withHeight(face.getHeight() * 0.13f).translated(0.0f, face.getHeight() * 0.02f),
                   juce::Justification::centred, false);
        g.setFont(labelFont(face.getHeight() * 0.26f, true, 0.95f));
        g.setColour(juce::Colour(0xff2e1f0e));
        g.drawText("VU", face.withHeight(face.getHeight() * 0.26f).translated(0.0f, face.getHeight() * 0.52f),
                   juce::Justification::centred, false);

        drawBrandMark(g, face.withHeight(face.getHeight() * 0.13f).translated(0.0f, face.getHeight() * 0.80f)
                             .reduced(face.getWidth() * 0.33f, 0.0f),
                      juce::Colour(0xff54401f), juce::Colour(0xff3a2712));

        g.setColour(juce::Colour(0xff3a2712).withAlpha(0.85f));
        g.setFont(labelFont(face.getHeight() * 0.13f, true, 1.0f));
        g.drawText("-", face.withWidth(face.getWidth() * 0.10f).withHeight(face.getHeight() * 0.2f)
                            .translated(face.getWidth() * 0.05f, face.getHeight() * 0.68f),
                   juce::Justification::centred, false);
        g.drawText("+", face.withWidth(face.getWidth() * 0.10f).withHeight(face.getHeight() * 0.2f)
                            .translated(face.getWidth() * 0.85f, face.getHeight() * 0.68f),
                   juce::Justification::centred, false);

        const auto shown = needle;
        const auto a = juce::jmap(juce::jlimit(0.0f, 1.08f, shown), 0.0f, 1.0f, -sweep, sweep);
        g.setColour(juce::Colours::black.withAlpha(0.20f));
        g.drawLine({ polar(pivot, a, radius * 0.86f).translated(2.0f, 2.0f),
                     polar(pivot, a, radius * 0.985f).translated(2.0f, 2.0f) }, 2.4f);
        g.setColour(juce::Colour(0xff141414));
        g.drawLine({ polar(pivot, a, radius * 0.80f), polar(pivot, a, radius * 0.985f) }, 2.2f);
    }

private:
    void timerCallback() override
    {
        const auto target = level ? juce::jlimit(0.0f, 1.2f, level()) : 0.0f;
        needle += (target - needle) * (target > needle ? 0.34f : 0.12f);
        repaint();
    }

    std::function<float()> level;
    juce::String title;
    bool reverse;
    float needle = 0.0f;
};

/** Single LED ladder filling one column aperture. */
class LedColumn final : public juce::Component, private juce::Timer
{
public:
    explicit LedColumn(std::function<float()> source) : level(std::move(source))
    {
        setInterceptsMouseClicks(false, false);
        startTimerHz(30);
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        const int segments = juce::jlimit(8, 26, juce::roundToInt(bounds.getHeight() / 13.0f));
        const auto gap = bounds.getHeight() * 0.010f;
        const auto cell = (bounds.getHeight() - gap * static_cast<float>(segments - 1))
                        / static_cast<float>(segments);

        for (int i = 0; i < segments; ++i)
        {
            const auto value = 1.0f - static_cast<float>(i) / static_cast<float>(segments - 1);
            const auto lit = shown >= value;
            const auto cellArea = juce::Rectangle<float>(bounds.getX(),
                                                         bounds.getY() + static_cast<float>(i) * (cell + gap),
                                                         bounds.getWidth(), cell);
            const auto colour = value > 0.86f ? juce::Colour(0xffe0431f)
                              : value > 0.62f ? juce::Colour(0xfff0a51f)
                                              : juce::Colour(0xff3fbf46);
            if (lit)
            {
                g.setColour(colour.withAlpha(0.28f));
                g.fillRoundedRectangle(cellArea.expanded(1.8f), 2.0f);
                g.setColour(colour);
            }
            else g.setColour(colour.withAlpha(0.11f));
            g.fillRoundedRectangle(cellArea, 1.6f);
        }
    }

private:
    void timerCallback() override
    {
        const auto target = level ? juce::jlimit(0.0f, 1.0f, level()) : 0.0f;
        shown = juce::jmax(target, shown * 0.88f);
        repaint();
    }

    std::function<float()> level;
    float shown = 0.0f;
};
}

// ---------------------------------------------------------------- Surface

class FaceplateEditor::Surface final : public juce::Component
{
public:
    explicit Surface(PluginProcessor& owner)
        : processor(owner), palette(Palette::forProduct(owner.spec.id))
    {
        faceplate = faceplateImage();
        geometry = loadGeometry(owner.spec.id);

        canvas = { geometry.valid() ? geometry.width : faceplate.getWidth(),
                   geometry.valid() ? geometry.height : faceplate.getHeight() };
        if (canvas.x <= 0 || canvas.y <= 0) canvas = { 1536, 910 };

        buildControls();
        setSize(canvas.x, canvas.y);
    }

    ~Surface() override = default;

    juce::Point<int> canvasSize() const noexcept { return canvas; }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff0a0a09));
        if (faceplate.isValid())
            g.drawImage(faceplate, getLocalBounds().toFloat(), juce::RectanglePlacement::stretchToFit);
    }

    void resized() override
    {
        for (auto& [component, bounds] : placements)
            component->setBounds(bounds);
    }

private:
    float outputPeak() const
    {
        return juce::jmax(processor.getOutputPeak(0), processor.getOutputPeak(1));
    }

    static float vuLevel(float peak)
    {
        const auto db = juce::Decibels::gainToDecibels(juce::jmax(peak, 1.0e-5f));
        return juce::jlimit(0.0f, 1.15f, juce::jmap(db, -26.0f, 3.0f, 0.0f, 1.0f));
    }

    /** Decibels of level lost across the processor, scaled onto the printed
        0..20 dB gain reduction arc. */
    float gainReduction() const
    {
        const auto in = juce::jmax(processor.getInputPeak(0), processor.getInputPeak(1));
        const auto out = outputPeak();
        if (in < 1.0e-4f) return 0.0f;
        const auto reduced = juce::Decibels::gainToDecibels(in) - juce::Decibels::gainToDecibels(juce::jmax(out, 1.0e-6f));
        return juce::jlimit(0.0f, 1.0f, reduced / 20.0f);
    }

    static float ledLevel(float peak)
    {
        const auto db = juce::Decibels::gainToDecibels(juce::jmax(peak, 1.0e-6f));
        return juce::jlimit(0.0f, 1.0f, juce::jmap(db, -60.0f, 18.0f, 0.0f, 1.0f));
    }

    void place(std::unique_ptr<juce::Component> component, juce::Rectangle<int> bounds)
    {
        addAndMakeVisible(*component);
        placements.push_back({ component.get(), bounds });
        owned.push_back(std::move(component));
    }

    void buildControls()
    {
        const auto& spec = processor.spec;
        const auto poses = knobPoses();

        for (const auto& region : geometry.regions)
        {
            auto art = spriteImage(region.id);
            const auto parameterId = parameterForRegion(spec, region.id);
            const auto* descriptor = parameterId.isNotEmpty() ? findParameter(spec, parameterId) : nullptr;

            if (region.kind == "knob")
            {
                if (descriptor != nullptr && descriptor->kind != ParameterDescriptor::Kind::boolean)
                {
                    const auto pose = poses.count(region.id) > 0 ? poses.at(region.id) : 0.0f;
                    auto knob = std::make_unique<SpriteKnob>(art, pose);
                    knob->setDoubleClickReturnValue(true, descriptor->defaultValue);
                    knob->setTitle(descriptor->name);
                    knob->setDescription(descriptor->rangeText + "; double-click restores "
                                         + descriptor->defaultText);
                    attachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                        processor.state, parameterId, *knob));
                    place(std::move(knob), region.bounds);
                }
                else place(makeStaticSprite(std::move(art)), region.bounds);   // no contract control
                continue;
            }

            if (region.kind == "lamp")
            {
                auto lamp = std::make_unique<SpriteLamp>(art, palette.accentGlow);
                if (descriptor != nullptr)
                {
                    const auto id = parameterId;
                    auto& state = processor.state;
                    lamp->intensity = [&state, id] {
                        const auto* value = state.getRawParameterValue(id);
                        return value != nullptr ? juce::jlimit(0.0f, 1.0f, value->load()) : 0.0f;
                    };
                }
                else lamp->intensity = [this] { return 0.25f + 0.75f * juce::jlimit(0.0f, 1.0f, outputPeak()); };
                place(std::move(lamp), region.bounds);
                continue;
            }

            if (region.kind == "button" || region.kind == "toggle")
            {
                place(makeButton(region, std::move(art)), region.bounds);
                continue;
            }

            // displays, readouts and meters
            if (isVuRegion(region.id))
            {
                const auto isGainReduction = region.id.contains("gr");
                const auto showsInput = region.id.contains("in") && ! isGainReduction;
                auto source = isGainReduction
                    ? std::function<float()>([this] { return gainReduction(); })
                    : std::function<float()>([this, showsInput]
                      {
                          return vuLevel(showsInput
                              ? juce::jmax(processor.getInputPeak(0), processor.getInputPeak(1))
                              : outputPeak());
                      });
                place(std::make_unique<PlateVu>(std::move(source),
                                                isGainReduction ? "GAIN REDUCTION" : "LEVEL",
                                                isGainReduction),
                      region.bounds);
                continue;
            }
            if (isLedRegion(region.id))
            {
                const auto rightChannel = region.id.endsWith("r") || region.id.endsWith("right");
                place(std::make_unique<LedColumn>([this, rightChannel]
                      { return ledLevel(processor.getOutputPeak(rightChannel ? 1 : 0)); }), region.bounds);
                continue;
            }
            if (region.id == "preset" || region.id == "preset_name")
            {
                place(std::make_unique<StaticReadout>("Factory Default", palette.cream), region.bounds);
                continue;
            }
            if (descriptor != nullptr)
            {
                place(std::make_unique<ValueReadout>(processor.state, *descriptor, palette.accentGlow),
                      region.bounds);
                continue;
            }

            place(makeStaticSprite(std::move(art)), region.bounds);
        }
    }

    static bool isVuRegion(const juce::String& id)
    {
        return id.startsWith("vu") || id == "vu_meter" || id.startsWith("vu_");
    }

    static bool isLedRegion(const juce::String& id)
    {
        return id.startsWith("led_") || id.contains("_led_") || id.endsWith("_led");
    }

    std::unique_ptr<juce::Component> makeStaticSprite(juce::Image art)
    {
        auto lamp = std::make_unique<SpriteLamp>(std::move(art), palette.accentGlow);
        lamp->intensity = [] { return 0.0f; };
        return lamp;
    }

    std::unique_ptr<juce::Component> makeButton(const Region& region, juce::Image art)
    {
        const auto& spec = processor.spec;
        auto button = std::make_unique<SpriteButton>(std::move(art), palette.accentGlow);

        juce::String choiceParameter;
        int choiceIndex = -1;
        if (choiceForRegion(spec, region.id, choiceParameter, choiceIndex))
        {
            auto& state = processor.state;
            const auto id = choiceParameter;
            const auto index = choiceIndex;
            button->onClick = [&state, id, index]
            {
                if (auto* parameter = state.getParameter(id))
                {
                    parameter->beginChangeGesture();
                    parameter->setValueNotifyingHost(parameter->convertTo0to1(static_cast<float>(index)));
                    parameter->endChangeGesture();
                }
            };
            button->isLit = [&state, id, index]
            {
                const auto* value = state.getRawParameterValue(id);
                return value != nullptr && juce::roundToInt(value->load()) == index;
            };
            return button;
        }

        const auto parameterId = parameterForRegion(spec, region.id);
        if (const auto* descriptor = parameterId.isNotEmpty() ? findParameter(spec, parameterId) : nullptr;
            descriptor != nullptr && descriptor->kind == ParameterDescriptor::Kind::boolean)
        {
            auto& state = processor.state;
            const auto id = parameterId;
            button->onClick = [&state, id]
            {
                if (auto* parameter = state.getParameter(id))
                {
                    const auto next = parameter->getValue() > 0.5f ? 0.0f : 1.0f;
                    parameter->beginChangeGesture();
                    parameter->setValueNotifyingHost(next);
                    parameter->endChangeGesture();
                }
            };
            button->isLit = [&state, id]
            {
                const auto* value = state.getRawParameterValue(id);
                return value != nullptr && value->load() > 0.5f;
            };
            return button;
        }

        // session actions the host already provides
        if (region.id == "undo") { auto& u = processor.undoManager; button->onClick = [&u] { u.undo(); }; }
        else if (region.id == "redo") { auto& u = processor.undoManager; button->onClick = [&u] { u.redo(); }; }

        return button;
    }

    std::map<juce::String, float> knobPoses() const;

    PluginProcessor& processor;
    Palette palette;
    juce::Image faceplate;
    Geometry geometry;
    juce::Point<int> canvas { 1536, 910 };

    std::vector<std::unique_ptr<juce::Component>> owned;
    std::vector<std::pair<juce::Component*, juce::Rectangle<int>>> placements;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> attachments;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Surface)
};

/** Pictured pose of every knob, measured from the approved raster during asset
    extraction and embedded alongside the geometry. */
std::map<juce::String, float> FaceplateEditor::Surface::knobPoses() const
{
    std::map<juce::String, float> poses;
    const auto block = namedResource("poses_json");
    if (block.getSize() == 0) return poses;

    const auto parsed = juce::JSON::parse(block.toString());
    const auto product = parsed.getProperty(juce::Identifier(processor.spec.id), {});
    if (auto* object = product.getDynamicObject())
        for (const auto& entry : object->getProperties())
            poses[entry.name.toString()] = static_cast<float>(static_cast<double>(entry.value));
    return poses;
}

// ----------------------------------------------------------------- editor

bool FaceplateEditor::isAvailableFor(const juce::String& pluginId)
{
    int size = 0;
    const auto hasGeometry = AmanorsacBinaryData::getNamedResource((pluginId + "_json").toRawUTF8(), size) != nullptr;
    return hasGeometry && faceplateImage().isValid();
}

FaceplateEditor::FaceplateEditor(PluginProcessor& owner)
    : AudioProcessorEditor(owner), surface(std::make_unique<Surface>(owner))
{
    addAndMakeVisible(*surface);

    const auto canvas = surface->canvasSize();
    const auto ratio = static_cast<double>(canvas.x) / static_cast<double>(juce::jmax(1, canvas.y));

    setResizable(true, true);
    setResizeLimits(900, juce::roundToInt(900.0 / ratio), 1920, juce::roundToInt(1920.0 / ratio));
    getConstrainer()->setFixedAspectRatio(ratio);
    setSize(1280, juce::roundToInt(1280.0 / ratio));
}

FaceplateEditor::~FaceplateEditor() = default;

void FaceplateEditor::paint(juce::Graphics& g) { g.fillAll(juce::Colour(0xff080807)); }

void FaceplateEditor::resized()
{
    const auto canvas = surface->canvasSize();
    const auto scale = juce::jmin(static_cast<float>(getWidth()) / static_cast<float>(canvas.x),
                                  static_cast<float>(getHeight()) / static_cast<float>(canvas.y));
    surface->setTransform(juce::AffineTransform::scale(scale));
    surface->setBounds(0, 0, canvas.x, canvas.y);
}
}
