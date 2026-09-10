#include "AnalogProductEditor.h"

#include <array>
#include <cmath>
#include <map>

namespace amanorsac
{
namespace
{
constexpr auto baseWidth = 1280.0f;
constexpr auto baseHeight = 800.0f;
constexpr auto dark = 0xff0c0b09;
constexpr auto panel = 0xff171815;
constexpr auto panelRaised = 0xff23231e;
constexpr auto brass = 0xffbb843c;
constexpr auto cream = 0xffe7d9b8;

struct Slot
{
    juce::String id;
    juce::Rectangle<float> bounds;
    juce::Colour colour;
};

struct Section
{
    juce::String name;
    juce::Rectangle<float> bounds;
};

void drawSoftShadow(juce::Graphics& graphics, juce::Rectangle<float> bounds, float cornerRadius,
                    float offsetY = 4.0f, float strength = 0.34f)
{
    for (int layer = 7; layer >= 1; --layer)
    {
        const auto spread = static_cast<float>(layer) * 1.25f;
        graphics.setColour(juce::Colours::black.withAlpha(strength * (8.0f - static_cast<float>(layer)) / 32.0f));
        graphics.fillRoundedRectangle(bounds.expanded(spread).translated(0.0f, offsetY), cornerRadius + spread);
    }
}

void drawScrew(juce::Graphics& graphics, juce::Point<float> centre, float radius = 6.0f)
{
    graphics.setColour(juce::Colours::black.withAlpha(0.72f));
    graphics.fillEllipse(juce::Rectangle<float>(radius * 2.0f, radius * 2.0f)
                             .withCentre(centre.translated(0.0f, 1.5f)).expanded(1.5f));
    juce::ColourGradient metal(juce::Colour(0xff8e8068), centre.x - radius, centre.y - radius,
                               juce::Colour(0xff181510), centre.x + radius, centre.y + radius, false);
    metal.addColour(0.42, juce::Colour(0xff4b4235));
    graphics.setGradientFill(metal);
    graphics.fillEllipse(juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(centre));
    graphics.setColour(juce::Colour(0xffb5a58b).withAlpha(0.42f));
    graphics.drawEllipse(juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(centre).reduced(0.7f), 0.8f);
    graphics.setColour(juce::Colour(0xff15120f));
    graphics.drawLine(centre.x - radius * 0.52f, centre.y - radius * 0.18f,
                      centre.x + radius * 0.52f, centre.y + radius * 0.18f, 1.35f);
}

void drawFineTexture(juce::Graphics& graphics, juce::Rectangle<float> bounds, juce::Colour tint)
{
    graphics.saveState();
    graphics.reduceClipRegion(bounds.toNearestInt());
    for (int y = static_cast<int>(bounds.getY()); y < static_cast<int>(bounds.getBottom()); y += 3)
    {
        const auto wave = 0.5f + 0.5f * std::sin(static_cast<float>(y) * 0.73f);
        graphics.setColour(tint.withAlpha(0.010f + wave * 0.018f));
        graphics.drawHorizontalLine(y, bounds.getX(), bounds.getRight());
    }
    for (int x = static_cast<int>(bounds.getX()) + 2; x < static_cast<int>(bounds.getRight()); x += 11)
    {
        const auto grain = 0.5f + 0.5f * std::sin(static_cast<float>(x) * 1.91f);
        graphics.setColour(juce::Colours::white.withAlpha(0.006f + grain * 0.012f));
        graphics.drawVerticalLine(x, bounds.getY(), bounds.getBottom());
    }
    graphics.restoreState();
}

const ParameterDescriptor* findDescriptor(const PluginSpec& spec, const juce::String& id)
{
    for (const auto& descriptor : spec.parameters)
        if (descriptor.id == id)
            return &descriptor;
    return nullptr;
}

class HardwareSlider final : public juce::Slider
{
public:
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override
    {
        auto adjusted = wheel;
        if (event.mods.isShiftDown()) adjusted.deltaY *= 0.1f;
        Slider::mouseWheelMove(event, adjusted);
    }
};

class HardwareLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    explicit HardwareLookAndFeel(juce::Colour highlight) : accent(highlight) {}

    void drawRotarySlider(juce::Graphics& graphics, int x, int y, int width, int height,
                          float position, float startAngle, float endAngle, juce::Slider& slider) override
    {
        auto area = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                           static_cast<float>(width), static_cast<float>(height)).reduced(11.0f);
        const auto diameter = juce::jmin(area.getWidth(), area.getHeight());
        auto knob = juce::Rectangle<float>(diameter, diameter).withCentre(area.getCentre());
        const auto centre = knob.getCentre();
        const auto radius = diameter * 0.5f;
        const auto angle = startAngle + position * (endAngle - startAngle);
        const auto faceColour = slider.findColour(juce::Slider::rotarySliderFillColourId);

        for (int halo = 8; halo >= 1; --halo)
        {
            graphics.setColour(juce::Colours::black.withAlpha((9.0f - static_cast<float>(halo)) * 0.012f));
            graphics.fillEllipse(knob.expanded(static_cast<float>(halo)).translated(0.0f, 5.0f));
        }

        for (int tick = 0; tick <= 20; ++tick)
        {
            const auto a = startAngle + static_cast<float>(tick) / 20.0f * (endAngle - startAngle);
            const auto major = tick % 5 == 0;
            const auto inner = centre + juce::Point<float>(std::sin(a), -std::cos(a)) * (radius + (major ? 0.5f : 2.0f));
            const auto outer = centre + juce::Point<float>(std::sin(a), -std::cos(a)) * (radius + (major ? 8.0f : 6.0f));
            graphics.setColour((major ? accent : juce::Colour(brass)).withAlpha(major ? 0.94f : 0.55f));
            graphics.drawLine({ inner, outer }, major ? 1.55f : 0.85f);
        }

        juce::ColourGradient rim(juce::Colour(0xffb09a72), knob.getX(), knob.getY(),
                                 juce::Colour(0xff17130e), knob.getRight(), knob.getBottom(), false);
        rim.addColour(0.32, juce::Colour(0xff655842));
        rim.addColour(0.72, juce::Colour(0xff2b251c));
        graphics.setGradientFill(rim);
        graphics.fillEllipse(knob);

        auto knurl = knob.reduced(radius * 0.065f);
        for (int tooth = 0; tooth < 36; ++tooth)
        {
            const auto a = static_cast<float>(tooth) * juce::MathConstants<float>::twoPi / 36.0f;
            const auto p1 = centre + juce::Point<float>(std::cos(a), std::sin(a)) * (radius * 0.89f);
            const auto p2 = centre + juce::Point<float>(std::cos(a), std::sin(a)) * (radius * 0.98f);
            graphics.setColour((tooth < 14 ? juce::Colour(0xffc8b48b) : juce::Colour(0xff0b0907)).withAlpha(0.55f));
            graphics.drawLine({ p1, p2 }, 1.0f);
        }
        graphics.setColour(juce::Colours::black.withAlpha(0.72f));
        graphics.drawEllipse(knurl, 1.5f);

        const auto face = knob.reduced(radius * 0.19f);
        juce::ColourGradient fill(faceColour.brighter(0.22f), face.getX() + face.getWidth() * 0.25f,
                                  face.getY() + face.getHeight() * 0.20f,
                                  faceColour.darker(0.78f), face.getRight(), face.getBottom(), true);
        fill.addColour(0.56, faceColour.darker(0.18f));
        graphics.setGradientFill(fill);
        graphics.fillEllipse(face);
        graphics.setColour(juce::Colours::white.withAlpha(0.11f));
        graphics.drawEllipse(face.reduced(1.0f), 1.2f);
        graphics.setColour(juce::Colours::black.withAlpha(0.76f));
        graphics.drawEllipse(face, 1.7f);

        juce::Path highlight;
        highlight.addArc(face.getX() + 4.0f, face.getY() + 4.0f, face.getWidth() - 8.0f, face.getHeight() - 8.0f,
                         juce::MathConstants<float>::pi * 1.12f, juce::MathConstants<float>::pi * 1.83f, true);
        graphics.setColour(juce::Colours::white.withAlpha(0.14f));
        graphics.strokePath(highlight, juce::PathStrokeType(1.35f));

        const auto start = centre + juce::Point<float>(std::sin(angle), -std::cos(angle)) * 3.0f;
        const auto end = centre + juce::Point<float>(std::sin(angle), -std::cos(angle)) * (radius * 0.62f);
        graphics.setColour(juce::Colours::black.withAlpha(0.72f));
        graphics.drawLine({ start.translated(1.2f, 1.6f), end.translated(1.2f, 1.6f) }, 3.8f);
        graphics.setColour(juce::Colour(cream).interpolatedWith(accent, 0.16f));
        graphics.drawLine({ start, end }, 2.35f);
        juce::ColourGradient cap(juce::Colour(0xff4c4437), centre.x - 4.0f, centre.y - 4.0f,
                                 juce::Colour(0xff090806), centre.x + 4.0f, centre.y + 4.0f, false);
        graphics.setGradientFill(cap);
        graphics.fillEllipse(centre.x - 4.0f, centre.y - 4.0f, 8.0f, 8.0f);
    }

    void drawToggleButton(juce::Graphics& graphics, juce::ToggleButton& button,
                          bool highlighted, bool down) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(3.0f);
        drawSoftShadow(graphics, bounds, 3.0f, 2.0f, 0.26f);
        juce::ColourGradient body(down ? juce::Colour(0xff090806) : juce::Colour(0xff2b2822),
                                  0.0f, bounds.getY(), juce::Colour(0xff080705), 0.0f, bounds.getBottom(), false);
        graphics.setGradientFill(body);
        graphics.fillRoundedRectangle(bounds, 3.0f);
        graphics.setColour(juce::Colours::white.withAlpha(highlighted ? 0.15f : 0.07f));
        graphics.drawLine(bounds.getX() + 3.0f, bounds.getY() + 1.0f, bounds.getRight() - 3.0f, bounds.getY() + 1.0f, 1.0f);
        graphics.setColour(accent.withAlpha(highlighted ? 0.95f : 0.46f));
        graphics.drawRoundedRectangle(bounds, 3.0f, down ? 1.6f : 0.85f);
        const auto lamp = juce::Rectangle<float>(15.0f, 15.0f).withCentre({ bounds.getX() + 17.0f, bounds.getCentreY() });
        const auto on = button.getToggleState();
        if (on)
        {
            juce::ColourGradient glow(accent.withAlpha(0.46f), lamp.getCentreX(), lamp.getCentreY(),
                                      accent.withAlpha(0.0f), lamp.getCentreX() + 13.0f, lamp.getCentreY(), true);
            graphics.setGradientFill(glow);
            graphics.fillEllipse(lamp.expanded(11.0f));
        }
        graphics.setColour(on ? accent.brighter(0.34f) : juce::Colour(0xff493326));
        graphics.fillEllipse(lamp);
        graphics.setColour(juce::Colours::white.withAlpha(on ? 0.60f : 0.16f));
        graphics.fillEllipse(lamp.reduced(4.0f).translated(-2.0f, -2.0f));
        graphics.setColour(juce::Colour(cream));
        graphics.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        graphics.drawText(button.getButtonText(), 32, 0, button.getWidth() - 36, button.getHeight(),
                          juce::Justification::centredLeft);
    }

    void drawComboBox(juce::Graphics& graphics, int width, int height, bool,
                      int, int, int, int, juce::ComboBox&) override
    {
        auto bounds = juce::Rectangle<float>(0, 0, static_cast<float>(width), static_cast<float>(height)).reduced(1.0f);
        drawSoftShadow(graphics, bounds, 3.0f, 2.0f, 0.22f);
        juce::ColourGradient fill(juce::Colour(0xff343028), 0, 0,
                                  juce::Colour(0xff080806), 0, static_cast<float>(height), false);
        fill.addColour(0.38, juce::Colour(0xff1b1915));
        graphics.setGradientFill(fill);
        graphics.fillRoundedRectangle(bounds, 3.0f);
        graphics.setColour(accent.withAlpha(0.68f));
        graphics.drawRoundedRectangle(bounds, 3.0f, 1.0f);
        juce::Path arrow;
        arrow.addTriangle(static_cast<float>(width - 19), height * 0.42f,
                          static_cast<float>(width - 9), height * 0.42f,
                          static_cast<float>(width - 14), height * 0.62f);
        graphics.setColour(accent);
        graphics.fillPath(arrow);
    }

private:
    juce::Colour accent;
};

class HardwareControl final : public juce::Component
{
public:
    HardwareControl(juce::AudioProcessorValueTreeState& state,
                    const ParameterDescriptor& parameter,
                    juce::Colour face,
                    HardwareLookAndFeel& look)
        : descriptor(parameter)
    {
        setLookAndFeel(&look);
        title.setText(descriptor.name.toUpperCase(), juce::dontSendNotification);
        title.setJustificationType(juce::Justification::centred);
        title.setFont(juce::FontOptions("Bahnschrift", 11.0f, juce::Font::bold));
        title.setColour(juce::Label::textColourId, juce::Colour(cream).interpolatedWith(face.brighter(0.5f), 0.20f));
        addAndMakeVisible(title);

        if (descriptor.kind == ParameterDescriptor::Kind::boolean)
        {
            toggle.setButtonText(descriptor.name.toUpperCase());
            addAndMakeVisible(toggle);
            buttonAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(state, descriptor.id, toggle);
        }
        else if (descriptor.kind == ParameterDescriptor::Kind::choice)
        {
            choice.addItemList(descriptor.choices, 1);
            choice.setJustificationType(juce::Justification::centred);
            choice.setColour(juce::ComboBox::textColourId, juce::Colour(cream));
            addAndMakeVisible(choice);
            choiceAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(state, descriptor.id, choice);
        }
        else
        {
            slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            slider.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f,
                                       juce::MathConstants<float>::pi * 2.75f, true);
            slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 92, 19);
            slider.setDoubleClickReturnValue(true, descriptor.defaultValue);
            slider.setColour(juce::Slider::rotarySliderFillColourId, face);
            slider.setColour(juce::Slider::textBoxTextColourId, juce::Colour(cream));
            slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff080806));
            slider.setColour(juce::Slider::textBoxOutlineColourId, face.withAlpha(0.42f));
            slider.setTextValueSuffix(descriptor.unit.isEmpty() ? juce::String() : " " + descriptor.unit);
            addAndMakeVisible(slider);
            sliderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state, descriptor.id, slider);
        }
        setTitle(descriptor.name);
        setDescription(descriptor.rangeText + "; double-click restores " + descriptor.defaultText);
    }

    ~HardwareControl() override { setLookAndFeel(nullptr); }

    void paint(juce::Graphics& graphics) override
    {
        const auto edge = static_cast<float>(getWidth() - 1);
        graphics.setColour(juce::Colours::black.withAlpha(0.25f));
        graphics.drawVerticalLine(static_cast<int>(edge), 9.0f, static_cast<float>(getHeight() - 8));
        graphics.setColour(juce::Colour(brass).withAlpha(0.10f));
        graphics.drawVerticalLine(static_cast<int>(edge - 1.0f), 9.0f, static_cast<float>(getHeight() - 8));
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(4, 3);
        title.setBounds(area.removeFromTop(18));
        if (toggle.isVisible())
        {
            title.setVisible(false);
            toggle.setBounds(area.withSizeKeepingCentre(juce::jmin(134, area.getWidth()), 42));
        }
        if (choice.isVisible()) choice.setBounds(area.withSizeKeepingCentre(juce::jmin(145, area.getWidth()), 40));
        if (slider.isVisible()) slider.setBounds(area);
    }

private:
    const ParameterDescriptor descriptor;
    juce::Label title;
    HardwareSlider slider;
    juce::ToggleButton toggle;
    juce::ComboBox choice;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> choiceAttachment;
};

class ProductVisual final : public juce::Component, private juce::Timer
{
public:
    ProductVisual(PluginProcessor& owner, juce::Colour highlight)
        : processor(owner), accent(highlight), productId(owner.spec.id)
    {
        startTimerHz(30);
    }

    void paint(juce::Graphics& graphics) override
    {
        auto bounds = getLocalBounds().toFloat().reduced(5.0f);
        drawSoftShadow(graphics, bounds, 10.0f, 6.0f, 0.58f);
        juce::ColourGradient bezel(juce::Colour(0xff85745c), bounds.getX(), bounds.getY(),
                                   juce::Colour(0xff100d09), bounds.getRight(), bounds.getBottom(), false);
        bezel.addColour(0.28, juce::Colour(0xff3c342a));
        bezel.addColour(0.70, juce::Colour(0xff191510));
        graphics.setGradientFill(bezel);
        graphics.fillRoundedRectangle(bounds, 10.0f);
        graphics.setColour(juce::Colours::white.withAlpha(0.12f));
        graphics.drawRoundedRectangle(bounds.reduced(1.0f), 9.0f, 1.0f);
        graphics.setColour(accent.withAlpha(0.42f));
        graphics.drawRoundedRectangle(bounds.reduced(3.0f), 7.0f, 1.0f);

        const auto content = bounds.reduced(12.0f);
        if (productId == "A04") drawReels(graphics, content.reduced(6.0f));
        else if (productId == "A05") drawTube(graphics, content.reduced(12.0f));
        else if (productId == "A09") drawPassiveCurve(graphics, content.reduced(12.0f));
        else if (productId == "A10") drawPlate(graphics, content.reduced(8.0f));
        else if (productId == "A03") drawSignalPath(graphics, content.reduced(12.0f));
        else drawMeter(graphics, content.reduced(6.0f));
    }

private:
    PluginProcessor& processor;
    juce::Colour accent;
    juce::String productId;
    float phase = 0.0f;
    float level = 0.0f;

    float parameter(const juce::String& id, float fallback = 0.0f) const
    {
        if (const auto* value = processor.state.getRawParameterValue(id)) return value->load();
        return fallback;
    }

    void drawMeter(juce::Graphics& graphics, juce::Rectangle<float> area)
    {
        juce::ColourGradient face(juce::Colour(0xfff2d99c), area.getCentreX(), area.getY(),
                                  juce::Colour(0xff9d7136), area.getCentreX(), area.getBottom(), false);
        face.addColour(0.42, juce::Colour(0xffd9b871));
        graphics.setGradientFill(face);
        graphics.fillRoundedRectangle(area, 7.0f);
        drawFineTexture(graphics, area, juce::Colour(0xff6f4721));
        graphics.setColour(juce::Colour(0xff3a2413));
        graphics.drawRoundedRectangle(area, 7.0f, 1.8f);
        const auto centre = juce::Point<float>(area.getCentreX(), area.getBottom() + area.getHeight() * 0.08f);
        const auto radius = area.getWidth() * 0.40f;
        for (int tick = 0; tick <= 10; ++tick)
        {
            const auto angle = juce::jmap(static_cast<float>(tick), 0.0f, 10.0f, -1.05f, 1.05f);
            const auto inner = centre + juce::Point<float>(std::sin(angle), -std::cos(angle)) * (radius * (tick % 2 == 0 ? 0.76f : 0.82f));
            const auto outer = centre + juce::Point<float>(std::sin(angle), -std::cos(angle)) * radius;
            graphics.setColour(tick >= 8 ? juce::Colour(0xff8b2018) : juce::Colour(0xff392515));
            graphics.drawLine({ inner, outer }, tick % 2 == 0 ? 1.5f : 0.8f);
            if (tick % 2 == 0)
            {
                const auto labelPoint = centre + juce::Point<float>(std::sin(angle), -std::cos(angle)) * (radius * 0.66f);
                graphics.setFont(juce::FontOptions("Bahnschrift", 10.0f, juce::Font::plain));
                graphics.drawText(juce::String(tick * 2), static_cast<int>(labelPoint.x) - 14,
                                  static_cast<int>(labelPoint.y) - 7, 28, 14, juce::Justification::centred);
            }
        }
        const auto angle = juce::jmap(level, 0.0f, 1.0f, -1.02f, 1.02f);
        const auto tip = centre + juce::Point<float>(std::sin(angle), -std::cos(angle)) * (radius * 0.9f);
        graphics.setColour(juce::Colours::black.withAlpha(0.45f));
        graphics.drawLine({ centre.translated(1.0f, 1.4f), tip.translated(1.0f, 1.4f) }, 2.5f);
        graphics.setColour(juce::Colour(0xff781d16));
        graphics.drawLine({ centre, tip }, 1.7f);
        juce::ColourGradient hub(juce::Colour(0xff625038), centre.x - 9.0f, centre.y - 9.0f,
                                 juce::Colour(0xff120e09), centre.x + 9.0f, centre.y + 9.0f, false);
        graphics.setGradientFill(hub);
        graphics.fillEllipse(centre.x - 9.0f, centre.y - 9.0f, 18.0f, 18.0f);
        graphics.setColour(juce::Colour(0xff3b2414));
        graphics.setFont(juce::FontOptions("Bahnschrift", 17.0f, juce::Font::bold));
        const auto label = productId == "A06" || productId == "A07" || productId == "A08"
                         ? "GAIN REDUCTION / OUTPUT" : "OUTPUT LEVEL";
        graphics.drawText(label, area.toNearestInt().removeFromTop(35), juce::Justification::centred);
        juce::ColourGradient glass(juce::Colours::white.withAlpha(0.17f), area.getX(), area.getY(),
                                   juce::Colours::white.withAlpha(0.0f), area.getX(), area.getCentreY(), false);
        graphics.setGradientFill(glass);
        graphics.fillRoundedRectangle(area.reduced(3.0f).withHeight(area.getHeight() * 0.42f), 5.0f);
    }

    void drawReels(juce::Graphics& graphics, juce::Rectangle<float> area)
    {
        const auto radius = juce::jmin(area.getHeight() * 0.42f, area.getWidth() * 0.20f);
        const std::array<float, 2> xs { area.getX() + area.getWidth() * 0.27f, area.getX() + area.getWidth() * 0.73f };
        for (const auto x : xs)
        {
            const auto centre = juce::Point<float>(x, area.getCentreY() - 4.0f);
            juce::ColourGradient metalFill(juce::Colour(0xffb49560), centre.x - radius, centre.y - radius,
                                           juce::Colour(0xff30271c), centre.x + radius, centre.y + radius, false);
            graphics.setGradientFill(metalFill);
            graphics.fillEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);
            graphics.setColour(juce::Colour(0xff0a0907));
            for (int spoke = 0; spoke < 6; ++spoke)
            {
                const auto a = phase + spoke * juce::MathConstants<float>::twoPi / 6.0f;
                const auto inner = centre + juce::Point<float>(std::cos(a), std::sin(a)) * (radius * 0.24f);
                const auto outer = centre + juce::Point<float>(std::cos(a), std::sin(a)) * (radius * 0.84f);
                graphics.drawLine({ inner, outer }, radius * 0.16f);
            }
            graphics.setColour(juce::Colour(brass));
            graphics.fillEllipse(centre.x - radius * 0.18f, centre.y - radius * 0.18f,
                                 radius * 0.36f, radius * 0.36f);
        }
        graphics.setColour(accent.withAlpha(0.72f));
        graphics.drawLine(xs[0], area.getBottom() - 18.0f, xs[1], area.getBottom() - 18.0f, 3.0f);
        graphics.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        graphics.drawText("DUAL REEL TRANSPORT / SIGNAL-DRIVEN", area.toNearestInt().removeFromBottom(22),
                          juce::Justification::centred);
    }

    void drawTube(juce::Graphics& graphics, juce::Rectangle<float> area)
    {
        auto chamber = area.reduced(area.getWidth() * 0.18f, 4.0f);
        graphics.setColour(juce::Colour(0xff1d0b06));
        graphics.fillRoundedRectangle(chamber, 10.0f);
        const auto glow = juce::jlimit(0.08f, 1.0f, parameter("drive") * 0.09f + level * 0.5f);
        graphics.setColour(accent.withAlpha(glow * 0.24f));
        graphics.fillEllipse(chamber.getCentreX() - 95.0f, chamber.getCentreY() - 95.0f, 190.0f, 190.0f);
        auto tube = juce::Rectangle<float>(90.0f, chamber.getHeight() * 0.70f).withCentre(chamber.getCentre());
        juce::ColourGradient glass(accent.withAlpha(0.70f), tube.getCentreX(), tube.getBottom(),
                                   juce::Colour(0xff25100a).withAlpha(0.45f), tube.getCentreX(), tube.getY(), false);
        graphics.setGradientFill(glass);
        graphics.fillRoundedRectangle(tube, 32.0f);
        graphics.setColour(juce::Colour(cream).withAlpha(0.40f));
        graphics.drawRoundedRectangle(tube.reduced(3.0f), 30.0f, 1.4f);
        for (int line = 0; line < 4; ++line)
        {
            const auto x = tube.getX() + 20.0f + line * 17.0f;
            graphics.setColour(accent.withAlpha(0.75f));
            graphics.drawVerticalLine(static_cast<int>(x), tube.getY() + 35.0f, tube.getBottom() - 18.0f);
        }
        graphics.setColour(juce::Colour(brass));
        graphics.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        graphics.drawText("HARMONIC VALVE CHAMBER", area.toNearestInt().removeFromBottom(24), juce::Justification::centred);
    }

    void drawPassiveCurve(juce::Graphics& graphics, juce::Rectangle<float> area)
    {
        graphics.setColour(juce::Colour(0xff25302a));
        graphics.fillRoundedRectangle(area, 5.0f);
        for (int x = 0; x <= 10; ++x)
        {
            graphics.setColour(juce::Colour(brass).withAlpha(0.15f));
            graphics.drawVerticalLine(static_cast<int>(area.getX() + x * area.getWidth() / 10.0f), area.getY(), area.getBottom());
        }
        graphics.drawHorizontalLine(static_cast<int>(area.getCentreY()), area.getX(), area.getRight());
        juce::Path curve;
        for (int x = 0; x <= static_cast<int>(area.getWidth()); ++x)
        {
            const auto n = static_cast<float>(x) / area.getWidth();
            const auto low = parameter("low_boost") * std::exp(-n * 8.0f) - parameter("low_cut") * 0.7f * std::exp(-n * 11.0f);
            const auto mid = parameter("mid_gain") * std::exp(-std::pow((n - 0.52f) / 0.16f, 2.0f));
            const auto high = parameter("high_boost") * std::exp(-(1.0f - n) * 7.0f)
                            - parameter("high_cut") * 0.7f * std::exp(-(1.0f - n) * 10.0f);
            const auto y = area.getCentreY() - juce::jlimit(-15.0f, 15.0f, low + mid + high) * area.getHeight() / 36.0f;
            if (x == 0) curve.startNewSubPath(area.getX(), y);
            else curve.lineTo(area.getX() + x, y);
        }
        graphics.setColour(accent.withAlpha(0.20f));
        graphics.strokePath(curve, juce::PathStrokeType(7.0f));
        graphics.setColour(accent.brighter(0.35f));
        graphics.strokePath(curve, juce::PathStrokeType(1.8f));
        graphics.setColour(juce::Colour(cream));
        graphics.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        graphics.drawText("PASSIVE PROGRAM CURVE", area.toNearestInt().removeFromTop(24), juce::Justification::centred);
    }

    void drawPlate(juce::Graphics& graphics, juce::Rectangle<float> area)
    {
        juce::ColourGradient frame(juce::Colour(0xffa78555), area.getX(), area.getY(),
                                   juce::Colour(0xff33271d), area.getRight(), area.getBottom(), false);
        graphics.setGradientFill(frame);
        graphics.fillRoundedRectangle(area, 10.0f);
        auto chamber = area.reduced(20.0f);
        graphics.setColour(juce::Colour(0xff2b2528));
        graphics.fillRoundedRectangle(chamber, 7.0f);
        const auto glow = 0.10f + level * 0.38f;
        graphics.setColour(accent.withAlpha(glow));
        for (int line = 0; line < 7; ++line)
        {
            const auto y = chamber.getY() + (line + 1) * chamber.getHeight() / 8.0f;
            const auto displacement = std::sin(phase * 1.7f + line) * level * 8.0f;
            graphics.drawLine(chamber.getX() + 14.0f, y, chamber.getRight() - 14.0f, y + displacement, 1.2f);
        }
        graphics.setColour(juce::Colour(brass));
        graphics.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        graphics.drawText("ELECTRO-MECHANICAL PLATE CHAMBER", area.toNearestInt().removeFromTop(27),
                          juce::Justification::centred);
    }

    void drawSignalPath(juce::Graphics& graphics, juce::Rectangle<float> area)
    {
        const std::array<juce::String, 5> labels { "INPUT", "PREAMP", "FILTER", "EQ", "COLOR" };
        const auto y = area.getCentreY();
        for (int index = 0; index < 5; ++index)
        {
            const auto x = area.getX() + (index + 0.5f) * area.getWidth() / 5.0f;
            graphics.setColour(accent.withAlpha(0.22f));
            graphics.fillEllipse(x - 28.0f, y - 28.0f, 56.0f, 56.0f);
            graphics.setColour(accent);
            graphics.drawEllipse(x - 20.0f, y - 20.0f, 40.0f, 40.0f, 1.5f);
            graphics.setColour(juce::Colour(cream));
            graphics.setFont(juce::FontOptions(8.0f, juce::Font::bold));
            graphics.drawText(labels[static_cast<size_t>(index)], static_cast<int>(x) - 38,
                              static_cast<int>(y) - 7, 76, 14, juce::Justification::centred);
            if (index < 4)
            {
                graphics.setColour(juce::Colour(brass));
                graphics.drawArrow({ x + 24.0f, y, x + area.getWidth() / 5.0f - 24.0f, y }, 1.3f, 7.0f, 6.0f);
            }
        }
    }

    void timerCallback() override
    {
        const auto target = juce::jmax(processor.getOutputPeak(0), processor.getOutputPeak(1));
        level = juce::jmax(juce::jlimit(0.0f, 1.0f, target), level * 0.91f);
        phase = std::fmod(phase + 0.025f + level * 0.14f, juce::MathConstants<float>::twoPi);
        repaint();
    }
};

juce::Colour productAccent(const juce::String& id)
{
    if (id == "A02") return juce::Colour(0xffe75b31);
    if (id == "A03") return juce::Colour(0xffd99a36);
    if (id == "A04") return juce::Colour(0xffd18a28);
    if (id == "A05") return juce::Colour(0xffe15b2a);
    if (id == "A06") return juce::Colour(0xff63b36a);
    if (id == "A07") return juce::Colour(0xff4998e8);
    if (id == "A08") return juce::Colour(0xff9a5bc7);
    if (id == "A09") return juce::Colour(0xffd79a3a);
    return juce::Colour(0xffc05cbd);
}

juce::Colour controlFace(const juce::String& id, juce::Colour accent, int index)
{
    if (id == "A03" || id == "A09")
    {
        constexpr std::array<juce::uint32, 5> faces { 0xffc5821f, 0xff71882e, 0xffc8ad80, 0xff176b84, 0xffa93722 };
        return juce::Colour(faces[static_cast<size_t>(index % 5)]);
    }
    return index % 4 == 0 ? accent.darker(0.25f) : juce::Colour(0xff17140f);
}
}

class AnalogProductEditor::Surface final : public juce::Component
{
public:
    explicit Surface(PluginProcessor& owner)
        : processor(owner), accent(productAccent(owner.spec.id)), look(accent), visual(owner, accent)
    {
        configureLayout();
        for (size_t index = 0; index < slots.size(); ++index)
        {
            const auto& slot = slots[index];
            if (const auto* descriptor = findDescriptor(processor.spec, slot.id))
            {
                auto control = std::make_unique<HardwareControl>(processor.state, *descriptor,
                                                                  slot.colour, look);
                controls[slot.id] = control.get();
                addAndMakeVisible(*control);
                ownedControls.push_back(std::move(control));
            }
        }
        addAndMakeVisible(visual);
        visual.toBack();
    }

    void paint(juce::Graphics& graphics) override
    {
        const auto sx = getWidth() / baseWidth;
        const auto sy = getHeight() / baseHeight;
        graphics.addTransform(juce::AffineTransform::scale(sx, sy));

        graphics.fillAll(juce::Colour(0xff070604));

        juce::ColourGradient walnut(juce::Colour(0xff5b321f), 0.0f, 0.0f,
                                    juce::Colour(0xff160c08), baseWidth, baseHeight, false);
        walnut.addColour(0.18, juce::Colour(0xff28140d));
        walnut.addColour(0.82, juce::Colour(0xff3b2115));
        graphics.setGradientFill(walnut);
        graphics.fillRoundedRectangle(1.0f, 1.0f, baseWidth - 2.0f, baseHeight - 2.0f, 10.0f);
        for (int y = 5; y < 800; y += 9)
        {
            const auto grain = 0.5f + 0.5f * std::sin(static_cast<float>(y) * 0.31f);
            graphics.setColour(juce::Colour(0xffc27a48).withAlpha(0.012f + grain * 0.022f));
            graphics.drawHorizontalLine(y, 3.0f, baseWidth - 3.0f);
        }

        const auto chassis = juce::Rectangle<float>(7.0f, 5.0f, 1266.0f, 790.0f);
        drawSoftShadow(graphics, chassis, 8.0f, 2.0f, 0.64f);
        juce::ColourGradient chassisFill(juce::Colour(0xff302e28), chassis.getX(), chassis.getY(),
                                         juce::Colour(0xff0c0c0a), chassis.getRight(), chassis.getBottom(), false);
        chassisFill.addColour(0.42, juce::Colour(0xff1b1c18));
        chassisFill.addColour(0.76, juce::Colour(0xff141411));
        graphics.setGradientFill(chassisFill);
        graphics.fillRoundedRectangle(chassis, 8.0f);
        graphics.setColour(juce::Colours::white.withAlpha(0.10f));
        graphics.drawRoundedRectangle(chassis.reduced(1.0f), 7.0f, 1.0f);
        graphics.setColour(accent.withAlpha(0.24f));
        graphics.drawRoundedRectangle(chassis.reduced(3.0f), 5.0f, 0.8f);
        drawFineTexture(graphics, chassis.reduced(4.0f), juce::Colour(0xffa69b82));

        const auto header = juce::Rectangle<float>(14.0f, 10.0f, 1252.0f, 58.0f);
        drawSoftShadow(graphics, header, 6.0f, 3.0f, 0.46f);
        juce::ColourGradient headerFill(juce::Colour(0xff292823), header.getX(), header.getY(),
                                        juce::Colour(0xff070705), header.getX(), header.getBottom(), false);
        headerFill.addColour(0.36, juce::Colour(0xff13130f));
        graphics.setGradientFill(headerFill);
        graphics.fillRoundedRectangle(header, 6.0f);
        graphics.setColour(juce::Colours::white.withAlpha(0.08f));
        graphics.drawLine(header.getX() + 7.0f, header.getY() + 1.0f,
                          header.getRight() - 7.0f, header.getY() + 1.0f, 1.0f);
        graphics.setColour(accent.withAlpha(0.44f));
        graphics.drawRoundedRectangle(header, 6.0f, 0.9f);
        drawLogo(graphics);
        graphics.setColour(juce::Colour(cream));
        graphics.setFont(juce::FontOptions("Bahnschrift", 27.0f, juce::Font::bold));
        graphics.drawText(processor.spec.displayName, 0, 15, 1280, 30, juce::Justification::centred);
        graphics.setColour(accent);
        graphics.setFont(juce::FontOptions("Bahnschrift", 9.5f, juce::Font::bold));
        graphics.drawText(processor.spec.primaryRole.toUpperCase(), 0, 46, 1280, 12,
                          juce::Justification::centred);
        drawScrew(graphics, { 28.0f, 39.0f }, 4.0f);
        drawScrew(graphics, { 1252.0f, 39.0f }, 4.0f);

        for (const auto& section : sections)
        {
            drawSoftShadow(graphics, section.bounds, 7.0f, 4.0f, 0.42f);
            juce::ColourGradient fill(juce::Colour(0xff34342e), section.bounds.getX(), section.bounds.getY(),
                                      juce::Colour(0xff11120f), section.bounds.getRight(), section.bounds.getBottom(), false);
            fill.addColour(0.27, juce::Colour(panelRaised));
            fill.addColour(0.68, juce::Colour(panel));
            graphics.setGradientFill(fill);
            graphics.fillRoundedRectangle(section.bounds, 7.0f);
            drawFineTexture(graphics, section.bounds.reduced(2.0f), juce::Colour(0xffa69b82));
            graphics.setColour(juce::Colours::white.withAlpha(0.075f));
            graphics.drawLine(section.bounds.getX() + 8.0f, section.bounds.getY() + 1.0f,
                              section.bounds.getRight() - 8.0f, section.bounds.getY() + 1.0f, 0.9f);
            graphics.setColour(accent.withAlpha(0.28f));
            graphics.drawRoundedRectangle(section.bounds.reduced(0.5f), 7.0f, 0.9f);
            graphics.setColour(juce::Colours::black.withAlpha(0.72f));
            graphics.drawRoundedRectangle(section.bounds.reduced(3.0f), 5.0f, 0.8f);
            drawScrew(graphics, section.bounds.getTopLeft() + juce::Point<float>(12.0f, 12.0f), 4.3f);
            drawScrew(graphics, section.bounds.getTopRight() + juce::Point<float>(-12.0f, 12.0f), 4.3f);
            drawScrew(graphics, section.bounds.getBottomLeft() + juce::Point<float>(12.0f, -12.0f), 4.3f);
            drawScrew(graphics, section.bounds.getBottomRight() + juce::Point<float>(-12.0f, -12.0f), 4.3f);
            if (section.name.isNotEmpty())
            {
                graphics.setColour(juce::Colour(cream).interpolatedWith(accent, 0.18f));
                graphics.setFont(juce::FontOptions("Bahnschrift", 10.5f, juce::Font::bold));
                graphics.drawText(section.name.toUpperCase(), section.bounds.toNearestInt().removeFromTop(25),
                                  juce::Justification::centred);
            }
        }

        const auto footer = juce::Rectangle<float>(14.0f, 750.0f, 1252.0f, 36.0f);
        drawSoftShadow(graphics, footer, 5.0f, 2.0f, 0.28f);
        juce::ColourGradient footerFill(juce::Colour(0xff201f1a), footer.getX(), footer.getY(),
                                        juce::Colour(0xff070705), footer.getX(), footer.getBottom(), false);
        graphics.setGradientFill(footerFill);
        graphics.fillRoundedRectangle(footer, 5.0f);
        graphics.setColour(accent.withAlpha(0.32f));
        graphics.drawRoundedRectangle(footer, 5.0f, 0.8f);
        graphics.setColour(juce::Colour(cream).withAlpha(0.62f));
        graphics.setFont(juce::FontOptions("Bahnschrift", 9.0f, juce::Font::plain));
        graphics.drawText("Shift-drag for fine control  /  double-click resets  /  all controls are automatable",
                          28, 759, 860, 16, juce::Justification::centredLeft);
        graphics.setColour(accent.withAlpha(0.82f));
        graphics.setFont(juce::FontOptions("Bahnschrift", 10.0f, juce::Font::bold));
        graphics.drawText(processor.spec.id + "  /  AMANORSAC ANALOG SERIES", 900, 759, 340, 16,
                          juce::Justification::centredRight);
    }

    void resized() override
    {
        const auto sx = getWidth() / baseWidth;
        const auto sy = getHeight() / baseHeight;
        const auto scale = [sx, sy](juce::Rectangle<float> bounds)
        {
            return juce::Rectangle<int>(juce::roundToInt(bounds.getX() * sx), juce::roundToInt(bounds.getY() * sy),
                                        juce::roundToInt(bounds.getWidth() * sx), juce::roundToInt(bounds.getHeight() * sy));
        };
        visual.setBounds(scale(visualBounds));
        for (const auto& slot : slots)
            if (const auto found = controls.find(slot.id); found != controls.end())
                found->second->setBounds(scale(slot.bounds));
    }

private:
    PluginProcessor& processor;
    juce::Colour accent;
    HardwareLookAndFeel look;
    ProductVisual visual;
    juce::Rectangle<float> visualBounds;
    std::vector<Slot> slots;
    std::vector<Section> sections;
    std::map<juce::String, HardwareControl*> controls;
    std::vector<std::unique_ptr<HardwareControl>> ownedControls;

    void add(const juce::String& id, float x, float y, float width, float height, int faceIndex = 0)
    {
        slots.push_back({ id, { x, y, width, height }, controlFace(processor.spec.id, accent, faceIndex) });
    }

    void section(const juce::String& name, float x, float y, float width, float height)
    {
        sections.push_back({ name, { x, y, width, height } });
    }

    void configureLayout()
    {
        const auto id = processor.spec.id;
        if (id == "A02") configureA02();
        else if (id == "A03") configureA03();
        else if (id == "A04") configureA04();
        else if (id == "A05") configureA05();
        else if (id == "A06") configureA06();
        else if (id == "A07") configureA07();
        else if (id == "A08") configureA08();
        else if (id == "A09") configureA09();
        else configureA10();
    }

    void configureA02()
    {
        section("Input stage", 18, 80, 175, 655);
        section("Transformer preamp", 204, 80, 862, 655);
        section("Output", 1077, 80, 185, 655);
        visualBounds = { 330, 103, 610, 260 };
        add("input", 36, 115, 140, 190, 0); add("hpf", 49, 315, 115, 145, 1);
        add("pad", 38, 487, 137, 52, 2); add("phase", 38, 553, 137, 52, 3);
        add("impedance", 235, 390, 150, 155, 0); add("drive", 393, 390, 150, 155, 1);
        add("transformer", 551, 390, 150, 155, 2); add("low_tone", 709, 390, 150, 155, 3);
        add("high_tone", 867, 390, 150, 155, 4); add("saturation", 465, 558, 180, 160, 0);
        add("output", 1100, 115, 140, 190, 0);
    }

    void configureA03()
    {
        section("Input", 18, 80, 170, 655); section("Channel path", 198, 80, 884, 655);
        section("Output", 1092, 80, 170, 655); visualBounds = { 330, 98, 620, 190 };
        add("input", 35, 115, 136, 185, 0); add("hpf", 47, 310, 112, 145, 1);
        add("noise", 35, 480, 136, 52, 2); add("output", 1109, 115, 136, 185, 0);
        add("mix", 1121, 320, 112, 155, 1);
        const std::array<juce::String, 6> top { "drive", "low", "mid_freq", "mid", "high", "bus_color" };
        for (int i = 0; i < 6; ++i) add(top[static_cast<size_t>(i)], 218 + i * 140.0f, 310, 132, 185, i);
        add("stereo_mode", 390, 535, 180, 85, 1);
    }

    void configureA04()
    {
        section("Tape deck", 18, 80, 1244, 655); visualBounds = { 220, 95, 840, 318 };
        add("input", 35, 112, 145, 185, 0); add("output", 1100, 112, 145, 185, 0);
        add("mix", 1110, 315, 125, 145, 1); add("hiss", 1110, 482, 125, 145, 2);
        const std::array<juce::String, 8> ids { "formula", "speed", "bias", "saturation", "wow", "flutter", "head_bump", "hf_rolloff" };
        for (int i = 0; i < 8; ++i) add(ids[static_cast<size_t>(i)], 205 + i * 105.0f, 430, 100, 205, i);
    }

    void configureA05()
    {
        section("Valve chamber", 18, 80, 1244, 655); visualBounds = { 330, 92, 620, 278 };
        const std::array<juce::String, 6> row { "drive", "bias", "density", "harmonic_balance", "tone", "soft_clip" };
        for (int i = 0; i < 6; ++i) add(row[static_cast<size_t>(i)], 180 + i * 155.0f, 390, 145, 200, i);
        add("topology", 100, 620, 165, 85, 0); add("pre_emphasis", 285, 603, 145, 120, 1);
        add("post_emphasis", 445, 603, 145, 120, 2); add("mix", 900, 603, 145, 120, 3);
        add("output", 1060, 603, 145, 120, 0);
    }

    void configureA06()
    {
        configureCompressor({ "input", "ratio", "attack", "release", "makeup",
                              "sc_hpf", "external_sc", "saturation", "mix", "output" });
    }

    void configureA07()
    {
        section("Optical cell", 18, 80, 1244, 655); visualBounds = { 310, 95, 660, 245 };
        const std::array<juce::String, 4> row { "peak_reduction", "gain", "response", "release" };
        for (int i = 0; i < 4; ++i) add(row[static_cast<size_t>(i)], 245 + i * 200.0f, 365, 185, 210, i);
        add("sc_hpf", 95, 375, 125, 170, 1); add("saturation", 250, 600, 145, 120, 2);
        add("stereo_link", 540, 600, 145, 120, 3); add("mix", 830, 600, 145, 120, 1);
        add("output", 1055, 365, 145, 190, 0);
    }

    void configureA08()
    {
        configureCompressor({ "threshold", "ratio", "attack", "release", "makeup",
                              "sc_hpf", "knee", "auto_release", "stereo_link", "mix" });
    }

    void configureCompressor(const std::array<juce::String, 10>& ids)
    {
        section("Dynamics", 18, 80, 1244, 655); visualBounds = { 310, 95, 660, 235 };
        for (int i = 0; i < 5; ++i) add(ids[static_cast<size_t>(i)], 205 + i * 175.0f, 350, 160, 205, i);
        for (int i = 0; i < 5; ++i) add(ids[static_cast<size_t>(i + 5)], 205 + i * 175.0f, 575, 160, 145, i + 1);
    }

    void configureA09()
    {
        section("Passive response", 18, 80, 1244, 655); visualBounds = { 295, 90, 690, 165 };
        section("Low", 40, 270, 380, 330); section("Mid presence", 450, 270, 380, 330);
        section("High", 860, 270, 380, 330);
        add("low_freq", 65, 305, 145, 190, 0); add("low_boost", 220, 305, 145, 190, 0);
        add("low_cut", 142, 492, 145, 180, 1); add("mid_freq", 485, 305, 145, 190, 2);
        add("mid_gain", 650, 305, 145, 190, 2); add("high_freq", 885, 305, 145, 190, 3);
        add("high_boost", 1040, 305, 145, 190, 3); add("high_cut", 962, 492, 145, 180, 4);
        add("output_stage", 350, 650, 170, 75, 0); add("drive", 555, 615, 145, 115, 1);
        add("output", 760, 615, 145, 115, 0);
    }

    void configureA10()
    {
        section("Plate chamber", 18, 80, 1244, 655); visualBounds = { 250, 90, 780, 285 };
        add("plate", 55, 130, 165, 90, 0); add("mix", 1070, 130, 145, 180, 1);
        add("output", 1070, 330, 145, 180, 0); add("treble", 55, 250, 145, 170, 2);
        const std::array<juce::String, 8> ids { "pre_delay", "decay", "damp", "bass_cut", "drive", "crosstalk", "noise", "width" };
        for (int i = 0; i < 8; ++i) add(ids[static_cast<size_t>(i)], 205 + i * 107.0f, 440, 102, 205, i);
    }

    void drawLogo(juce::Graphics& graphics)
    {
        juce::Path logo;
        logo.startNewSubPath(42.0f, 18.0f); logo.lineTo(57.0f, 33.0f);
        logo.lineTo(42.0f, 48.0f); logo.lineTo(27.0f, 33.0f); logo.closeSubPath();
        logo.startNewSubPath(42.0f, 21.0f); logo.lineTo(42.0f, 45.0f);
        logo.startNewSubPath(30.0f, 33.0f); logo.lineTo(54.0f, 33.0f);
        graphics.setColour(accent); graphics.strokePath(logo, juce::PathStrokeType(1.4f));
        graphics.setColour(juce::Colour(cream)); graphics.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        graphics.drawText("AMANORSAC", 66, 19, 140, 18, juce::Justification::centredLeft);
        graphics.setColour(accent); graphics.setFont(juce::FontOptions(8.0f, juce::Font::bold));
        graphics.drawText("STUDIO", 67, 39, 80, 12, juce::Justification::centredLeft);
    }
};

AnalogProductEditor::AnalogProductEditor(PluginProcessor& owner) : AudioProcessorEditor(owner)
{
    surface = std::make_unique<Surface>(owner);
    addAndMakeVisible(*surface);
    setResizable(true, true);
    setResizeLimits(1024, 640, 1800, 1125);
    setSize(1280, 800);
}

AnalogProductEditor::~AnalogProductEditor() = default;

void AnalogProductEditor::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colour(dark));
}

void AnalogProductEditor::resized()
{
    surface->setBounds(getLocalBounds());
}
}
