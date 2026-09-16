#include "VocalRackEditor.h"

#include <juce_dsp/juce_dsp.h>

#include <cmath>

namespace amanorsac::vocalrack
{
namespace
{
using juce::Colour;
using APVTS = juce::AudioProcessorValueTreeState;

constexpr float canvasWidth = 1536.0f, canvasHeight = 1024.0f;
constexpr float cardTop = 116.0f, cardHeight = 226.0f, panelTop = 354.0f, panelHeight = 526.0f;
constexpr float columnWidth = 363.0f, gutter = 12.0f, left0 = 24.0f;

const Colour ground { 0xff0a0c10 }, panelBase { 0xff11141a }, inset { 0xff0b0e13 };
const Colour textMain { 0xffe9edf2 }, textDim { 0xff8a93a0 };
const Colour pink { 0xffff4fa3 }, cyan { 0xff31d0ff }, violet { 0xffa86bff }, amber { 0xffffc23d }, blue { 0xff2f8bff };
const Colour moduleColours[4] { pink, cyan, violet, amber };
const char* moduleTitles[4] { "DYNAMICS", "TONE", "SPACE", "FX" };
const char* styleIds[4] { "dyn_style", "tone_style", "space_style", "fx_style" };

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

float columnX(int m) { return left0 + static_cast<float>(m) * (columnWidth + gutter); }

const juce::StringArray& stylesFor(int m)
{
    switch (m)
    {
        case 0: return dynamicsStyles();
        case 1: return toneStyles();
        case 2: return spaceStyles();
        default: return fxStyles();
    }
}

void setParameter(APVTS& state, const juce::String& id, float realValue)
{
    if (auto* p = state.getParameter(id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost(p->convertTo0to1(realValue));
        p->endChangeGesture();
    }
}

float value(const APVTS& state, const juce::String& id)
{
    if (const auto* v = state.getRawParameterValue(id)) return v->load();
    return 0.0f;
}

/** A neon line: a soft halo under a bright core. */
void neon(juce::Graphics& g, const juce::Path& path, Colour colour, float width, float intensity = 1.0f)
{
    const juce::PathStrokeType::JointStyle joint = juce::PathStrokeType::curved;
    const auto cap = juce::PathStrokeType::rounded;
    g.setColour(colour.withAlpha(0.07f * intensity));
    g.strokePath(path, juce::PathStrokeType(width * 5.0f, joint, cap));
    g.setColour(colour.withAlpha(0.18f * intensity));
    g.strokePath(path, juce::PathStrokeType(width * 2.6f, joint, cap));
    g.setColour(colour.brighter(0.25f).withAlpha(juce::jmin(1.0f, 0.55f + 0.45f * intensity)));
    g.strokePath(path, juce::PathStrokeType(width, joint, cap));
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
        setColour(juce::AlertWindow::backgroundColourId, juce::Colour(0xff161a21));
        setColour(juce::AlertWindow::textColourId, textMain);
        setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff0d1015));
        setColour(juce::TextEditor::textColourId, textMain);
        setColour(juce::TextButton::buttonColourId, juce::Colour(0xff232a35));
    }

    juce::Font getPopupMenuFont() override { return uiFont(17.0f); }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float position,
                          float startAngle, float endAngle, juce::Slider& slider) override
    {
        const auto accent = colourOf(slider);
        const auto bounds = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                                   static_cast<float>(width), static_cast<float>(height));
        const auto centre = bounds.getCentre();
        const auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f - 3.0f;
        const auto thickness = juce::jmax(3.0f, radius * 0.08f);
        const auto arcRadius = radius - thickness * 0.5f;
        const auto angle = startAngle + position * (endAngle - startAngle);
        const juce::PathStrokeType stroke(thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

        juce::Path track;
        track.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
        g.setColour(juce::Colour(0xff1c2029));
        g.strokePath(track, stroke);

        if (position > 0.001f)
        {
            juce::Path arc;
            arc.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, angle, true);
            g.setColour(accent.withAlpha(0.18f));
            g.strokePath(arc, juce::PathStrokeType(thickness * 2.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour(accent);
            g.strokePath(arc, stroke);
        }

        const auto knob = radius * 0.78f;
        const auto face = juce::Rectangle<float>(knob * 2.0f, knob * 2.0f).withCentre(centre);
        g.setColour(juce::Colours::black.withAlpha(0.6f));
        g.fillEllipse(face.translated(0.0f, knob * 0.07f).expanded(knob * 0.05f));
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff393d46), centre.x, face.getY(),
                                               juce::Colour(0xff0f1115), centre.x, face.getBottom(), false));
        g.fillEllipse(face);
        g.setColour(juce::Colours::white.withAlpha(0.07f));
        g.drawEllipse(face.reduced(0.8f), 1.2f);

        const juce::Point<float> direction(std::sin(angle), -std::cos(angle));
        g.setColour(juce::Colours::white);
        g.fillEllipse(juce::Rectangle<float>(knob * 0.16f, knob * 0.16f).withCentre(centre + direction * (knob * 0.72f)));
    }

    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height, float position, float minPosition,
                          float maxPosition, juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        const auto accent = colourOf(slider);
        const auto r = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height));
        const auto bipolar = static_cast<bool>(slider.getProperties().getWithDefault("bipolar", false));

        if (style == juce::Slider::LinearVertical)
        {
            const auto cx = r.getCentreX();
            const auto track = juce::Rectangle<float>(6.0f, r.getHeight()).withCentre({ cx, r.getCentreY() });
            g.setColour(juce::Colour(0xff1a1e26));
            g.fillRoundedRectangle(track, 3.0f);
            const auto origin = bipolar ? r.getCentreY() : r.getBottom();
            g.setColour(accent);
            g.fillRoundedRectangle(juce::Rectangle<float>::leftTopRightBottom(track.getX(), juce::jmin(origin, position),
                                                                              track.getRight(), juce::jmax(origin, position)), 3.0f);
            thumb(g, { cx, position }, accent);
            return;
        }

        const auto cy = r.getCentreY();
        const auto track = juce::Rectangle<float>(r.getWidth(), 6.0f).withCentre({ r.getCentreX(), cy });
        g.setColour(juce::Colour(0xff1a1e26));
        g.fillRoundedRectangle(track, 3.0f);
        if (style == juce::Slider::TwoValueHorizontal)
        {
            g.setColour(accent);
            g.fillRoundedRectangle(juce::Rectangle<float>::leftTopRightBottom(minPosition, track.getY(), maxPosition, track.getBottom()), 3.0f);
            thumb(g, { minPosition, cy }, accent);
            thumb(g, { maxPosition, cy }, accent);
            return;
        }
        const auto origin = bipolar ? r.getCentreX() : r.getX();
        g.setColour(accent);
        g.fillRoundedRectangle(juce::Rectangle<float>::leftTopRightBottom(juce::jmin(origin, position), track.getY(),
                                                                          juce::jmax(origin, position), track.getBottom()), 3.0f);
        thumb(g, { position, cy }, accent);
    }

    int getSliderThumbRadius(juce::Slider&) override { return 11; }

private:
    static Colour colourOf(const juce::Slider& s)
    {
        return Colour(static_cast<juce::uint32>(static_cast<juce::int64>(
            s.getProperties().getWithDefault("accent", static_cast<juce::int64>(0xffffffff)))));
    }

    static void thumb(juce::Graphics& g, juce::Point<float> p, Colour accent)
    {
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.fillEllipse(juce::Rectangle<float>(24.0f, 24.0f).withCentre(p.translated(0.0f, 1.5f)));
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff4a4f59), p.x, p.y - 11.0f, juce::Colour(0xff1b1e24), p.x, p.y + 11.0f, false));
        g.fillEllipse(juce::Rectangle<float>(22.0f, 22.0f).withCentre(p));
        g.setColour(accent);
        g.fillEllipse(juce::Rectangle<float>(7.0f, 7.0f).withCentre(p));
    }
};

// ------------------------------------------------------------------ small controls
Colour accentOf(const juce::Component& c)
{
    return Colour(static_cast<juce::uint32>(static_cast<juce::int64>(
        c.getProperties().getWithDefault("accent", static_cast<juce::int64>(0xffffffff)))));
}

class PowerButton final : public juce::ToggleButton
{
public:
    void paintButton(juce::Graphics& g, bool over, bool) override
    {
        const auto r = getLocalBounds().toFloat().reduced(3.0f);
        const auto on = getToggleState();
        const auto colour = on ? accentOf(*this) : textDim.withAlpha(over ? 0.9f : 0.55f);
        const auto c = r.getCentre();
        const auto radius = r.getWidth() * 0.32f;
        juce::Path glyph;
        glyph.addCentredArc(c.x, c.y, radius, radius, 0.0f, juce::MathConstants<float>::pi * 0.24f,
                            juce::MathConstants<float>::pi * 1.76f, true);
        glyph.startNewSubPath(c.x, c.y - radius * 1.25f);
        glyph.lineTo(c.x, c.y - radius * 0.1f);
        if (on) neon(g, glyph, colour, 2.2f, 0.9f);
        else
        {
            g.setColour(colour);
            g.strokePath(glyph, juce::PathStrokeType(2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }
};

class SoloButton final : public juce::ToggleButton
{
public:
    void paintButton(juce::Graphics& g, bool over, bool) override
    {
        const auto r = getLocalBounds().toFloat().reduced(4.0f);
        const auto on = getToggleState();
        const auto colour = accentOf(*this);
        if (on)
        {
            g.setColour(colour);
            g.fillEllipse(r);
        }
        g.setColour(on ? colour : textDim.withAlpha(over ? 0.9f : 0.5f));
        g.drawEllipse(r, 1.6f);
        g.setColour(on ? juce::Colours::black : textDim.withAlpha(over ? 1.0f : 0.7f));
        g.setFont(uiFont(r.getHeight() * 0.62f, true));
        g.drawText("S", r, juce::Justification::centred, false);
    }
};

class Chevron final : public juce::Button
{
public:
    explicit Chevron(bool pointsRight) : juce::Button({}), right(pointsRight) {}
    void paintButton(juce::Graphics& g, bool over, bool down) override
    {
        const auto c = getLocalBounds().toFloat().getCentre();
        const auto d = right ? 1.0f : -1.0f;
        juce::Path p;
        p.startNewSubPath(c.x - 5.0f * d, c.y - 11.0f);
        p.lineTo(c.x + 5.0f * d, c.y);
        p.lineTo(c.x - 5.0f * d, c.y + 11.0f);
        g.setColour(down ? juce::Colours::white : over ? textMain : textDim);
        g.strokePath(p, juce::PathStrokeType(2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
private:
    bool right;
};

class IconButton final : public juce::Button
{
public:
    enum class Icon { undo, redo, save };
    explicit IconButton(Icon i) : juce::Button({}), icon(i) {}

    void paintButton(juce::Graphics& g, bool over, bool down) override
    {
        const auto r = getLocalBounds().toFloat().reduced(1.0f);
        g.setColour(juce::Colour(over ? 0xff1a1f28 : 0xff12161d));
        g.fillRoundedRectangle(r, 8.0f);
        g.setColour(juce::Colour(0xff2b313b));
        g.drawRoundedRectangle(r, 8.0f, 1.2f);
        const auto c = r.getCentre();
        g.setColour(! isEnabled() ? textDim.withAlpha(0.35f) : down ? juce::Colours::white : textMain);
        juce::Path p;
        if (icon == Icon::save)
        {
            p.startNewSubPath(c.x - 8.0f, c.y);
            p.lineTo(c.x + 8.0f, c.y);
            p.startNewSubPath(c.x, c.y - 8.0f);
            p.lineTo(c.x, c.y + 8.0f);
        }
        else
        {
            const auto d = icon == Icon::undo ? -1.0f : 1.0f;
            p.addCentredArc(c.x, c.y + 2.0f, 8.0f, 7.0f, 0.0f, -juce::MathConstants<float>::halfPi * d,
                            juce::MathConstants<float>::halfPi * 1.2f * d, true);
            const auto tip = juce::Point<float>(c.x + 0.0f, c.y - 5.0f);
            p.startNewSubPath(tip.translated(4.0f * -d, -4.0f));
            p.lineTo(tip);
            p.lineTo(tip.translated(4.0f * -d, 4.0f));
        }
        g.strokePath(p, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
private:
    Icon icon;
};

class BypassButton final : public juce::ToggleButton
{
public:
    void paintButton(juce::Graphics& g, bool over, bool) override
    {
        const auto r = getLocalBounds().toFloat().reduced(1.0f);
        const auto on = getToggleState();
        g.setColour(on ? juce::Colour(0xffd83b3b) : juce::Colour(over ? 0xff1a1f28 : 0xff12161d));
        g.fillRoundedRectangle(r, 8.0f);
        g.setColour(on ? juce::Colour(0xffff6b6b) : juce::Colour(0xff2b313b));
        g.drawRoundedRectangle(r, 8.0f, 1.2f);
        g.setColour(on ? juce::Colours::white : textMain);
        g.setFont(uiFont(17.0f, false, 0.08f));
        g.drawText("BYPASS", r, juce::Justification::centred, false);
    }
};

/** A value box you drag up and down: "TIME  2.3 s". */
class Pill final : public juce::Slider
{
public:
    Pill(juce::String caption, Colour colour, std::function<juce::String()> textFn)
        : label(std::move(caption)), text(std::move(textFn))
    {
        getProperties().set("accent", static_cast<juce::int64>(colour.getARGB()));
        setSliderStyle(juce::Slider::RotaryVerticalDrag);
        setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        setMouseDragSensitivity(180);
    }

    void paint(juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced(1.0f);
        const auto over = isMouseOverOrDragging();
        g.setColour(juce::Colour(over ? 0xff151a22 : 0xff0e1117));
        g.fillRoundedRectangle(r, 7.0f);
        g.setColour(over ? accentOf(*this).withAlpha(0.7f) : juce::Colour(0xff2b313b));
        g.drawRoundedRectangle(r, 7.0f, 1.3f);
        const auto inner = r.reduced(12.0f, 0.0f);
        g.setColour(textDim);
        g.setFont(uiFont(13.0f, true, 0.12f));
        g.drawText(label, inner, juce::Justification::centredLeft, false);
        g.setColour(textMain);
        g.setFont(uiFont(16.0f, true));
        g.drawText(text ? text() : juce::String(), inner, juce::Justification::centredRight, false);
    }

private:
    juce::String label;
    std::function<juce::String()> text;
};

struct ClickArea final : public juce::Component
{
    std::function<void()> onClick;
    void mouseUp(const juce::MouseEvent& e) override { if (e.mouseWasClicked() && onClick) onClick(); }
};

std::unique_ptr<juce::Slider> makeKnob(Colour accent, bool bipolar = false)
{
    auto s = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox);
    s->getProperties().set("accent", static_cast<juce::int64>(accent.getARGB()));
    s->getProperties().set("bipolar", bipolar);
    s->setRotaryParameters(juce::MathConstants<float>::pi * 1.22f, juce::MathConstants<float>::pi * 2.78f, true);
    s->setMouseDragSensitivity(240);
    return s;
}

std::unique_ptr<juce::Slider> makeFader(Colour accent, juce::Slider::SliderStyle style, bool bipolar)
{
    auto s = std::make_unique<juce::Slider>(style, juce::Slider::NoTextBox);
    s->getProperties().set("accent", static_cast<juce::int64>(accent.getARGB()));
    s->getProperties().set("bipolar", bipolar);
    return s;
}

juce::String decimals(float v, int places)
{
    auto t = juce::String(v, places);
    if (t.containsChar('.')) t = t.trimCharactersAtEnd("0").trimCharactersAtEnd(".");
    return t == "-0" ? juce::String("0") : t;
}

juce::String signedDb(float v) { return (v > 0.05f ? "+" : "") + juce::String(v, 1) + " dB"; }
}

// ================================================================== surface
class VocalRackEditor::Surface final : public juce::Component
{
public:
    explicit Surface(VocalRackProcessor& owner)
        : processor(owner), state(owner.state), fft(12),
          window(4096, juce::dsp::WindowingFunction<float>::hann, false)
    {
        setLookAndFeel(&look);
        setSize(static_cast<int>(canvasWidth), static_cast<int>(canvasHeight));
        spectrum.assign(2048, -100.0f);
        fftData.assign(8192, 0.0f);
        ring.assign(4096, 0.0f);
        processor.analyserWanted.store(true);

        buildHeader();
        for (int m = 0; m < 4; ++m) buildCard(m);
        buildDynamics();
        buildTone();
        buildSpace();
        buildFx();
        buildFooter();
    }

    ~Surface() override
    {
        processor.analyserWanted.store(false);
        setLookAndFeel(nullptr);
    }

    void tick()
    {
        clock += 1.0f / 30.0f;
        auto& m = processor.meters;
        const auto decay = [](float& shown, float now, float fall) { shown = juce::jmax(now, shown - fall); };
        decay(inLevel, juce::Decibels::gainToDecibels(m.input.exchange(0.0f), -90.0f), 1.5f);
        decay(outLevel, juce::Decibels::gainToDecibels(m.output.exchange(0.0f), -90.0f), 1.5f);
        decay(gateLevel, m.gateLevel.load(), 1.5f);
        decay(compReduction, m.compReduction.load(), 0.5f);
        decay(deessReduction, m.deessReduction.load(), 0.5f);
        decay(verbSend, m.verbSend.load(), 0.02f);
        decay(verbOut, m.verbOut.load(), 0.02f);
        decay(verbDuck, m.verbDuck.load(), 0.02f);
        decay(delaySend, m.delaySend.load(), 0.02f);
        decay(delayOut, m.delayOut.load(), 0.02f);
        decay(delayDuck, m.delayDuck.load(), 0.02f);
        decay(fxActivity, m.fxActivity.load(), 0.01f);
        gateOpen = m.gateOpen.load();
        pullAnalyser();

        // Group whatever changed since the last quiet moment into one undo step.
        if (! juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown()
            && processor.undo.getNumActionsInCurrentTransaction() > 0)
            processor.undo.beginNewTransaction();
        undoButton.setEnabled(processor.undo.canUndo());
        redoButton.setEnabled(processor.undo.canRedo());

        syncFilterRange();
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(ground);
        paintHeader(g);
        for (int m = 0; m < 4; ++m) paintCard(g, m);
        for (int m = 0; m < 4; ++m) paintPanelBase(g, m);
        paintDynamics(g);
        paintTone(g);
        paintSpace(g);
        paintFx(g);
        paintFooter(g);
    }

private:
    // ------------------------------------------------------------------ building
    juce::Slider& own(std::unique_ptr<juce::Slider> slider, const juce::String& id, juce::Rectangle<int> bounds)
    {
        auto& s = *slider;
        s.setBounds(bounds);
        addAndMakeVisible(s);
        sliderAttachments.push_back(std::make_unique<APVTS::SliderAttachment>(state, id, s));
        if (auto* p = state.getParameter(id))
            s.setDoubleClickReturnValue(true, p->convertFrom0to1(p->getDefaultValue()));
        sliders.push_back(std::move(slider));
        return s;
    }

    void ownButton(std::unique_ptr<juce::Button> button, const juce::String& id, juce::Rectangle<int> bounds, Colour accent)
    {
        button->getProperties().set("accent", static_cast<juce::int64>(accent.getARGB()));
        button->setBounds(bounds);
        addAndMakeVisible(*button);
        buttonAttachments.push_back(std::make_unique<APVTS::ButtonAttachment>(state, id, *button));
        button->onStateChange = [this] { repaint(); };
        buttons.push_back(std::move(button));
    }

    Pill& pill(const juce::String& caption, const juce::String& id, Colour accent, juce::Rectangle<int> bounds,
               std::function<juce::String()> text)
    {
        auto p = std::make_unique<Pill>(caption, accent, std::move(text));
        auto& ref = *p;
        own(std::move(p), id, bounds);
        return ref;
    }

    void buildHeader()
    {
        for (auto* b : { &undoButton, &redoButton, &saveButton }) addAndMakeVisible(*b);
        undoButton.setBounds(360, 28, 50, 48);
        redoButton.setBounds(412, 28, 50, 48);
        undoButton.onClick = [this] { processor.undo.undo(); };
        redoButton.onClick = [this] { processor.undo.redo(); };

        addAndMakeVisible(presetArea);
        addAndMakeVisible(presetPrev);
        addAndMakeVisible(presetNext);
        presetArea.setBounds(496, 28, 380, 48);
        presetPrev.setBounds(878, 28, 50, 48);
        presetNext.setBounds(930, 28, 50, 48);
        saveButton.setBounds(994, 28, 50, 48);
        presetArea.onClick = [this] { showPresetMenu(); };
        presetPrev.onClick = [this] { processor.stepPreset(-1); };
        presetNext.onClick = [this] { processor.stepPreset(1); };
        saveButton.onClick = [this] { askForPresetName(); };

        ownButton(std::make_unique<BypassButton>(), "bypass", { 1342, 28, 150, 48 }, juce::Colours::red);
    }

    void buildCard(int m)
    {
        const auto x = juce::roundToInt(columnX(m));
        const auto w = juce::roundToInt(columnWidth);
        const auto top = juce::roundToInt(cardTop);
        const juce::String prefix(modulePrefix[m]);
        const auto colour = moduleColours[m];

        ownButton(std::make_unique<PowerButton>(), prefix + "_on", { x + 12, top + 10, 40, 40 }, colour);
        ownButton(std::make_unique<SoloButton>(), prefix + "_solo", { x + w - 50, top + 12, 36, 36 }, colour);

        // Own the buttons first and hold plain pointers: taking a reference to a
        // vector entry and then growing the vector would leave it dangling.
        auto* previous = cardButtons.emplace_back(std::make_unique<Chevron>(false)).get();
        auto* following = cardButtons.emplace_back(std::make_unique<Chevron>(true)).get();
        previous->setBounds(x + 16, top + 70, 40, 60);
        following->setBounds(x + w - 56, top + 70, 40, 60);
        addAndMakeVisible(*previous);
        addAndMakeVisible(*following);
        previous->onClick = [this, m] { stepStyle(m, -1); };
        following->onClick = [this, m] { stepStyle(m, 1); };

        auto trim = makeKnob(colour, true);
        own(std::move(trim), prefix + "_trim", { x + w - 54, top + 150, 40, 40 });
    }

    void buildDynamics()
    {
        const auto x = juce::roundToInt(columnX(0));
        const auto top = juce::roundToInt(panelTop);
        // The gate threshold is set right on the gate meter.
        auto gate = makeFader(pink, juce::Slider::LinearVertical, false);
        gate->setAlpha(0.0f);   // the meter draws the handle
        gateSlider = &own(std::move(gate), "gate", { x + 26, top + 58, 30, 420 });
        gateSlider->onValueChange = [this] { repaint(); };

        own(makeKnob(pink), "comp", { x + 196, top + 40, 140, 140 });
        pill("COLOUR", "color", pink, { x + 186, top + 212, 160, 38 },
             [this] { return juce::String(juce::roundToInt(value(state, "color"))) + " %"; });
        own(makeKnob(pink), "deess", { x + 196, top + 278, 140, 140 });
        pill("FOCUS", "ds_focus", pink, { x + 186, top + 450, 160, 38 },
             [this] { return decimals(value(state, "ds_focus") / 1000.0f, 1) + " kHz"; });
    }

    void buildTone()
    {
        const auto x = juce::roundToInt(columnX(1));
        const auto top = juce::roundToInt(panelTop);
        const char* ids[] { "low", "mid", "high", "air" };
        for (int i = 0; i < 4; ++i)
        {
            auto fader = makeFader(cyan, juce::Slider::LinearVertical, i < 3);
            own(std::move(fader), ids[i], { x + 30 + i * 80, top + 214, 60, 196 });
        }

        filterRange = std::make_unique<juce::Slider>(juce::Slider::TwoValueHorizontal, juce::Slider::NoTextBox);
        filterRange->getProperties().set("accent", static_cast<juce::int64>(cyan.getARGB()));
        filterRange->setRange(0.0, 1.0);
        filterRange->setBounds(x + 60, top + 462, 243, 34);
        addAndMakeVisible(*filterRange);
        filterRange->onDragStart = [this]
        {
            for (const auto* id : { "hp", "lp" }) if (auto* p = state.getParameter(id)) p->beginChangeGesture();
        };
        filterRange->onDragEnd = [this]
        {
            for (const auto* id : { "hp", "lp" }) if (auto* p = state.getParameter(id)) p->endChangeGesture();
        };
        filterRange->onValueChange = [this]
        {
            if (syncing) return;
            auto toFrequency = [](double t) { return static_cast<float>(20.0 * std::pow(1000.0, t)); };
            if (auto* p = state.getParameter("hp"))
                p->setValueNotifyingHost(p->convertTo0to1(juce::jlimit(20.0f, 800.0f, toFrequency(filterRange->getMinValue()))));
            if (auto* p = state.getParameter("lp"))
                p->setValueNotifyingHost(p->convertTo0to1(juce::jlimit(2000.0f, 20000.0f, toFrequency(filterRange->getMaxValue()))));
        };
        syncFilterRange();
    }

    void buildSpace()
    {
        const auto x = juce::roundToInt(columnX(2));
        const auto top = juce::roundToInt(panelTop);
        pill("VERB DUCK", "verb_duck", violet, { x + 22, top + 150, 154, 34 },
             [this] { return juce::String(juce::roundToInt(value(state, "verb_duck"))) + " %"; });
        pill("DLY DUCK", "dly_duck", violet, { x + 187, top + 150, 154, 34 },
             [this] { return juce::String(juce::roundToInt(value(state, "dly_duck"))) + " %"; });

        own(makeKnob(violet), "reverb", { x + 34, top + 214, 130, 130 });
        own(makeKnob(violet), "delay", { x + 199, top + 214, 130, 130 });
        pill("TIME", "verb_time", violet, { x + 22, top + 374, 154, 34 },
             [this] { return decimals(value(state, "verb_time"), 1) + " s"; });
        pill("RATE", "dly_rate", violet, { x + 187, top + 374, 154, 34 },
             [this] { return delayRates()[juce::jlimit(0, 6, juce::roundToInt(value(state, "dly_rate")))]; });
        pill("FEEDBACK", "dly_feedback", violet, { x + 187, top + 414, 154, 34 },
             [this] { return juce::String(juce::roundToInt(value(state, "dly_feedback"))) + " %"; });

        auto tone = makeFader(violet, juce::Slider::LinearHorizontal, true);
        own(std::move(tone), "space_tone", { x + 60, top + 462, 243, 34 });
    }

    void buildFx()
    {
        const auto x = juce::roundToInt(columnX(3));
        const auto top = juce::roundToInt(panelTop);
        own(makeKnob(amber), "fx_amount", { x + 71, top + 50, 220, 220 });
        pill("RATE", "fx_rate", amber, { x + 101, top + 330, 160, 36 },
             [this] { return decimals(value(state, "fx_rate"), 2) + " Hz"; });

        addAndMakeVisible(routingArea);
        routingArea.setBounds(x + 40, top + 396, 283, 96);
        routingArea.onClick = [this] { setParameter(state, "fx_post", value(state, "fx_post") > 0.5f ? 0.0f : 1.0f); };
    }

    void buildFooter()
    {
        own(makeKnob(blue, true), "in_gain", { 30, 948, 54, 54 });
        own(makeKnob(blue, true), "out_gain", { 1452, 948, 54, 54 });
    }

    void stepStyle(int m, int delta)
    {
        const auto count = stylesFor(m).size();
        const auto current = juce::roundToInt(value(state, styleIds[m]));
        setParameter(state, styleIds[m], static_cast<float>(((current + delta) % count + count) % count));
    }

    void syncFilterRange()
    {
        if (filterRange == nullptr || filterRange->isMouseButtonDown()) return;
        auto toPosition = [](float f) { return std::log(f / 20.0) / std::log(1000.0); };
        const juce::ScopedValueSetter<bool> guard(syncing, true);
        filterRange->setMinAndMaxValues(toPosition(value(state, "hp")), toPosition(value(state, "lp")), juce::dontSendNotification);
    }

    // ------------------------------------------------------------------ analyser
    void pullAnalyser()
    {
        auto& fifo = processor.analyserFifo;
        const auto ready = fifo.getNumReady();
        if (ready <= 0)
        {
            for (auto& s : spectrum) s = juce::jmax(-100.0f, s - 1.0f);
            return;
        }
        const auto scope = fifo.read(ready);
        auto take = [this](int start, int size)
        {
            for (int i = 0; i < size; ++i)
            {
                ring[static_cast<size_t>(ringPosition)] = processor.analyserBuffer[static_cast<size_t>(start + i)];
                ringPosition = (ringPosition + 1) % 4096;
            }
        };
        take(scope.startIndex1, scope.blockSize1);
        take(scope.startIndex2, scope.blockSize2);

        for (int i = 0; i < 4096; ++i) fftData[static_cast<size_t>(i)] = ring[static_cast<size_t>((ringPosition + i) % 4096)];
        std::fill(fftData.begin() + 4096, fftData.end(), 0.0f);
        window.multiplyWithWindowingTable(fftData.data(), 4096);
        fft.performFrequencyOnlyForwardTransform(fftData.data());
        for (int b = 0; b < 2048; ++b)
        {
            const auto db = juce::Decibels::gainToDecibels(fftData[static_cast<size_t>(b)] * 4.0f / 4096.0f, -100.0f);
            auto& s = spectrum[static_cast<size_t>(b)];
            s += (db > s ? 0.6f : 0.2f) * (db - s);
        }
    }

    [[nodiscard]] float spectrumAt(double f) const
    {
        const auto rate = juce::jmax(8000.0, processor.getSampleRate() > 0 ? processor.getSampleRate() : 48000.0);
        const auto bin = f * 4096.0 / rate;
        const auto b = juce::jlimit(1, 2046, static_cast<int>(bin));
        const auto t = static_cast<float>(juce::jlimit(0.0, 1.0, bin - b));
        return spectrum[static_cast<size_t>(b)] + t * (spectrum[static_cast<size_t>(b + 1)] - spectrum[static_cast<size_t>(b)]);
    }

    // ------------------------------------------------------------------ menus
    void showPresetMenu()
    {
        juce::PopupMenu menu;
        const auto& bank = factoryPresets();
        for (const auto& category : presetCategories())
        {
            juce::PopupMenu sub;
            for (int i = 0; i < static_cast<int>(bank.size()); ++i)
                if (bank[static_cast<size_t>(i)].category == category)
                    sub.addItem(i + 1, bank[static_cast<size_t>(i)].name, true, bank[static_cast<size_t>(i)].name == processor.presetName());
            menu.addSubMenu(category, sub);
        }
        const auto user = VocalRackProcessor::userPresets();
        menu.addSeparator();
        juce::PopupMenu mine;
        for (int i = 0; i < user.size(); ++i)
            mine.addItem(1000 + i, user[i].getFileNameWithoutExtension(), true, user[i].getFileNameWithoutExtension() == processor.presetName());
        menu.addSubMenu("My Presets", mine, ! user.isEmpty());
        menu.addItem(2000, "Open presets folder");

        juce::Component::SafePointer<Surface> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&presetArea), [safe, user](int result)
        {
            if (safe == nullptr || result <= 0) return;
            if (result == 2000)
            {
                auto folder = VocalRackProcessor::userPresetFolder();
                folder.createDirectory();
                folder.startAsProcess();
            }
            else if (result >= 1000) safe->processor.loadUserPreset(user[result - 1000]);
            else safe->processor.loadFactory(result - 1);
        });
    }

    void askForPresetName()
    {
        auto* window = new juce::AlertWindow("Save preset", "Name this vocal chain.", juce::MessageBoxIconType::NoIcon, this);
        window->addTextEditor("name", processor.presetName());
        window->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
        window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        juce::Component::SafePointer<Surface> safe(this);
        window->enterModalState(true, juce::ModalCallbackFunction::create([safe, window](int result)
        {
            if (safe != nullptr && result == 1) safe->processor.saveUserPreset(window->getTextEditorContents("name"));
        }), true);
    }

    // ------------------------------------------------------------------ painting
    void paintHeader(juce::Graphics& g)
    {
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff151a22), 0.0f, 0.0f, juce::Colour(0xff0c0f14), 0.0f, 100.0f, false));
        g.fillRect(0.0f, 0.0f, canvasWidth, 100.0f);
        g.setColour(juce::Colour(0xff1c2129));
        g.drawHorizontalLine(100, 0.0f, canvasWidth);

        // Mark: four bars in the module colours, rising like a level meter.
        for (int i = 0; i < 4; ++i)
        {
            const auto h = 14.0f + 6.0f * static_cast<float>(i) + 3.0f * std::sin(clock * 3.0f + static_cast<float>(i));
            juce::Path bar;
            bar.addRoundedRectangle(juce::Rectangle<float>(40.0f + static_cast<float>(i) * 9.0f, 62.0f - h, 5.0f, h), 2.5f);
            g.setColour(moduleColours[i]);
            g.fillPath(bar);
        }

        const auto titleFont = uiFont(34.0f, true, 0.06f);
        g.setFont(titleFont);
        g.setColour(juce::Colours::white);
        g.drawText("VOCAL", juce::Rectangle<float>(90.0f, 20.0f, 200.0f, 42.0f), juce::Justification::centredLeft, false);
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText(titleFont, "VOCAL ", 0.0f, 0.0f);
        g.setColour(blue);
        g.drawText("RACK", juce::Rectangle<float>(90.0f + glyphs.getBoundingBox(0, -1, true).getWidth(), 20.0f, 160.0f, 42.0f),
                   juce::Justification::centredLeft, false);
        g.setColour(textDim);
        g.setFont(uiFont(13.0f, false, 0.42f));
        g.drawText("AMANORSAC.STUDIO", juce::Rectangle<float>(92.0f, 60.0f, 240.0f, 18.0f), juce::Justification::centredLeft, false);

        const auto box = presetArea.getBounds().toFloat();
        g.setColour(juce::Colour(0xff12161d));
        g.fillRoundedRectangle(box, 8.0f);
        g.setColour(juce::Colour(0xff2b313b));
        g.drawRoundedRectangle(box.reduced(0.6f), 8.0f, 1.2f);
        g.setColour(textDim);
        g.setFont(uiFont(11.0f, true, 0.2f));
        g.drawText("PRESET", box.reduced(16.0f, 0.0f).withHeight(18.0f).translated(0.0f, 4.0f), juce::Justification::centredLeft, false);
        g.setColour(textMain);
        g.setFont(uiFont(20.0f));
        g.drawFittedText(processor.presetName() + (processor.presetModified() ? " *" : ""),
                         box.reduced(16.0f, 0.0f).withTrimmedTop(16.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
        for (const auto* b : { &presetPrev, &presetNext })
        {
            g.setColour(juce::Colour(0xff12161d));
            g.fillRoundedRectangle(b->getBounds().toFloat(), 8.0f);
            g.setColour(juce::Colour(0xff2b313b));
            g.drawRoundedRectangle(b->getBounds().toFloat().reduced(0.6f), 8.0f, 1.2f);
        }
    }

    [[nodiscard]] bool moduleOn(int m) const
    {
        const juce::String p(modulePrefix[m]);
        if (value(state, p + "_on") < 0.5f) return false;
        bool anySolo = false;
        for (int i = 0; i < 4; ++i) anySolo = anySolo || value(state, juce::String(modulePrefix[i]) + "_solo") > 0.5f;
        return ! anySolo || value(state, p + "_solo") > 0.5f;
    }

    void paintCard(juce::Graphics& g, int m)
    {
        const auto r = juce::Rectangle<float>(columnX(m), cardTop, columnWidth, cardHeight);
        const auto colour = moduleColours[m];
        const auto on = moduleOn(m);

        g.setColour(panelBase);
        g.fillRoundedRectangle(r, 12.0f);
        g.setGradientFill(juce::ColourGradient(colour.withAlpha(on ? 0.16f : 0.04f), r.getCentreX(), r.getY() + 110.0f,
                                               colour.withAlpha(0.0f), r.getCentreX(), r.getY() + 250.0f, true));
        g.fillRoundedRectangle(r, 12.0f);
        g.setColour(on ? colour.withAlpha(0.35f) : juce::Colour(0xff222731));
        g.drawRoundedRectangle(r.reduced(0.6f), 12.0f, 1.2f);

        g.setColour(on ? colour : textDim);
        g.setFont(uiFont(20.0f, false, 0.08f));
        g.drawText(moduleTitles[m], r.withHeight(60.0f), juce::Justification::centred, false);

        const auto iconArea = juce::Rectangle<float>(120.0f, 96.0f).withCentre({ r.getCentreX(), r.getY() + 104.0f });
        paintGlyph(g, m, iconArea, on);

        const auto& styles = stylesFor(m);
        const auto style = juce::jlimit(0, styles.size() - 1, juce::roundToInt(value(state, styleIds[m])));
        g.setColour(on ? textMain : textDim);
        g.setFont(uiFont(19.0f, false, 0.12f));
        g.drawText(styles[style].toUpperCase(), juce::Rectangle<float>(r.getX(), r.getY() + 158.0f, r.getWidth(), 26.0f),
                   juce::Justification::centred, false);
        const auto dots = styles.size();
        for (int i = 0; i < dots; ++i)
        {
            const auto x = r.getCentreX() + (static_cast<float>(i) - (static_cast<float>(dots) - 1.0f) * 0.5f) * 11.0f;
            g.setColour(i == style ? (on ? colour : textMain) : textDim.withAlpha(0.35f));
            g.fillEllipse(juce::Rectangle<float>(5.0f, 5.0f).withCentre({ x, r.getY() + 196.0f }));
        }

        g.setColour(textDim);
        g.setFont(uiFont(11.5f));
        g.drawText(signedDb(value(state, juce::String(modulePrefix[m]) + "_trim")),
                   juce::Rectangle<float>(r.getRight() - 80.0f, r.getY() + 196.0f, 72.0f, 18.0f), juce::Justification::centredRight, false);
    }

    /** Each module draws its own living icon. */
    void paintGlyph(juce::Graphics& g, int m, juce::Rectangle<float> area, bool on)
    {
        const auto colour = on ? moduleColours[m] : textDim;
        const auto c = area.getCentre();
        const auto intensity = on ? 1.0f : 0.25f;

        if (m == 0)   // nested frames that tighten as the compressor works
        {
            const auto squeeze = juce::jlimit(0.0f, 1.0f, compReduction / 14.0f);
            for (int i = 0; i < 3; ++i)
            {
                const auto size = (74.0f - static_cast<float>(i) * 20.0f) * (1.0f - 0.22f * squeeze * static_cast<float>(3 - i) / 3.0f);
                juce::Path frame;
                frame.addRoundedRectangle(juce::Rectangle<float>(size, size).withCentre(c), size * 0.28f);
                neon(g, frame, colour, 2.4f, intensity * (0.55f + 0.15f * static_cast<float>(i)));
            }
            g.setColour(colour.withAlpha(intensity));
            g.fillEllipse(juce::Rectangle<float>(8.0f, 8.0f).withCentre(c));
        }
        else if (m == 1)   // four bars shaped by the tone settings
        {
            const char* ids[] { "low", "mid", "high", "air" };
            const auto lift = juce::jlimit(0.0f, 1.0f, (outLevel + 50.0f) / 50.0f);
            for (int i = 0; i < 4; ++i)
            {
                const auto v = value(state, ids[i]);
                const auto height = 30.0f + v * 2.6f + 8.0f * lift * std::sin(clock * 5.0f + static_cast<float>(i) * 1.3f);
                const auto x = c.x - 42.0f + static_cast<float>(i) * 28.0f;
                juce::Path bar;
                bar.startNewSubPath(x, c.y + 32.0f);
                bar.lineTo(x, c.y + 32.0f - juce::jlimit(8.0f, 66.0f, height));
                neon(g, bar, colour, 5.0f, intensity);
            }
        }
        else if (m == 2)   // ripples leaving the centre, carried by the reverb
        {
            const auto energy = juce::jlimit(0.15f, 1.0f, verbOut * 6.0f + 0.15f);
            for (int i = 0; i < 3; ++i)
            {
                const auto phase = std::fmod(clock * 0.45f + static_cast<float>(i) / 3.0f, 1.0f);
                const auto radius = 10.0f + phase * 40.0f;
                juce::Path ring;
                ring.addEllipse(juce::Rectangle<float>(radius * 2.0f, radius * 1.3f).withCentre(c));
                neon(g, ring, colour, 2.0f, intensity * energy * (1.0f - phase));
            }
            juce::Path core;
            core.addEllipse(juce::Rectangle<float>(18.0f, 18.0f).withCentre(c));
            neon(g, core, colour, 2.4f, intensity);
        }
        else   // a ribbon whose shape follows the effect
        {
            const auto style = juce::roundToInt(value(state, "fx_style"));
            const auto amount = value(state, "fx_amount") * 0.01f;
            juce::Path wave, second;
            for (auto x = -50.0f; x <= 50.0f; x += 2.0f)
            {
                const auto t = x / 50.0f;
                auto y = std::sin(t * 6.0f + clock * 3.0f);
                if (style == 2) y = std::round(y * 3.0f) / 3.0f;
                if (style == 3) y *= 0.4f + 0.6f * std::abs(std::sin(t * 2.0f + clock * 2.0f));
                if (style == 4) y = std::sin(t * 6.0f + clock * 3.0f + 0.8f * std::sin(clock * 1.3f + t * 2.0f));
                if (style == 5) y = juce::jlimit(-0.55f, 0.55f, y * (1.0f + 3.0f * amount)) / 0.55f;
                const auto h = 22.0f * (0.45f + 0.55f * juce::jmax(amount, 0.2f));
                const auto p = juce::Point<float>(c.x + x, c.y + y * h * (1.0f - t * t * 0.6f));
                if (x <= -50.0f) wave.startNewSubPath(p); else wave.lineTo(p);
                if (style <= 1)
                {
                    const auto q = juce::Point<float>(c.x + x, c.y + std::sin(t * 6.0f + clock * 3.0f + 1.4f) * h * 0.7f);
                    if (x <= -50.0f) second.startNewSubPath(q); else second.lineTo(q);
                }
            }
            neon(g, wave, colour, 2.6f, intensity);
            if (style <= 1) neon(g, second, colour, 1.8f, intensity * 0.6f);
        }
    }

    void paintPanelBase(juce::Graphics& g, int m)
    {
        const auto r = juce::Rectangle<float>(columnX(m), panelTop, columnWidth, panelHeight);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff161a21), r.getX(), r.getY(), juce::Colour(0xff0f1217), r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle(r, 12.0f);
        g.setColour(juce::Colour(0xff222731));
        g.drawRoundedRectangle(r.reduced(0.6f), 12.0f, 1.2f);
        if (! moduleOn(m))
        {
            g.setColour(ground.withAlpha(0.45f));
            g.fillRoundedRectangle(r, 12.0f);
        }
    }

    void label(juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, float size = 15.0f) const
    {
        g.setColour(textMain);
        g.setFont(uiFont(size, false, 0.1f));
        g.drawText(text, area, juce::Justification::centred, false);
    }

    /** A vertical meter: fills from the bottom (level) or the top (reduction). */
    void verticalMeter(juce::Graphics& g, juce::Rectangle<float> r, float fraction, bool fromTop, Colour colour) const
    {
        g.setColour(inset);
        g.fillRoundedRectangle(r, 5.0f);
        g.setColour(juce::Colour(0xff1d222b));
        g.drawRoundedRectangle(r, 5.0f, 1.0f);
        const auto h = r.getHeight() * juce::jlimit(0.0f, 1.0f, fraction);
        const auto bar = fromTop ? r.withHeight(h) : r.withTop(r.getBottom() - h);
        g.setGradientFill(juce::ColourGradient(colour.withAlpha(0.95f), 0.0f, r.getY(), colour.withAlpha(0.45f), 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle(bar.reduced(4.0f, 2.0f), 3.0f);
    }

    void scale(juce::Graphics& g, juce::Rectangle<float> r, const juce::StringArray& marks, bool left) const
    {
        g.setColour(textDim.withAlpha(0.8f));
        g.setFont(uiFont(10.5f));
        for (int i = 0; i < marks.size(); ++i)
        {
            const auto y = r.getY() + r.getHeight() * static_cast<float>(i) / static_cast<float>(marks.size() - 1);
            g.drawText(marks[i], juce::Rectangle<float>(left ? r.getX() - 34.0f : r.getRight() + 4.0f, y - 7.0f, 30.0f, 14.0f),
                       left ? juce::Justification::centredRight : juce::Justification::centredLeft, false);
        }
    }

    void paintDynamics(juce::Graphics& g)
    {
        const auto x = columnX(0);
        const auto top = panelTop;

        const auto gateMeter = juce::Rectangle<float>(x + 30.0f, top + 66.0f, 22.0f, 404.0f);
        const auto grMeter = juce::Rectangle<float>(x + 86.0f, top + 66.0f, 22.0f, 404.0f);
        const auto dsMeter = juce::Rectangle<float>(x + 142.0f, top + 66.0f, 22.0f, 404.0f);
        label(g, "GATE", gateMeter.withHeight(20.0f).translated(0.0f, -34.0f).expanded(20.0f, 0.0f), 14.0f);
        label(g, "GR", grMeter.withHeight(20.0f).translated(0.0f, -34.0f).expanded(20.0f, 0.0f), 14.0f);
        label(g, "DS", dsMeter.withHeight(20.0f).translated(0.0f, -34.0f).expanded(20.0f, 0.0f), 14.0f);

        verticalMeter(g, gateMeter, (gateLevel + 84.0f) / 84.0f, false, gateOpen > 0.5f ? pink : textDim);
        verticalMeter(g, grMeter, compReduction / 30.0f, true, pink);
        verticalMeter(g, dsMeter, deessReduction / 15.0f, true, pink);
        scale(g, gateMeter.translated(-2.0f, 0.0f), { "0", "-12", "-24", "-36", "-48", "-60", "-72", "-84" }, false);
        scale(g, dsMeter, { "0", "-3", "-6", "-9", "-12", "-15" }, false);

        // Gate handle, drawn on the meter it controls.
        const auto gate = value(state, "gate");
        const auto hy = gateMeter.getY() + (1.0f - (gate + 84.0f) / 84.0f) * gateMeter.getHeight();
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.fillEllipse(juce::Rectangle<float>(30.0f, 30.0f).withCentre({ gateMeter.getCentreX(), hy + 1.5f }));
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff4a4f59), gateMeter.getCentreX(), hy - 14.0f,
                                               juce::Colour(0xff1b1e24), gateMeter.getCentreX(), hy + 14.0f, false));
        g.fillEllipse(juce::Rectangle<float>(28.0f, 28.0f).withCentre({ gateMeter.getCentreX(), hy }));
        g.setColour(pink);
        g.fillEllipse(juce::Rectangle<float>(8.0f, 8.0f).withCentre({ gateMeter.getCentreX(), hy }));
        g.setColour(textDim);
        g.setFont(uiFont(12.0f, true));
        g.drawText(gate <= -79.5f ? juce::String("OFF") : juce::String(juce::roundToInt(gate)),
                   juce::Rectangle<float>(gateMeter.getX() - 20.0f, gateMeter.getBottom() + 8.0f, 62.0f, 16.0f), juce::Justification::centred, false);

        label(g, "COMP  " + juce::String(juce::roundToInt(value(state, "comp"))) + " %",
              juce::Rectangle<float>(x + 186.0f, top + 180.0f, 160.0f, 24.0f), 16.0f);
        label(g, "DE-ESS  " + juce::String(juce::roundToInt(value(state, "deess"))) + " %",
              juce::Rectangle<float>(x + 186.0f, top + 418.0f, 160.0f, 24.0f), 16.0f);
    }

    void paintTone(juce::Graphics& g)
    {
        const auto x = columnX(1);
        const auto top = panelTop;
        const auto screen = juce::Rectangle<float>(x + 18.0f, top + 18.0f, columnWidth - 36.0f, 160.0f);

        g.setColour(juce::Colour(0xff0b1119));
        g.fillRoundedRectangle(screen, 10.0f);
        g.setColour(cyan.withAlpha(0.35f));
        g.drawRoundedRectangle(screen.reduced(0.6f), 10.0f, 1.2f);
        const auto plot = screen.reduced(10.0f, 14.0f).withTrimmedBottom(10.0f);
        const auto xFor = [plot](double f) { return plot.getX() + static_cast<float>(std::log(f / 20.0) / std::log(1000.0)) * plot.getWidth(); };
        const auto fFor = [plot](float px) { return 20.0 * std::pow(1000.0, (px - plot.getX()) / plot.getWidth()); };
        const auto yFor = [plot](float db) { return juce::jmap(juce::jlimit(-15.0f, 15.0f, db), 15.0f, -15.0f, plot.getY(), plot.getBottom()); };

        g.setColour(juce::Colour(0xff17202b));
        for (const auto f : { 100.0, 1000.0, 10000.0 }) g.drawVerticalLine(juce::roundToInt(xFor(f)), plot.getY(), plot.getBottom());
        for (const auto db : { -12.0f, 0.0f, 12.0f }) g.drawHorizontalLine(juce::roundToInt(yFor(db)), plot.getX(), plot.getRight());

        // The music, faint behind the curve.
        juce::Path music;
        for (auto px = plot.getX(); px <= plot.getRight(); px += 2.0f)
        {
            const auto y = juce::jmap(juce::jlimit(-84.0f, 0.0f, spectrumAt(fFor(px))), 0.0f, -84.0f, plot.getY(), plot.getBottom());
            if (px == plot.getX()) music.startNewSubPath(px, y); else music.lineTo(px, y);
        }
        auto musicFill = music;
        musicFill.lineTo(plot.getRight(), plot.getBottom());
        musicFill.lineTo(plot.getX(), plot.getBottom());
        musicFill.closeSubPath();
        g.setColour(cyan.withAlpha(0.10f));
        g.fillPath(musicFill);
        g.setColour(cyan.withAlpha(0.3f));
        g.strokePath(music, juce::PathStrokeType(1.0f));

        // The tone curve, computed from the same filters the engine uses.
        const auto style = juce::roundToInt(value(state, "tone_style"));
        const auto& voice = toneVoice(style);
        constexpr double displayRate = 96000.0;
        std::array<Biquad, 6> filters;
        filters[0].highPass(displayRate, juce::jmax(value(state, "hp"), voice.hpMin));
        filters[1].lowShelf(displayRate, voice.low, value(state, "low"));
        filters[2].peak(displayRate, voice.mid, 0.8, value(state, "mid"));
        filters[3].peak(displayRate, voice.high, 0.9, value(state, "high"));
        filters[4].highShelf(displayRate, voice.air, value(state, "air"));
        filters[5].lowPass(displayRate, juce::jmin(value(state, "lp"), voice.lpMax));
        juce::Path curve;
        for (auto px = plot.getX(); px <= plot.getRight(); px += 2.0f)
        {
            double magnitude = 1.0;
            for (const auto& f : filters) magnitude *= f.magnitude(displayRate, fFor(px));
            const auto y = yFor(static_cast<float>(juce::Decibels::gainToDecibels(magnitude, -40.0)));
            if (px == plot.getX()) curve.startNewSubPath(px, y); else curve.lineTo(px, y);
        }
        neon(g, curve, cyan, 2.2f, moduleOn(1) ? 1.0f : 0.3f);

        const std::array<std::pair<float, float>, 4> nodes { { { voice.low, value(state, "low") }, { voice.mid, value(state, "mid") },
                                                               { voice.high, value(state, "high") }, { voice.air, value(state, "air") } } };
        for (const auto& [f, db] : nodes)
        {
            g.setColour(juce::Colours::white);
            g.fillEllipse(juce::Rectangle<float>(10.0f, 10.0f).withCentre({ xFor(f), yFor(db) }));
        }

        g.setColour(cyan.withAlpha(0.9f));
        g.setFont(uiFont(11.5f, true, 0.2f));
        g.drawText("ANALYSER", juce::Rectangle<float>(screen.getX() + 12.0f, screen.getBottom() - 22.0f, 100.0f, 16.0f), juce::Justification::centredLeft, false);
        g.setColour(textDim);
        g.setFont(uiFont(11.0f));
        for (const auto& [f, text] : std::initializer_list<std::pair<double, const char*>> { { 100.0, "100" }, { 1000.0, "1k" }, { 10000.0, "10k" } })
            g.drawText(text, juce::Rectangle<float>(xFor(f) - 20.0f, screen.getBottom() - 22.0f, 40.0f, 16.0f), juce::Justification::centred, false);
        g.drawText("+12", juce::Rectangle<float>(screen.getX() + 10.0f, yFor(12.0f) - 8.0f, 40.0f, 16.0f), juce::Justification::centredLeft, false);
        g.drawText("-12", juce::Rectangle<float>(screen.getX() + 10.0f, yFor(-12.0f) - 8.0f, 40.0f, 16.0f), juce::Justification::centredLeft, false);

        const char* names[] { "LOW", "MID", "HIGH", "AIR+" };
        const char* ids[] { "low", "mid", "high", "air" };
        for (int i = 0; i < 4; ++i)
        {
            const auto cx = x + 60.0f + static_cast<float>(i) * 80.0f;
            label(g, names[i], juce::Rectangle<float>(cx - 40.0f, top + 188.0f, 80.0f, 22.0f), 15.0f);
            g.setColour(textDim);
            g.setFont(uiFont(13.0f));
            g.drawText(signedDb(value(state, ids[i])), juce::Rectangle<float>(cx - 40.0f, top + 414.0f, 80.0f, 18.0f), juce::Justification::centred, false);
        }

        // Cut icons either side of the range slider, and its values.
        const auto rowY = top + 479.0f;
        juce::Path lowCut, highCut;
        lowCut.startNewSubPath(x + 22.0f, rowY + 8.0f);
        lowCut.quadraticTo(x + 30.0f, rowY - 8.0f, x + 44.0f, rowY - 8.0f);
        highCut.startNewSubPath(x + columnWidth - 44.0f, rowY - 8.0f);
        highCut.quadraticTo(x + columnWidth - 30.0f, rowY - 8.0f, x + columnWidth - 22.0f, rowY + 8.0f);
        g.setColour(textMain);
        g.strokePath(lowCut, juce::PathStrokeType(2.0f));
        g.strokePath(highCut, juce::PathStrokeType(2.0f));
        g.setColour(textDim);
        g.setFont(uiFont(12.0f));
        g.drawText("LOW CUT " + juce::String(juce::roundToInt(value(state, "hp"))) + " Hz",
                   juce::Rectangle<float>(x + 20.0f, top + 440.0f, 160.0f, 16.0f), juce::Justification::centredLeft, false);
        g.drawText("HIGH CUT " + decimals(value(state, "lp") / 1000.0f, 1) + " kHz",
                   juce::Rectangle<float>(x + columnWidth - 180.0f, top + 440.0f, 160.0f, 16.0f), juce::Justification::centredRight, false);
    }

    void paintSpace(juce::Graphics& g)
    {
        const auto x = columnX(2);
        const auto top = panelTop;
        const auto box = juce::Rectangle<float>(x + 18.0f, top + 18.0f, columnWidth - 36.0f, 176.0f);
        g.setColour(juce::Colour(0xff0d0f18));
        g.fillRoundedRectangle(box, 10.0f);
        g.setColour(violet.withAlpha(0.35f));
        g.drawRoundedRectangle(box.reduced(0.6f), 10.0f, 1.2f);
        g.drawVerticalLine(juce::roundToInt(box.getCentreX()), box.getY() + 10.0f, box.getY() + 122.0f);

        auto group = [&](float gx, const juce::String& title, float send, float duck, float out)
        {
            g.setColour(violet.brighter(0.2f));
            g.setFont(uiFont(13.0f, true, 0.15f));
            g.drawText(title, juce::Rectangle<float>(gx, box.getY() + 8.0f, 150.0f, 18.0f), juce::Justification::centredLeft, false);
            const std::array<std::pair<const char*, float>, 3> bars { { { "IN", send * 4.0f }, { "DUCK", duck }, { "OUT", out * 4.0f } } };
            for (size_t i = 0; i < bars.size(); ++i)
            {
                const auto bx = gx + 8.0f + static_cast<float>(i) * 48.0f;
                const auto meter = juce::Rectangle<float>(bx, box.getY() + 32.0f, 14.0f, 70.0f);
                verticalMeter(g, meter, bars[i].second, i == 1, i == 2 ? cyan : violet);
                g.setColour(textDim);
                g.setFont(uiFont(10.0f, true));
                g.drawText(bars[i].first, juce::Rectangle<float>(bx - 16.0f, box.getY() + 104.0f, 46.0f, 14.0f), juce::Justification::centred, false);
            }
        };
        group(box.getX() + 10.0f, "REVERB", verbSend, verbDuck, verbOut);
        group(box.getCentreX() + 10.0f, "DELAY", delaySend, delayDuck, delayOut);

        label(g, "REVERB  " + juce::String(juce::roundToInt(value(state, "reverb"))) + " %",
              juce::Rectangle<float>(x + 20.0f, top + 344.0f, 160.0f, 24.0f), 15.0f);
        label(g, "DELAY  " + juce::String(juce::roundToInt(value(state, "delay"))) + " %",
              juce::Rectangle<float>(x + 184.0f, top + 344.0f, 160.0f, 24.0f), 15.0f);

        const auto rowY = top + 479.0f;
        g.setColour(textDim);
        g.setFont(uiFont(12.0f, true, 0.1f));
        g.drawText("DARK", juce::Rectangle<float>(x + 14.0f, rowY - 8.0f, 44.0f, 16.0f), juce::Justification::centred, false);
        g.drawText("BRIGHT", juce::Rectangle<float>(x + columnWidth - 62.0f, rowY - 8.0f, 54.0f, 16.0f), juce::Justification::centred, false);
        g.drawText("SPACE TONE", juce::Rectangle<float>(x, top + 440.0f, 120.0f, 16.0f).translated(20.0f, 0.0f), juce::Justification::centredLeft, false);
    }

    void paintFx(juce::Graphics& g)
    {
        const auto x = columnX(3);
        const auto top = panelTop;
        const auto on = moduleOn(3);

        // Corner lights that flicker with the effect.
        for (int corner = 0; corner < 2; ++corner)
            for (int row = 0; row < 3; ++row)
                for (int col = 0; col < 4; ++col)
                {
                    const auto cx = (corner == 0 ? x + 18.0f : x + columnWidth - 54.0f) + static_cast<float>(col) * 10.0f;
                    const auto cy = top + 18.0f + static_cast<float>(row) * 10.0f;
                    const auto flicker = 0.5f + 0.5f * std::sin(clock * 6.0f + static_cast<float>(row * 3 + col + corner * 7));
                    const auto lit = on ? juce::jlimit(0.12f, 1.0f, fxActivity * 12.0f * flicker + 0.12f) : 0.1f;
                    g.setColour(amber.withAlpha(lit));
                    g.fillEllipse(juce::Rectangle<float>(4.0f, 4.0f).withCentre({ cx, cy }));
                }

        const auto style = juce::jlimit(0, 5, juce::roundToInt(value(state, "fx_style")));
        label(g, fxStyles()[style].toUpperCase() + "  " + juce::String(juce::roundToInt(value(state, "fx_amount"))) + " %",
              juce::Rectangle<float>(x, top + 282.0f, columnWidth, 30.0f), 22.0f);

        const auto routing = routingArea.getBounds().toFloat();
        g.setColour(juce::Colour(0xff0e1117));
        g.fillRoundedRectangle(routing, 10.0f);
        g.setColour(routingArea.isMouseOver() ? amber.withAlpha(0.6f) : juce::Colour(0xff2b313b));
        g.drawRoundedRectangle(routing.reduced(0.6f), 10.0f, 1.2f);
        label(g, "FX ROUTING", routing.withHeight(30.0f).translated(0.0f, 8.0f), 14.0f);

        const auto post = value(state, "fx_post") > 0.5f;
        const auto chainY = routing.getY() + 62.0f;
        const std::array<std::pair<const char*, Colour>, 3> chain = post
            ? std::array<std::pair<const char*, Colour>, 3> { { { "TONE", cyan }, { "SPACE", violet }, { "FX", amber } } }
            : std::array<std::pair<const char*, Colour>, 3> { { { "TONE", cyan }, { "FX", amber }, { "SPACE", violet } } };
        for (size_t i = 0; i < chain.size(); ++i)
        {
            const auto cx = routing.getX() + 48.0f + static_cast<float>(i) * 94.0f;
            g.setColour(chain[i].second);
            g.fillEllipse(juce::Rectangle<float>(8.0f, 8.0f).withCentre({ cx - 30.0f, chainY }));
            g.setColour(textMain);
            g.setFont(uiFont(13.0f, true, 0.1f));
            g.drawText(chain[i].first, juce::Rectangle<float>(cx - 22.0f, chainY - 9.0f, 60.0f, 18.0f), juce::Justification::centredLeft, false);
            if (i < 2)
            {
                g.setColour(textDim);
                g.drawText(juce::String(juce::CharPointer_UTF8("\xe2\x86\x92")),
                           juce::Rectangle<float>(cx + 30.0f, chainY - 9.0f, 20.0f, 18.0f), juce::Justification::centred, false);
            }
        }
        g.setColour(textDim);
        g.setFont(uiFont(11.0f));
        g.drawText("click to switch", routing.withTop(routing.getBottom() - 16.0f), juce::Justification::centred, false);
    }

    void dotMeter(juce::Graphics& g, juce::Rectangle<float> r, float levelDb) const
    {
        constexpr int dots = 44;
        const auto step = r.getWidth() / dots;
        for (int i = 0; i < dots; ++i)
        {
            const auto db = -60.0f + 66.0f * static_cast<float>(i) / static_cast<float>(dots - 1);
            const auto lit = levelDb >= db;
            const auto colour = db > -3.0f ? juce::Colour(0xffef4444) : db > -12.0f ? juce::Colour(0xfff5a524) : cyan;
            g.setColour(lit ? colour : juce::Colour(0xff222832));
            g.fillEllipse(juce::Rectangle<float>(5.0f, 5.0f).withCentre({ r.getX() + step * (static_cast<float>(i) + 0.5f), r.getCentreY() }));
        }
    }

    void paintFooter(juce::Graphics& g)
    {
        g.setColour(juce::Colour(0xff0c0f14));
        g.fillRect(0.0f, 896.0f, canvasWidth, 128.0f);
        g.setColour(juce::Colour(0xff1c2129));
        g.drawHorizontalLine(896, 0.0f, canvasWidth);

        g.setColour(textMain);
        g.setFont(uiFont(15.0f, false, 0.12f));
        g.drawText(signedDb(value(state, "in_gain")) + "  INPUT", juce::Rectangle<float>(96.0f, 966.0f, 190.0f, 18.0f), juce::Justification::centredLeft, false);
        dotMeter(g, juce::Rectangle<float>(290.0f, 966.0f, 330.0f, 18.0f), inLevel);

        g.setColour(textMain);
        g.setFont(uiFont(15.0f, false, 0.12f));
        g.drawText("OUTPUT  " + signedDb(value(state, "out_gain")), juce::Rectangle<float>(1250.0f, 966.0f, 190.0f, 18.0f), juce::Justification::centredRight, false);
        dotMeter(g, juce::Rectangle<float>(916.0f, 966.0f, 330.0f, 18.0f), outLevel);

        g.setColour(textDim);
        g.setFont(uiFont(14.0f, false, 0.5f));
        g.drawText("AMANORSAC.STUDIO", juce::Rectangle<float>(618.0f, 950.0f, 300.0f, 20.0f), juce::Justification::centred, false);
        g.setColour(textDim.withAlpha(0.6f));
        g.setFont(uiFont(11.0f, false, 0.7f));
        g.drawText("ZERO LATENCY VOCAL CHAIN", juce::Rectangle<float>(618.0f, 972.0f, 300.0f, 18.0f), juce::Justification::centred, false);
    }

    // ------------------------------------------------------------------ state
    VocalRackProcessor& processor;
    APVTS& state;
    Look look;

    IconButton undoButton { IconButton::Icon::undo }, redoButton { IconButton::Icon::redo }, saveButton { IconButton::Icon::save };
    ClickArea presetArea, routingArea;
    Chevron presetPrev { false }, presetNext { true };

    std::vector<std::unique_ptr<juce::Slider>> sliders;
    std::vector<std::unique_ptr<juce::Button>> buttons;
    std::vector<std::unique_ptr<Chevron>> cardButtons;
    std::vector<std::unique_ptr<APVTS::SliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<APVTS::ButtonAttachment>> buttonAttachments;
    std::unique_ptr<juce::Slider> filterRange;
    juce::Slider* gateSlider = nullptr;
    bool syncing = false;

    juce::dsp::FFT fft;
    juce::dsp::WindowingFunction<float> window;
    std::vector<float> spectrum, fftData, ring;
    int ringPosition = 0;

    float clock = 0.0f;
    float inLevel = -90.0f, outLevel = -90.0f, gateLevel = -90.0f, gateOpen = 1.0f;
    float compReduction = 0.0f, deessReduction = 0.0f;
    float verbSend = 0.0f, verbOut = 0.0f, verbDuck = 0.0f, delaySend = 0.0f, delayOut = 0.0f, delayDuck = 0.0f, fxActivity = 0.0f;
};

// ================================================================== editor
VocalRackEditor::VocalRackEditor(VocalRackProcessor& owner)
    : AudioProcessorEditor(owner), surface(std::make_unique<Surface>(owner))
{
    addAndMakeVisible(*surface);
    setResizable(true, true);
    setResizeLimits(768, 512, 1920, 1280);
    if (auto* limits = getConstrainer()) limits->setFixedAspectRatio(canvasWidth / canvasHeight);
    setSize(1152, 768);
    startTimerHz(30);
}

VocalRackEditor::~VocalRackEditor() { stopTimer(); }

void VocalRackEditor::paint(juce::Graphics& g) { g.fillAll(ground); }

void VocalRackEditor::resized()
{
    surface->setTransform(juce::AffineTransform::scale(static_cast<float>(getWidth()) / canvasWidth));
}

void VocalRackEditor::timerCallback() { surface->tick(); }
}
