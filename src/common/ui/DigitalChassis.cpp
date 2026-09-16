#include "DigitalChassis.h"

#include "DigitalDisplays.h"
#include "common/audio/PluginProcessor.h"
#include "common/presets/PresetManager.h"

#include <cmath>
#include <map>

namespace amanorsac
{
namespace
{
constexpr float canvasWidth = 1536.0f, canvasHeight = 1024.0f;
// The live display fills the top; the panels sit in a strip underneath.
constexpr float displayTop = 116.0f, displayHeight = 380.0f;
constexpr float contentTop = 510.0f, contentHeight = 392.0f, panelHeader = 56.0f;
constexpr float meterTop = 120.0f, meterHeight = 782.0f;

const juce::Colour textMain { 0xffe9edf2 }, textDim { 0xff8e97a3 };
const juce::Colour blue { 0xff2f8bff }, green { 0xff2fd36b }, purple { 0xffa65cf2 }, orange { 0xffff7a1a };

juce::Font uiFont(float size, bool bold = false, float kerning = 0.0f)
{
   #if JUCE_WINDOWS
    const juce::String name = "Segoe UI";
   #elif JUCE_MAC
    const juce::String name = "Helvetica Neue";
   #else
    const auto name = juce::Font::getDefaultSansSerifFontName();
   #endif
    return juce::Font(juce::FontOptions(name, size, bold ? juce::Font::bold : juce::Font::plain)).withExtraKerningFactor(kerning);
}

float textWidth(const juce::Font& font, const juce::String& text)
{
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(font, text, 0.0f, 0.0f);
    return glyphs.getBoundingBox(0, -1, true).getWidth();
}

// ------------------------------------------------------------------ sections
struct SectionPlan
{
    const char* title;
    juce::Colour accent;
    juce::StringArray ids;      // in the order they should be shown
};

/** How each product divides into panels. Only ids that exist in the contract
    are placed; anything the contract has and this map misses is added to a
    final panel, so a control can never go missing from the window. */
std::vector<SectionPlan> planFor(const juce::String& product)
{
    if (product == "D01")
        return { { "BAND", blue, { "band.type", "band.frequency", "band.gain", "band.q", "band.slope", "band.stereo_mode", "band.solo", "band.delta" } },
                 { "DYNAMICS", green, { "band.dynamic_mode", "band.dynamic_range", "band.threshold", "band.attack", "band.release" } },
                 { "COLOUR", orange, { "color_mode", "color_drive", "color_amount" } },
                 { "GLOBAL", purple, { "input_gain", "output_gain", "auto_gain", "processing_quality", "oversampling", "analyzer", "analyzer_speed" } } };
    if (product == "D02")
        return { { "BAND", blue, { "band.frequency", "band.q", "band.stereo_mode", "band.listen" } },
                 { "DYNAMICS", green, { "band.threshold", "band.range", "band.ratio", "band.attack", "band.release", "band.direction" } },
                 { "ADAPTIVE", purple, { "adaptive", "auto_learn", "band_count", "external_sc", "delta" } },
                 { "GLOBAL", orange, { "input_gain", "output_gain" } } };
    if (product == "D03")
        return { { "SPLIT", blue, { "band_count", "xover.frequency", "split_phase", "global_link" } },
                 { "DYNAMICS", green, { "band.threshold", "band.ratio", "band.attack", "band.release", "band.range", "band.makeup", "band.mode" } },
                 { "MONITOR", purple, { "band.solo", "band.bypass", "band.delta" } },
                 { "GLOBAL", orange, { "global_mix" } } };
    if (product == "D04")
        return { { "LIMIT", blue, { "threshold", "ceiling", "target_lufs", "style" } },
                 { "TIME", green, { "lookahead", "release", "release_mode", "transient_preserve" } },
                 { "QUALITY", purple, { "oversampling", "true_peak", "stereo_link", "delta" } } };
    if (product == "D05")
        return { { "DETECT", blue, { "threshold", "sensitivity", "focus", "bandwidth" } },
                 { "ACTION", green, { "range", "attack", "release", "mode" } },
                 { "VOICE", purple, { "voice_profile", "stereo_link", "listen" } } };
    if (product == "D06")
        return { { "CONTROL", blue, { "depth", "selectivity", "sensitivity", "transient_keep" } },
                 { "MOTION", green, { "speed", "smooth", "learn", "freeze" } },
                 { "RANGE", purple, { "low_freq", "high_freq", "stereo_mode", "delta" } } };
    if (product == "D07")
        return { { "ZONE", blue, { "zone.harmonics", "zone.density", "zone.width", "zone.solo" } },
                 { "SPLIT", green, { "xover.frequency", "punch" } },
                 { "BLEND", purple, { "direct", "global_mix", "auto_gain" } } };
    if (product == "D08")
        return { { "DELAY", orange, { "tempo_sync", "time", "feedback", "freeze" } },
                 { "TAPS", blue, { "tap.time", "tap.level", "tap.pan", "tap.filter" } },
                 { "MOTION", green, { "mod_rate", "mod_depth", "duck" } },
                 { "MIX", purple, { "mix" } } };
    if (product == "D09")
        return { { "SPACE", purple, { "pre_delay", "decay", "size", "density" } },
                 { "TONE", blue, { "diffusion", "early_late", "low_damp", "high_damp" } },
                 { "MOTION", green, { "mod_rate", "mod_depth", "freeze" } },
                 { "MIX", orange, { "duck", "width", "mix" } } };
    if (product == "D10")
        return { { "WIDTH", blue, { "global_width", "band.width", "xover.frequency", "focus" } },
                 { "POSITION", green, { "rotation", "ms_balance", "bass_mono" } },
                 { "SAFETY", purple, { "safe_width", "mono_check", "correlation_alarm", "output_gain" } } };
    return {};
}

/** The switch that powers a panel, when the contract has one. */
juce::String powerIdFor(const juce::String& product, const juce::String& title)
{
    if (product == "D01" && title == "BAND") return "band.enabled";
    if (product == "D07" && title == "ZONE") return "zone.enabled";
    if (product == "D08" && title == "TAPS") return "tap.enabled";
    return {};
}

// ------------------------------------------------------------------ look
class Look final : public juce::LookAndFeel_V4
{
public:
    Look()
    {
        setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff161a21));
        setColour(juce::PopupMenu::textColourId, textMain);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xff2a3242));
        setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
        setColour(juce::ComboBox::textColourId, textMain);
        setColour(juce::AlertWindow::backgroundColourId, juce::Colour(0xff161a21));
        setColour(juce::AlertWindow::textColourId, textMain);
        setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff0d1015));
        setColour(juce::TextEditor::textColourId, textMain);
        setColour(juce::TextButton::buttonColourId, juce::Colour(0xff232a35));
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float position,
                          float startAngle, float endAngle, juce::Slider& slider) override
    {
        const auto accent = juce::Colour(static_cast<juce::uint32>(static_cast<juce::int64>(
            slider.getProperties().getWithDefault("accent", static_cast<juce::int64>(0xffffffff)))));
        const auto bipolar = static_cast<bool>(slider.getProperties().getWithDefault("bipolar", false));
        const auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                                   static_cast<float>(width), static_cast<float>(height));
        const auto centre = bounds.getCentre();
        const auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f - 2.0f;
        const auto thickness = radius * 0.1f;
        const auto arcRadius = radius - thickness * 0.5f;
        const auto angle = startAngle + position * (endAngle - startAngle);
        const auto origin = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
        const juce::PathStrokeType stroke(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

        juce::Path track;
        track.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
        g.setColour(juce::Colour(0xff1c2029));
        g.strokePath(track, stroke);

        if (std::abs(angle - origin) > 0.01f)
        {
            juce::Path value;
            value.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                                juce::jmin(origin, angle), juce::jmax(origin, angle), true);
            g.setColour(accent);
            g.strokePath(value, stroke);
        }

        const auto knob = radius * 0.8f;
        const auto face = juce::Rectangle<float>(knob * 2.0f, knob * 2.0f).withCentre(centre);
        g.setColour(juce::Colours::black.withAlpha(0.6f));
        g.fillEllipse(face.translated(0.0f, knob * 0.08f).expanded(knob * 0.05f));
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff363a42), centre.x, face.getY(),
                                               juce::Colour(0xff0e1014), centre.x, face.getBottom(), false));
        g.fillEllipse(face);
        g.setColour(juce::Colours::white.withAlpha(0.07f));
        g.drawEllipse(face.reduced(0.8f), 1.2f);

        const juce::Point<float> direction(std::sin(angle), -std::cos(angle));
        juce::Path pointer;
        pointer.startNewSubPath(centre + direction * (knob * 0.36f));
        pointer.lineTo(centre + direction * (knob * 0.78f));
        g.setColour(juce::Colours::white.withAlpha(0.96f));
        g.strokePath(pointer, juce::PathStrokeType(juce::jmax(2.2f, knob * 0.075f), juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }

    void drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox&) override
    {
        const auto r = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)).reduced(0.6f);
        g.setColour(juce::Colour(0xff171b22));
        g.fillRoundedRectangle(r, 7.0f);
        g.setColour(juce::Colour(0xff2b313b));
        g.drawRoundedRectangle(r, 7.0f, 1.2f);

        const auto cx = static_cast<float>(width) - 24.0f, cy = static_cast<float>(height) * 0.5f;
        juce::Path chevron;
        chevron.startNewSubPath(cx - 6.0f, cy - 3.0f);
        chevron.lineTo(cx, cy + 3.0f);
        chevron.lineTo(cx + 6.0f, cy - 3.0f);
        g.setColour(textMain);
        g.strokePath(chevron, juce::PathStrokeType(2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    juce::Font getComboBoxFont(juce::ComboBox&) override { return uiFont(18.0f); }
    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds(10, 0, box.getWidth() - 38, box.getHeight());
        label.setFont(getComboBoxFont(box));
    }
    juce::Font getPopupMenuFont() override { return uiFont(17.0f); }
};

// ------------------------------------------------------------------ buttons
class PowerButton final : public juce::ToggleButton
{
public:
    explicit PowerButton(juce::Colour colour) : accent(colour) {}

    void paintButton(juce::Graphics& g, bool over, bool) override
    {
        const auto r = getLocalBounds().toFloat().reduced(2.0f);
        const auto on = getToggleState();
        const auto base = on ? accent : juce::Colour(0xff3a404a);

        g.setColour(base.withAlpha(on ? 0.3f : 0.15f));
        g.fillEllipse(r);
        g.setGradientFill(juce::ColourGradient(base.brighter(0.3f), r.getCentreX(), r.getY(),
                                               base.darker(0.3f), r.getCentreX(), r.getBottom(), false));
        g.fillEllipse(r.reduced(3.0f));
        if (over)
        {
            g.setColour(juce::Colours::white.withAlpha(0.08f));
            g.fillEllipse(r.reduced(3.0f));
        }

        const auto c = r.getCentre();
        const auto glyph = r.getWidth() * 0.22f;
        juce::Path power;
        power.addCentredArc(c.x, c.y, glyph, glyph, 0.0f, juce::MathConstants<float>::pi * 0.22f,
                            juce::MathConstants<float>::pi * 1.78f, true);
        power.startNewSubPath(c.x, c.y - glyph * 1.25f);
        power.lineTo(c.x, c.y - glyph * 0.15f);
        g.setColour(on ? juce::Colours::white : textDim);
        g.strokePath(power, juce::PathStrokeType(r.getWidth() * 0.07f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
    }

private:
    juce::Colour accent;
};

class PillToggle final : public juce::ToggleButton
{
public:
    explicit PillToggle(juce::Colour colour) : accent(colour) {}

    void paintButton(juce::Graphics& g, bool, bool) override
    {
        const auto r = getLocalBounds().toFloat().reduced(1.0f);
        const auto on = getToggleState();
        const auto radius = r.getHeight() * 0.5f;
        g.setColour(on ? accent : juce::Colour(0xff2b313b));
        g.fillRoundedRectangle(r, radius);
        const auto dot = juce::Rectangle<float>(r.getHeight() - 9.0f, r.getHeight() - 9.0f)
                             .withCentre({ on ? r.getRight() - radius : r.getX() + radius, r.getCentreY() });
        g.setColour(on ? juce::Colours::white : textDim);
        g.fillEllipse(dot);
    }

private:
    juce::Colour accent;
};

class ChevronButton final : public juce::Button
{
public:
    explicit ChevronButton(bool pointsRight) : juce::Button({}), right(pointsRight) {}

    void paintButton(juce::Graphics& g, bool over, bool) override
    {
        const auto c = getLocalBounds().toFloat().getCentre();
        juce::Path chevron;
        const auto d = right ? 1.0f : -1.0f;
        chevron.startNewSubPath(c.x - 4.0f * d, c.y - 9.0f);
        chevron.lineTo(c.x + 4.0f * d, c.y);
        chevron.lineTo(c.x - 4.0f * d, c.y + 9.0f);
        g.setColour(over ? juce::Colours::white : textMain);
        g.strokePath(chevron, juce::PathStrokeType(2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

private:
    bool right;
};

class DotsButton final : public juce::Button
{
public:
    DotsButton() : juce::Button({}) {}
    void paintButton(juce::Graphics& g, bool over, bool) override
    {
        const auto c = getLocalBounds().toFloat().getCentre();
        g.setColour(over ? juce::Colours::white : textMain);
        for (int i = -1; i <= 1; ++i)
            g.fillEllipse(juce::Rectangle<float>(6.0f, 6.0f).withCentre({ c.x, c.y + static_cast<float>(i) * 12.0f }));
    }
};

struct ClickArea final : public juce::Component
{
    std::function<void()> onClick;
    void mouseUp(const juce::MouseEvent& e) override { if (e.mouseWasClicked() && onClick) onClick(); }
};

float meterY(float db)
{
    static constexpr float dbs[] { 0.0f, -6.0f, -12.0f, -24.0f, -36.0f, -60.0f };
    static constexpr float ys[] { 235.0f, 295.0f, 357.0f, 428.0f, 508.0f, 583.0f };
    if (db >= 0.0f) return 235.0f - db * 10.0f;
    for (int i = 1; i < 6; ++i)
        if (db >= dbs[i]) return juce::jmap(db, dbs[i - 1], dbs[i], ys[i - 1], ys[i]);
    return 583.0f + (-60.0f - db) * 0.5f;
}

float meterDb(float y)
{
    static constexpr float dbs[] { 0.0f, -6.0f, -12.0f, -24.0f, -36.0f, -60.0f };
    static constexpr float ys[] { 235.0f, 295.0f, 357.0f, 428.0f, 508.0f, 583.0f };
    if (y <= 235.0f) return (235.0f - y) / 10.0f;
    for (int i = 1; i < 6; ++i)
        if (y <= ys[i]) return juce::jmap(y, ys[i - 1], ys[i], dbs[i - 1], dbs[i]);
    return -60.0f - (y - 583.0f) * 2.0f;
}
}

// ------------------------------------------------------------------ surface
class DigitalChassis::Surface final : public juce::Component
{
public:
    explicit Surface(PluginProcessor& owner) : processor(owner), prev(false), next(true)
    {
        setLookAndFeel(&look);
        setSize(static_cast<int>(canvasWidth), static_cast<int>(canvasHeight));

        display = DigitalDisplay::create(processor);
        display->setBounds(juce::Rectangle<float>(24.0f, displayTop, 1359.0f, displayHeight).toNearestInt());
        addAndMakeVisible(*display);
        display->onSelectSlot = [this](const juce::String& family, int slot)
        {
            if (slotFor.count(family) != 0 && slotFor[family] == slot) return;
            slotFor[family] = slot;
            rebuild();
        };
        processor.analysisWanted.store(true);

        buildSections();
        layOutSections();

        addAndMakeVisible(prev);
        addAndMakeVisible(next);
        addAndMakeVisible(presetArea);
        addAndMakeVisible(dots);
        prev.setBounds(1090, 26, 50, 54);
        next.setBounds(1400, 26, 50, 54);
        presetArea.setBounds(1140, 26, 260, 54);
        dots.setBounds(1470, 28, 36, 50);
        prev.onClick = [this] { if (auto* m = manager()) { m->loadPrevious(); refresh(); } };
        next.onClick = [this] { if (auto* m = manager()) { m->loadNext(); refresh(); } };
        presetArea.onClick = [this] { showPresetMenu(); };
        dots.onClick = [this] { showOptionsMenu(); };

        refresh();
    }

    ~Surface() override
    {
        processor.analysisWanted.store(false);
        setLookAndFeel(nullptr);
    }

    void tick()
    {
        display->tick();
        const auto peak = juce::jmax(processor.getOutputPeak(0), processor.getOutputPeak(1));
        const auto peakDb = juce::Decibels::gainToDecibels(peak, -90.0f);
        meterLevel = juce::jmax(peakDb, meterLevel - 1.2f);
        const auto reduction = processor.getGainReductionDb();
        if (std::abs(reduction - shownReduction) > 0.05f) shownReduction = reduction;
        repaint(1426, 198, 38, 418);

        if (auto* m = manager())
            if (m->currentName() != shownPreset) refresh();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff0a0c10));
        drawHeader(g);
        for (const auto& section : sections) drawSection(g, section);
        drawOutput(g);
        drawFooter(g);
    }

private:
    struct Control
    {
        juce::String id, label;
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::ComboBox> combo;
        std::unique_ptr<PillToggle> toggle;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachment;
        juce::Point<float> centre;
        float size = 108.0f;
    };

    struct Section
    {
        juce::String title, family;
        juce::Colour accent;
        juce::Rectangle<float> bounds;
        std::unique_ptr<ChevronButton> slotDown, slotUp;
        std::vector<Control> controls;
        juce::String powerId;
        std::unique_ptr<PowerButton> power;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAttachment;
        int columns = 2;
    };

    presets::PresetManager* manager() const { return processor.presetManager.get(); }

    const ParameterDescriptor* describe(const juce::String& id) const
    {
        for (const auto& p : processor.spec.parameters)
            if (p.id == id) return &p;
        return nullptr;
    }

    /** "band." style controls exist once per slot. The window shows one slot at
        a time with a stepper, the way any real equaliser does. */
    static juce::String familyOf(const juce::String& id)
    {
        const auto dot = id.indexOfChar('.');
        if (dot < 0) return {};
        const auto family = id.substring(0, dot);
        return family == "band" || family == "zone" || family == "tap" || family == "xover" ? family : juce::String();
    }

    int slotCount(const juce::String& family) const
    {
        int count = 0;
        for (const auto& p : processor.spec.parameters)
            if (p.id.startsWith(family + "."))
                count = juce::jmax(count, p.id.substring(family.length() + 1).upToFirstOccurrenceOf(".", false, false).getIntValue());
        return count;
    }

    /** Turns "band.gain" into the id of the slot on show, "band.03.gain". */
    juce::String resolve(const juce::String& id) const
    {
        const auto family = familyOf(id);
        if (family.isEmpty() || slotCount(family) == 0) return id;
        const auto slot = slotFor.count(family) != 0 ? slotFor.at(family) : 1;
        return family + "." + juce::String(slot).paddedLeft('0', 2) + "." + id.fromFirstOccurrenceOf(".", false, false);
    }

    /** Drops the "Band 03 " the contract prepends, since the panel says it. */
    static juce::String shortLabel(juce::String name)
    {
        // Only strip a real slot prefix: "Band 03 Gain" loses it, "Band Count"
        // keeps every word it has.
        for (const auto* prefix : { "Band ", "Zone ", "Tap ", "Crossover " })
        {
            const juce::String head(prefix);
            if (! name.startsWith(head)) continue;
            const auto rest = name.substring(head.length());
            if (rest.length() > 3 && juce::CharacterFunctions::isDigit(rest[0])
                && juce::CharacterFunctions::isDigit(rest[1]) && rest[2] == ' ')
                return rest.substring(3).trim();
        }
        return name;
    }

    void buildSections()
    {
        auto plan = planFor(processor.spec.id);
        juce::StringArray placed;
        for (const auto& entry : plan)
        {
            Section section;
            section.title = entry.title;
            section.accent = entry.accent;
            for (const auto& id : entry.ids)
            {
                const auto actual = resolve(id);
                if (describe(actual) == nullptr) continue;
                section.controls.emplace_back().id = actual;
                placed.add(id);
                if (section.family.isEmpty()) section.family = familyOf(id);
            }

            const auto powerId = powerIdFor(processor.spec.id, section.title);
            if (powerId.isNotEmpty() && describe(resolve(powerId)) != nullptr)
            {
                section.powerId = resolve(powerId);
                placed.add(powerId);
                if (section.family.isEmpty()) section.family = familyOf(powerId);
            }
            if (! section.controls.empty()) sections.push_back(std::move(section));
        }

        // Anything the contract declares but the plan missed still gets a home.
        Section extra;
        extra.title = "MORE";
        extra.accent = juce::Colour(0xff2bb6c4);
        for (const auto& p : processor.spec.parameters)
        {
            // Indexed controls are reached through their panel's stepper, so a
            // slot the plan already covers is not loose.
            const auto family = familyOf(p.id);
            const auto base = family.isEmpty() ? p.id : family + "." + p.id.fromFirstOccurrenceOf(".", false, false)
                                                                            .fromFirstOccurrenceOf(".", false, false);
            if (! placed.contains(base)) extra.controls.emplace_back().id = p.id;
        }
        if (! extra.controls.empty()) sections.push_back(std::move(extra));
    }

    void layOutSections()
    {
        if (sections.empty()) return;

        // Panels are as wide as their contents need, sharing the row.
        // Two rows at most under the display, so wide panels take more columns.
        float demand = 0.0f;
        for (auto& section : sections)
        {
            const auto count = static_cast<int>(section.controls.size());
            section.columns = juce::jlimit(1, 4, (count + 1) / 2);
            demand += columnPitch(section.columns) * section.columns + 30.0f;
        }
        // Panels always fill the row, so the window never looks half empty.
        const auto available = canvasWidth - 48.0f - 123.0f - 14.0f - 13.0f * static_cast<float>(sections.size() - 1);
        const auto scale = available / juce::jmax(1.0f, demand);

        auto x = 24.0f;
        for (auto& section : sections)
        {
            const auto width = (columnPitch(section.columns) * section.columns + 30.0f) * scale;
            section.bounds = { x, contentTop, width, contentHeight };
            x += width + 13.0f;
            placeControls(section);
        }
    }

    static float columnPitch(int) { return 104.0f; }

    void placeControls(Section& section)
    {
        const auto slots = section.family.isEmpty() ? 0 : slotCount(section.family);
        // One stepper per family: several panels can show the same slot.
        if (slots > 1 && ! steppedFamilies.contains(section.family))
        {
            steppedFamilies.add(section.family);
            const auto family = section.family;
            section.slotDown = std::make_unique<ChevronButton>(false);
            section.slotUp = std::make_unique<ChevronButton>(true);
            const auto row = juce::roundToInt(contentTop + panelHeader * 0.5f);
            section.slotDown->setBounds(juce::Rectangle<int>(26, 30).withCentre(
                { juce::roundToInt(section.bounds.getRight()) - 104, row }));
            section.slotUp->setBounds(juce::Rectangle<int>(26, 30).withCentre(
                { juce::roundToInt(section.bounds.getRight()) - 20, row }));
            addAndMakeVisible(*section.slotDown);
            addAndMakeVisible(*section.slotUp);
            section.slotDown->onClick = [this, family, slots] { stepSlot(family, -1, slots); };
            section.slotUp->onClick = [this, family, slots] { stepSlot(family, 1, slots); };
        }

        if (section.powerId.isNotEmpty())
        {
            auto* descriptor = describe(section.powerId);
            juce::ignoreUnused(descriptor);
            section.power = std::make_unique<PowerButton>(section.accent);
            section.power->setBounds(juce::Rectangle<int>(40, 40).withCentre(
                { juce::roundToInt(section.bounds.getX()) + 30, juce::roundToInt(contentTop + panelHeader * 0.5f) }));
            addAndMakeVisible(*section.power);
            section.powerAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
                processor.state, section.powerId, *section.power);
            section.power->onStateChange = [this] { refreshEnablement(); repaint(); };
        }

        const auto count = static_cast<int>(section.controls.size());
        const auto columns = section.columns;
        const auto rows = (count + columns - 1) / columns;
        const auto pitchX = section.bounds.getWidth() / static_cast<float>(columns);
        const auto usable = contentHeight - panelHeader - 14.0f;
        const auto pitchY = usable / static_cast<float>(juce::jmax(1, rows));
        const auto knob = juce::jlimit(56.0f, 92.0f, juce::jmin(pitchY * 0.5f, pitchX * 0.7f));
        const auto top = contentTop + panelHeader + 4.0f;

        for (int i = 0; i < count; ++i)
        {
            auto& control = section.controls[static_cast<size_t>(i)];
            const auto* descriptor = describe(control.id);
            if (descriptor == nullptr) continue;
            control.label = shortLabel(descriptor->name).toUpperCase();
            control.size = knob;
            control.centre = { section.bounds.getX() + pitchX * (static_cast<float>(i % columns) + 0.5f),
                               top + pitchY * (static_cast<float>(i / columns) + 0.5f) };

            if (descriptor->kind == ParameterDescriptor::Kind::boolean)
            {
                control.toggle = std::make_unique<PillToggle>(section.accent);
                control.toggle->setBounds(juce::Rectangle<int>(64, 30).withCentre(control.centre.toInt()));
                addAndMakeVisible(*control.toggle);
                control.buttonAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
                    processor.state, control.id, *control.toggle);
                control.toggle->onStateChange = [this] { repaint(); };
            }
            else if (descriptor->kind == ParameterDescriptor::Kind::choice)
            {
                control.combo = std::make_unique<juce::ComboBox>();
                juce::StringArray pretty;
                for (const auto& choice : descriptor->choices) pretty.add(prettyChoice(choice));
                control.combo->addItemList(pretty, 1);
                control.combo->setBounds(juce::Rectangle<int>(juce::roundToInt(pitchX - 12.0f), 34)
                                             .withCentre(control.centre.toInt()));
                addAndMakeVisible(*control.combo);
                control.comboAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
                    processor.state, control.id, *control.combo);
                control.combo->onChange = [this] { repaint(); };
            }
            else
            {
                control.slider = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag,
                                                                juce::Slider::NoTextBox);
                control.slider->getProperties().set("accent", static_cast<juce::int64>(section.accent.getARGB()));
                control.slider->getProperties().set("bipolar", descriptor->minimum < -0.001f && descriptor->maximum > 0.001f);
                control.slider->setRotaryParameters(juce::MathConstants<float>::pi * 1.22f,
                                                    juce::MathConstants<float>::pi * 2.78f, true);
                control.slider->setMouseDragSensitivity(260);
                control.slider->setBounds(juce::Rectangle<float>(knob, knob).withCentre(control.centre).toNearestInt());
                addAndMakeVisible(*control.slider);
                control.sliderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                    processor.state, control.id, *control.slider);
                control.slider->setDoubleClickReturnValue(true, descriptor->defaultValue);
                control.slider->onValueChange = [this] { repaint(); };
            }
        }
    }

    void refreshEnablement()
    {
        for (auto& section : sections)
        {
            const auto on = section.power == nullptr || section.power->getToggleState();
            for (auto& control : section.controls)
            {
                if (control.slider != nullptr) control.slider->setAlpha(on ? 1.0f : 0.4f);
                if (control.combo != nullptr) control.combo->setAlpha(on ? 1.0f : 0.4f);
                if (control.toggle != nullptr) control.toggle->setAlpha(on ? 1.0f : 0.4f);
            }
        }
    }

    void stepSlot(const juce::String& family, int delta, int slots)
    {
        const auto current = slotFor.count(family) != 0 ? slotFor[family] : 1;
        slotFor[family] = ((current - 1 + delta) % slots + slots) % slots + 1;
        rebuild();
    }

    /** Rebuilds the panels against the slot now on show. Controls own their
        attachments, so dropping them detaches cleanly. */
    void rebuild()
    {
        sections.clear();
        steppedFamilies.clear();
        buildSections();
        layOutSections();
        for (const auto& [family, slot] : slotFor) display->setSelectedSlot(family, slot);
        refresh();
    }

    void refresh()
    {
        if (auto* m = manager()) shownPreset = m->currentName();
        refreshEnablement();
        repaint();
    }

    // ------------------------------------------------------------------ menus
    void showPresetMenu()
    {
        auto* m = manager();
        if (m == nullptr) return;

        juce::PopupMenu menu;
        const auto& items = m->presets();
        for (const auto& category : m->categories())
        {
            juce::PopupMenu sub;
            for (int i = 0; i < static_cast<int>(items.size()); ++i)
                if (items[static_cast<size_t>(i)].category == category)
                    sub.addItem(i + 1, items[static_cast<size_t>(i)].name, true, i == m->currentIndex());
            menu.addSubMenu(category, sub);
        }

        juce::Component::SafePointer<Surface> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&presetArea), [safe](int result)
        {
            if (safe == nullptr || result <= 0) return;
            if (auto* mm = safe->manager()) mm->load(result - 1);
            safe->refresh();
        });
    }

    void showOptionsMenu()
    {
        juce::PopupMenu menu;
        menu.addItem(1, "Save preset...");
        menu.addItem(2, "Open presets folder");
        menu.addSeparator();
        menu.addItem(3, "Copy settings");
        menu.addItem(4, "Paste settings");
        juce::Component::SafePointer<Surface> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&dots), [safe](int result)
        {
            if (safe == nullptr) return;
            auto* m = safe->manager();
            if (m == nullptr) return;
            if (result == 1) safe->askForPresetName();
            if (result == 2) { auto folder = m->userDirectory(); folder.createDirectory(); folder.startAsProcess(); }
            if (result == 3) m->copyToClipboard();
            if (result == 4) { m->pasteFromClipboard(); safe->refresh(); }
        });
    }

    void askForPresetName()
    {
        auto* window = new juce::AlertWindow("Save preset", "Name this preset.", juce::MessageBoxIconType::NoIcon, this);
        window->addTextEditor("name", shownPreset);
        window->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
        window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        juce::Component::SafePointer<Surface> safe(this);
        window->enterModalState(true, juce::ModalCallbackFunction::create([safe, window](int result)
        {
            if (safe == nullptr || result != 1) return;
            if (auto* m = safe->manager()) m->save(window->getTextEditorContents("name"), "Amanorsac Studio", {});
            safe->refresh();
        }), true);
    }

    // ------------------------------------------------------------------ drawing
    void drawHeader(juce::Graphics& g)
    {
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff151a22), 0.0f, 0.0f, juce::Colour(0xff0c0f14), 0.0f, 100.0f, false));
        g.fillRect(0.0f, 0.0f, canvasWidth, 100.0f);
        g.setColour(juce::Colour(0xff1c2129));
        g.drawHorizontalLine(100, 0.0f, canvasWidth);

        const auto name = processor.spec.displayName.toUpperCase();
        const auto space = name.lastIndexOfChar(' ');
        const auto head = space > 0 ? name.substring(0, space) : name;
        const auto tail = space > 0 ? name.substring(space + 1) : juce::String();

        const auto titleFont = uiFont(38.0f, true, 0.06f);
        g.setFont(titleFont);
        g.setColour(juce::Colours::white);
        g.drawText(head, juce::Rectangle<float>(50.0f, 20.0f, 420.0f, 46.0f), juce::Justification::centredLeft, false);
        if (tail.isNotEmpty())
        {
            g.setColour(blue);
            g.drawText(tail, juce::Rectangle<float>(50.0f + textWidth(titleFont, head + " "), 20.0f, 300.0f, 46.0f),
                       juce::Justification::centredLeft, false);
        }

        g.setColour(textDim);
        g.setFont(uiFont(14.0f, false, 0.42f));
        g.drawText("AMANORSAC.STUDIO", juce::Rectangle<float>(52.0f, 62.0f, 300.0f, 20.0f), juce::Justification::centredLeft, false);

        g.setColour(juce::Colour(0xffa8b0bc));
        g.setFont(uiFont(22.0f, false, 0.5f));
        g.drawText(processor.spec.primaryRole.toUpperCase(), juce::Rectangle<float>(500.0f, 30.0f, 560.0f, 46.0f),
                   juce::Justification::centred, false);

        const auto bar = juce::Rectangle<float>(1090.0f, 26.0f, 360.0f, 54.0f);
        g.setColour(juce::Colour(0xff151a21));
        g.fillRoundedRectangle(bar, 9.0f);
        g.setColour(juce::Colour(0xff2b313b));
        g.drawRoundedRectangle(bar.reduced(0.6f), 9.0f, 1.2f);
        g.drawVerticalLine(1140, 27.0f, 79.0f);
        g.drawVerticalLine(1400, 27.0f, 79.0f);
        g.setColour(textMain);
        g.setFont(uiFont(22.0f));
        g.drawFittedText(shownPreset, juce::Rectangle<int>(1146, 26, 248, 54), juce::Justification::centred, 1, 0.8f);
    }

    void drawSection(juce::Graphics& g, const Section& section)
    {
        const auto r = section.bounds;
        const auto on = section.power == nullptr || section.power->getToggleState();
        const auto accent = section.accent;

        g.setColour(juce::Colour(0xff10131a));
        g.fillRoundedRectangle(r, 10.0f);
        g.setGradientFill(juce::ColourGradient(accent.withAlpha(on ? 0.30f : 0.08f), r.getX(), r.getY(),
                                               accent.withAlpha(0.0f), r.getX(), r.getY() + 230.0f, false));
        g.fillRoundedRectangle(r, 10.0f);

        const auto body = r.withTrimmedTop(panelHeader).reduced(6.0f, 0.0f).withTrimmedBottom(6.0f);
        g.setColour(juce::Colour(0xff0c0f14).withAlpha(0.8f));
        g.fillRoundedRectangle(body, 8.0f);
        g.setColour(accent.withAlpha(on ? 0.5f : 0.18f));
        g.drawRoundedRectangle(r.reduced(0.6f), 10.0f, 1.4f);

        const auto header = r.withHeight(panelHeader);
        const auto stepped = section.slotUp != nullptr;
        g.setColour(on ? juce::Colours::white : textDim);
        g.setFont(uiFont(20.0f, true));
        g.drawText(section.title, stepped ? header.withTrimmedRight(118.0f).withTrimmedLeft(section.power != nullptr ? 50.0f : 14.0f)
                                          : header,
                   stepped ? juce::Justification::centredLeft : juce::Justification::centred, false);

        if (section.power != nullptr && ! stepped)
        {
            g.setColour(on ? accent : textDim);
            g.setFont(uiFont(15.0f, true));
            g.drawText(on ? "ON" : "OFF", header.withTrimmedRight(16.0f), juce::Justification::centredRight, false);
        }

        if (stepped)
        {
            const auto slots = slotCount(section.family);
            const auto slot = slotFor.count(section.family) != 0 ? slotFor.at(section.family) : 1;
            g.setColour(accent.withAlpha(0.18f));
            g.fillRoundedRectangle(juce::Rectangle<float>(r.getRight() - 118.0f, header.getCentreY() - 15.0f, 112.0f, 30.0f), 6.0f);
            g.setColour(textMain);
            g.setFont(uiFont(15.0f, true));
            g.drawText(juce::String(slot) + " / " + juce::String(slots),
                       juce::Rectangle<float>(r.getRight() - 92.0f, header.getCentreY() - 12.0f, 60.0f, 24.0f),
                       juce::Justification::centred, false);
        }

        for (const auto& control : section.controls)
        {
            const auto alpha = on ? 1.0f : 0.4f;
            const auto half = control.slider != nullptr ? control.size * 0.5f : 22.0f;
            const auto cell = section.bounds.getWidth() / static_cast<float>(section.columns) - 6.0f;
            g.setColour(textMain.withAlpha(alpha));
            g.setFont(uiFont(12.5f, false, 0.03f));
            g.drawFittedText(control.label, juce::Rectangle<float>(control.centre.x - cell * 0.5f, control.centre.y - half - 22.0f,
                                                                  cell, 18.0f).toNearestInt(),
                             juce::Justification::centred, 1, 0.75f);
            if (control.slider != nullptr)
            {
                g.setFont(uiFont(15.5f));
                g.drawFittedText(valueText(control.id),
                                 juce::Rectangle<float>(control.centre.x - cell * 0.5f, control.centre.y + half + 1.0f, cell, 22.0f).toNearestInt(),
                                 juce::Justification::centred, 1, 0.8f);
            }
        }
    }

    /** Readable rather than exact: a de-esser says -24 dB, not -24.00 dB. */
    juce::String valueText(const juce::String& id) const
    {
        const auto* descriptor = describe(id);
        const auto* raw = processor.state.getRawParameterValue(id);
        if (descriptor == nullptr || raw == nullptr) return {};

        auto value = raw->load();
        auto unit = descriptor->unit.trim();
        if (unit.equalsIgnoreCase("Hz") && std::abs(value) >= 1000.0f) { value /= 1000.0f; unit = "kHz"; }

        const auto magnitude = std::abs(value);
        const auto decimals = magnitude >= 100.0f ? 0 : magnitude >= 10.0f ? 1 : 2;
        auto text = juce::String(value, decimals);
        if (text.containsChar('.'))
            text = text.trimCharactersAtEnd("0").trimCharactersAtEnd(".");
        if (text == "-0") text = "0";
        return unit.isEmpty() ? text : text + " " + unit;
    }

    /** Contracts spell choices in code, so "low_shelf" reads "Low Shelf". */
    static juce::String prettyChoice(juce::String choice)
    {
        choice = choice.replaceCharacter('_', ' ').replaceCharacter('-', ' ').trim();
        juce::StringArray words;
        words.addTokens(choice, " ", {});
        for (auto& word : words)
            if (word.isNotEmpty())
                word = word.length() <= 2 ? word.toUpperCase()
                                          : word.substring(0, 1).toUpperCase() + word.substring(1).toLowerCase();
        return words.joinIntoString(" ");
    }

    void drawOutput(juce::Graphics& g)
    {
        const auto r = juce::Rectangle<float>(1396.0f, meterTop, 123.0f, meterHeight);
        g.setColour(juce::Colour(0xff10131a));
        g.fillRoundedRectangle(r, 10.0f);
        g.setColour(juce::Colour(0xff1f242d));
        g.drawRoundedRectangle(r.reduced(0.6f), 10.0f, 1.2f);

        g.setColour(textMain);
        g.setFont(uiFont(22.0f, false, 0.05f));
        g.drawText("OUTPUT", juce::Rectangle<float>(1396.0f, 150.0f, 123.0f, 32.0f), juce::Justification::centred, false);

        g.setColour(juce::Colour(0xff0a0c10));
        g.fillRoundedRectangle(juce::Rectangle<float>(1426.0f, 198.0f, 36.0f, 418.0f), 6.0f);
        for (float y = 598.0f; y > 208.0f; y -= 14.0f)
        {
            const auto segmentDb = meterDb(y - 5.0f);
            const auto lit = meterLevel >= segmentDb;
            const auto colour = segmentDb >= -3.0f ? juce::Colour(0xffef4444)
                              : segmentDb >= -9.0f ? juce::Colour(0xfff5c518) : juce::Colour(0xff27d05a);
            g.setColour(lit ? colour : juce::Colour(0xff262b33));
            g.fillRect(juce::Rectangle<float>(1433.0f, y - 11.0f, 22.0f, 11.0f));
        }

        g.setColour(textDim);
        g.setFont(uiFont(15.0f));
        for (const auto db : { 0, -6, -12, -24, -36, -60 })
            g.drawText(juce::String(db), juce::Rectangle<float>(1472.0f, meterY(static_cast<float>(db)) - 10.0f, 40.0f, 20.0f),
                       juce::Justification::centredLeft, false);

        // Gain reduction, for the products that report it.
        g.setColour(textDim);
        g.setFont(uiFont(13.0f, false, 0.2f));
        g.drawText("GR", juce::Rectangle<float>(1396.0f, 636.0f, 123.0f, 18.0f), juce::Justification::centred, false);
        const auto bar = juce::Rectangle<float>(1426.0f, 658.0f, 36.0f, 180.0f);
        g.setColour(juce::Colour(0xff0a0c10));
        g.fillRoundedRectangle(bar, 5.0f);
        const auto amount = juce::jlimit(0.0f, 1.0f, shownReduction / 20.0f);
        g.setColour(green);
        g.fillRoundedRectangle(bar.withHeight(bar.getHeight() * amount).reduced(4.0f, 4.0f), 3.0f);
        g.setColour(textMain);
        g.setFont(uiFont(16.0f));
        g.drawText(juce::String(shownReduction, 1) + " dB", juce::Rectangle<float>(1396.0f, 848.0f, 123.0f, 22.0f),
                   juce::Justification::centred, false);
    }

    void drawFooter(juce::Graphics& g)
    {
        g.setColour(juce::Colour(0xff0c0f14));
        g.fillRect(0.0f, 928.0f, canvasWidth, 96.0f);
        g.setColour(juce::Colour(0xff1c2129));
        g.drawHorizontalLine(928, 0.0f, canvasWidth);

        g.setColour(textDim);
        g.setFont(uiFont(15.0f, false, 0.42f));
        g.drawText("AMANORSAC.STUDIO", juce::Rectangle<float>(56.0f, 960.0f, 400.0f, 24.0f), juce::Justification::centredLeft, false);
        g.setFont(uiFont(15.0f, false, 0.5f));
        g.drawText(juce::String(juce::CharPointer_UTF8("CREATE  \xe2\x80\xa2  PLAY  \xe2\x80\xa2  PERFORM")),
                   juce::Rectangle<float>(940.0f, 960.0f, 544.0f, 24.0f), juce::Justification::centredRight, false);

        g.setColour(juce::Colour(0xffb6bec9));
        g.setFont(uiFont(17.0f, false, 0.55f));
        g.drawText(processor.spec.displayName.toUpperCase(), juce::Rectangle<float>(518.0f, 944.0f, 500.0f, 24.0f),
                   juce::Justification::centred, false);
        g.setColour(textDim.withAlpha(0.7f));
        g.setFont(uiFont(13.0f, false, 0.7f));
        g.drawText("MUSIC LIVES HERE", juce::Rectangle<float>(518.0f, 970.0f, 500.0f, 22.0f), juce::Justification::centred, false);
    }

    PluginProcessor& processor;
    Look look;
    std::unique_ptr<DigitalDisplay> display;
    std::vector<Section> sections;
    ChevronButton prev, next;
    ClickArea presetArea;
    DotsButton dots;
    std::map<juce::String, int> slotFor;
    juce::StringArray steppedFamilies;
    juce::String shownPreset;
    float shownReduction = 0.0f, meterLevel = -90.0f;
};

// ------------------------------------------------------------------ editor
DigitalChassis::DigitalChassis(PluginProcessor& owner)
    : AudioProcessorEditor(owner), surface(std::make_unique<Surface>(owner))
{
    addAndMakeVisible(*surface);
    setResizable(true, true);
    setResizeLimits(768, 512, 1920, 1280);
    if (auto* limits = getConstrainer()) limits->setFixedAspectRatio(canvasWidth / canvasHeight);
    setSize(1152, 768);
    startTimerHz(30);
}

DigitalChassis::~DigitalChassis() { stopTimer(); }

void DigitalChassis::paint(juce::Graphics& g) { g.fillAll(juce::Colour(0xff0a0c10)); }

void DigitalChassis::resized()
{
    surface->setTransform(juce::AffineTransform::scale(static_cast<float>(getWidth()) / canvasWidth));
}

void DigitalChassis::timerCallback() { surface->tick(); }
}
