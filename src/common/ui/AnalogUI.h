#pragma once

#include "common/state/PluginSpec.h"

#include <juce_gui_extra/juce_gui_extra.h>

/** Shared analog-series design system.

    Every widget draws procedurally in a fixed 1536x960 design space so the
    analog products stay resolution independent while matching the approved
    high-fidelity visual anchors. Colours come from a per-product Palette so a
    product keeps its own accent without forking the family chrome.
*/
namespace amanorsac::analog
{
struct Palette
{
    juce::Colour accent { 0xffe2571f };      // product accent (ticks, values, active states)
    juce::Colour accentGlow { 0xffff7a3c };  // brighter bloom for lit elements
    juce::Colour trim { 0xff9c2418 };        // large input/output trim knob face
    juce::Colour wood { 0xff4a3529 };        // outer chassis frame
    juce::Colour chassis { 0xff212429 };     // main panel face
    juce::Colour recess { 0xff15171a };      // recessed sub-panel
    juce::Colour bronze { 0xff7d5c34 };      // hairline borders
    juce::Colour cream { 0xffe6ded0 };       // pointers and primary text
    juce::Colour label { 0xffbfb6a6 };       // secondary text

    static Palette forProduct(const juce::String& pluginId);
};

/** Shared 1536x960 design canvas used by every analog product. */
constexpr float designWidth = 1536.0f;
constexpr float designHeight = 960.0f;

// ---------------------------------------------------------------- primitives

/** Tiled monochrome grain used to break up flat panel fills. */
const juce::Image& grainTexture();

void dropShadow(juce::Graphics&, juce::Rectangle<float> bounds, float corner, float depth);
void fillPanel(juce::Graphics&, juce::Rectangle<float> bounds, const Palette&,
               float corner = 10.0f, bool recessed = false);
void strokePanel(juce::Graphics&, juce::Rectangle<float> bounds, const Palette&,
                 float corner = 10.0f, float thickness = 1.2f, float alpha = 0.75f);
void drawScrew(juce::Graphics&, juce::Point<float> centre, float radius);
void drawPanelScrews(juce::Graphics&, juce::Rectangle<float> bounds, float inset = 13.0f,
                     float radius = 6.0f);
void drawWoodFrame(juce::Graphics&, juce::Rectangle<float> bounds, const Palette&);
void drawSectionCaption(juce::Graphics&, juce::Rectangle<float> row, const juce::String& text,
                        juce::Colour colour, float height = 12.0f);
void drawBrandMark(juce::Graphics&, juce::Rectangle<float> bounds, juce::Colour barColour,
                   juce::Colour textColour);
void drawHexEmblem(juce::Graphics&, juce::Rectangle<float> bounds, juce::Colour colour);

/** Condensed uppercase face used for every label in the analog family. */
juce::Font labelFont(float height, bool bold = true, float squeeze = 0.92f);
/** Wide tracked serif for engraved product names. */
juce::Font displayFont(float height);

// ------------------------------------------------------------- look and feel

/** Knob rendering that matches the anchor: notched rim, illuminated tick ring,
    cream chamfered pointer and an inset value readout. */
class AnalogLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    explicit AnalogLookAndFeel(Palette);

    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height, float position,
                          float startAngle, float endAngle, juce::Slider&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&,
                              bool isMouseOverButton, bool isButtonDown) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool isMouseOverButton,
                        bool isButtonDown) override;
    void drawLabel(juce::Graphics&, juce::Label&) override;
    juce::Label* createSliderTextBox(juce::Slider&) override;
    void drawPopupMenuBackground(juce::Graphics&, int width, int height) override;
    juce::Font getPopupMenuFont() override;

    /** Larger faces (trim knobs) use the red trim colour and a numeric scale ring. */
    void setTrimStyle(bool shouldUseTrimStyle) noexcept { trimStyle = shouldUseTrimStyle; }

private:
    Palette palette;
    bool trimStyle = false;
};

// ----------------------------------------------------------------- widgets

/** Slider with shift-fine wheel and shift-fine drag. */
class AnalogSlider final : public juce::Slider
{
public:
    AnalogSlider();
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
};

/** Label + knob + inset value readout + unit, as used across the knob rows. */
class KnobCell final : public juce::Component
{
public:
    KnobCell(juce::AudioProcessorValueTreeState&, const ParameterDescriptor&, const Palette&,
             AnalogLookAndFeel&, bool showCaption = true);
    ~KnobCell() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    /** Optional min/max legend drawn under the knob instead of a value box. */
    void setEndLegend(const juce::String& low, const juce::String& high);

private:
    float captionHeight() const;
    float unitHeight() const;
    float valueHeight() const;
    juce::Rectangle<float> valueBounds() const;

    Palette palette;
    juce::String caption;
    juce::String unit;
    juce::String lowLegend, highLegend;
    bool useLegend = false;
    AnalogSlider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KnobCell)
};

/** Large red input/output trim with a numeric dB scale ring. */
class TrimKnob final : public juce::Component
{
public:
    TrimKnob(juce::AudioProcessorValueTreeState&, const ParameterDescriptor&, const Palette&,
             juce::String caption, juce::String footer);
    ~TrimKnob() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    Palette palette;
    juce::String caption, footer;
    AnalogLookAndFeel look;
    AnalogSlider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrimKnob)
};

/** Segmented button row/column bound to a choice parameter (A/B/C, PAD, LINK). */
class SegmentGroup final : public juce::Component
{
public:
    SegmentGroup(juce::AudioProcessorValueTreeState&, const ParameterDescriptor&, const Palette&,
                 juce::String caption, int columns);
    ~SegmentGroup() override;

    /** Replaces the printed button text without touching the parameter values,
        so contract ids such as iron_a can display as A on the faceplate. */
    void setOptionLabels(const juce::StringArray&);

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void refresh();

    Palette palette;
    juce::String caption;
    int columns;
    juce::StringArray options;
    juce::OwnedArray<juce::TextButton> buttons;
    juce::AudioParameterChoice* parameter = nullptr;
    juce::AudioProcessorValueTreeState& state;
    juce::String parameterId;
    std::unique_ptr<juce::ParameterAttachment> sync;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SegmentGroup)
};

/** Momentary-look latching button with an integrated lamp (PHASE, HPF). */
class LampButton final : public juce::Component
{
public:
    LampButton(juce::AudioProcessorValueTreeState&, const ParameterDescriptor&, const Palette&,
               juce::String glyph, juce::String caption, bool squareLamp = false);
    ~LampButton() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    Palette palette;
    juce::String glyph, caption;
    bool squareLamp;
    juce::ToggleButton hidden;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LampButton)
};

/** Cream-faced ballistic VU with branded scale plate. */
class VuMeter final : public juce::Component, private juce::Timer
{
public:
    VuMeter(std::function<float()> levelSource, juce::String scaleCaption = "LEVEL");

    void paint(juce::Graphics&) override;

private:
    void timerCallback() override;

    std::function<float()> source;
    juce::String scaleCaption;
    float needle = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VuMeter)
};

/** Vertical LED ladder pair with a shared dB legend. */
class LedBargraph final : public juce::Component, private juce::Timer
{
public:
    LedBargraph(std::function<float()> leftSource, std::function<float()> rightSource,
                const Palette&);

    void paint(juce::Graphics&) override;

private:
    void timerCallback() override;

    std::function<float()> left, right;
    Palette palette;
    float leftLevel = 0.0f, rightLevel = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LedBargraph)
};

/** Branding, preset navigation and session controls across the top of a product. */
class TopBar final : public juce::Component
{
public:
    TopBar(const Palette&, juce::String presetName);
    ~TopBar() override;

    /** Products that print their name in the bar (A02) supply it here; those
        that print it on the chassis (A10) leave it empty. */
    void setProductTitle(juce::String title, juce::String subtitle);

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    Palette palette;
    AnalogLookAndFeel look;
    juce::String presetName;
    juce::String productTitle, productSubtitle;
    juce::Rectangle<int> presetBounds;
    juce::OwnedArray<juce::TextButton> actions;
    juce::TextButton previousPreset { "<" }, nextPreset { ">" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TopBar)
};

/** Interaction help strip along the bottom edge. */
class FooterStrip final : public juce::Component
{
public:
    struct Hint { juce::String glyph, title, detail; };

    FooterStrip(const Palette&, std::vector<Hint>);

    void paint(juce::Graphics&) override;

private:
    Palette palette;
    std::vector<Hint> hints;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FooterStrip)
};
}
