#pragma once

#include "ProductHost.h"
#include "AnalogUI.h"
#include "BinaryData.h"

#include <array>
#include <cmath>
#include <functional>
#include <vector>

/** Hardware-grade widget set and editor chassis shared by every analog product.

    Everything here was built for HERITAGE EQ and approved there: the printed
    knobs, machined keys, lamps, VU and LED meters, the top bar with preset
    window and tool keys, typed value entry, Ctrl-click reset, per-gesture undo,
    and the preset / A/B / clipboard menus. The other analog products derive
    their editor surface from AnalogChassis so they behave identically.
*/
namespace amanorsac::hw
{
using namespace analog;


constexpr float sweepDegrees = 270.0f;
constexpr float startDegrees = -135.0f;

const juce::Colour cLow { 0xfff2a83c };
const juce::Colour cLowMid { 0xffd8dc4a };
const juce::Colour cMid { 0xffe8e6df };
const juce::Colour cHighMid { 0xff3cb0e6 };
const juce::Colour cHigh { 0xfff0563a };
const juce::Colour cText { 0xffd9d3c2 };
const juce::Colour cMuted { 0xff9a9483 };
const juce::Colour cGold { 0xffd3b26a };
const juce::Colour cAmber { 0xfff0a030 };
const juce::Colour cGreen { 0xff4bd24f };
const juce::Colour cRed { 0xffc9382c };

inline juce::Point<float> polar(juce::Point<float> centre, float degrees, float radius)
{
    const auto radians = (degrees - 90.0f) * juce::MathConstants<float>::pi / 180.0f;
    return { centre.x + radius * std::cos(radians), centre.y + radius * std::sin(radians) };
}

inline const ParameterDescriptor* find(const PluginSpec& spec, const juce::String& id)
{
    for (const auto& descriptor : spec.parameters)
        if (descriptor.id == id)
            return &descriptor;
    return nullptr;
}

inline juce::Image brandLogoImage()
{
    int size = 0;
    if (const auto* data = AmanorsacBinaryData::getNamedResource("AmanorsacLogo_png", size))
        return juce::ImageFileFormat::loadFrom(data, static_cast<size_t>(size));
    return {};
}

/** The official artwork re-inked as a silkscreen print: the shape and alpha
    are kept exactly, but the pure white and blue become the panel's warm
    ivory and aged gold, so the mark reads as printed on the metal rather
    than pasted over it. */
struct PrintedLogo
{
    juce::Image ink, shadow;

    explicit PrintedLogo(const juce::Image& source)
    {
        if (! source.isValid()) return;
        ink = source.createCopy().convertedToFormat(juce::Image::ARGB);
        shadow = juce::Image(juce::Image::ARGB, ink.getWidth(), ink.getHeight(), true);
        const juce::Colour light(0xffe9dfc6), dark(0xff9a8657);
        juce::Image::BitmapData in(ink, juce::Image::BitmapData::readWrite);
        juce::Image::BitmapData sh(shadow, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < in.height; ++y)
            for (int x = 0; x < in.width; ++x)
            {
                const auto p = in.getPixelColour(x, y);
                const auto lum = p.getPerceivedBrightness();
                in.setPixelColour(x, y, dark.interpolatedWith(light, lum).withAlpha(p.getAlpha()));
                sh.setPixelColour(x, y, juce::Colours::black.withAlpha(p.getAlpha()));
            }
    }

    /** Draws the print with a faint recessed shadow, slightly faded into the panel. */
    void draw(juce::Graphics& g, juce::Rectangle<float> area, float opacity) const
    {
        if (! ink.isValid()) return;
        const auto placement = juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize;
        g.setOpacity(opacity * 0.55f);
        g.drawImage(shadow, area.translated(0.0f, 1.5f), placement);
        g.setOpacity(opacity);
        g.drawImage(ink, area, placement);
        g.setOpacity(1.0f);
    }
};

/** The backdrop belonging to one product. The rack embeds all ten, so the
    lookup has to name the product rather than take the first it finds. */
inline juce::Image backdropImage(const juce::String& productId = {})
{
    for (int i = 0; i < AmanorsacBinaryData::namedResourceListSize; ++i)
    {
        const juce::String name(AmanorsacBinaryData::namedResourceList[i]);
        if (! name.contains("CLEAN_BACKDROP")) continue;
        if (productId.isNotEmpty() && ! name.startsWith(productId + "_")) continue;
        int size = 0;
        if (const auto* data = AmanorsacBinaryData::getNamedResource(name.toRawUTF8(), size))
            return juce::ImageFileFormat::loadFrom(data, static_cast<size_t>(size));
    }
    return {};
}

inline void drawHexBadge(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour, float weight)
{
    const auto centre = area.getCentre();
    const auto radius = juce::jmin(area.getWidth(), area.getHeight()) * 0.5f;
    auto hexagon = [&](float r)
    {
        juce::Path path;
        for (int i = 0; i < 6; ++i)
        {
            const auto point = polar(centre, 30.0f + 60.0f * static_cast<float>(i), r);
            if (i == 0) path.startNewSubPath(point); else path.lineTo(point);
        }
        path.closeSubPath();
        return path;
    };
    g.setColour(colour);
    g.strokePath(hexagon(radius), juce::PathStrokeType(weight, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
    g.strokePath(hexagon(radius * 0.62f), juce::PathStrokeType(weight));
    g.drawLine(centre.x, centre.y - radius * 0.62f, centre.x, centre.y + radius * 0.62f, weight);
    g.drawLine(centre.x - radius * 0.54f, centre.y - radius * 0.31f,
               centre.x + radius * 0.54f, centre.y + radius * 0.31f, weight);
    g.drawLine(centre.x + radius * 0.54f, centre.y - radius * 0.31f,
               centre.x - radius * 0.54f, centre.y + radius * 0.31f, weight);
}

inline juce::String lastAuthor;   // remembered between saves within one session

// ------------------------------------------------------------ value entry

/** Typed numeric entry for a knob, shown in a call-out on double-click. The
    text goes through the slider's own text/value functions, so units and
    ranges are exactly the parameter contract's. */
class ValueEntry final : public juce::Component
{
public:
    explicit ValueEntry(juce::Slider& target) : slider(target)
    {
        editor.setFont(labelFont(18.0f, false, 1.0f));
        editor.setJustification(juce::Justification::centred);
        editor.setSelectAllWhenFocused(true);
        editor.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff0d0c0b));
        editor.setColour(juce::TextEditor::textColourId, juce::Colour(0xffecdfc2));
        editor.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff2a2723));
        editor.setColour(juce::TextEditor::focusedOutlineColourId, cAmber);
        editor.setColour(juce::TextEditor::highlightColourId, cAmber.withAlpha(0.35f));
        editor.setText(slider.getTextFromValue(slider.getValue()), false);
        editor.onReturnKey = [this] { commit(); };
        editor.onEscapeKey = [this] { close(); };
        addAndMakeVisible(editor);
        setSize(170, 66);

        juce::Component::SafePointer<ValueEntry> safe(this);
        juce::Timer::callAfterDelay(30, [safe] { if (safe != nullptr) safe->editor.grabKeyboardFocus(); });
    }

    void paint(juce::Graphics& g) override
    {
        g.setColour(cMuted);
        g.setFont(labelFont(12.0f, true, 1.0f));
        g.drawText(slider.getTitle().toUpperCase(), getLocalBounds().removeFromTop(24),
                   juce::Justification::centred, false);
    }

    void resized() override { editor.setBounds(getLocalBounds().withTrimmedTop(24).reduced(10, 6)); }

private:
    void commit()
    {
        slider.setValue(slider.getValueFromText(editor.getText()), juce::sendNotificationSync);
        close();
    }
    void close()
    {
        if (auto* box = findParentComponentOfClass<juce::CallOutBox>()) box->dismiss();
    }

    juce::Slider& slider;
    juce::TextEditor editor;
};

// -------------------------------------------------------------- top bar keys

enum class ToolIcon { none, undo, redo, gear, menu };

/** Machined toolbar key from the reference layout: text or a stroked glyph. */
class ToolKey final : public juce::Button
{
public:
    ToolKey(juce::String label, ToolIcon glyph) : juce::Button(label), text(std::move(label)), icon(glyph) {}

    std::function<bool()> isAvailable;   // greys the key when there is nothing to do
    std::function<int()> activeSlot;     // A/B key: which letter is lit

    void paintButton(juce::Graphics& g, bool over, bool down) override
    {
        const auto area = getLocalBounds().toFloat().reduced(0.5f);
        const auto available = ! isAvailable || isAvailable();

        juce::ColourGradient face(juce::Colour(0xff26231f), area.getCentreX(), area.getY(),
                                  juce::Colour(0xff161412), area.getCentreX(), area.getBottom(), false);
        g.setGradientFill(face);
        g.fillRoundedRectangle(area, 7.0f);
        if (down)
        {
            g.setColour(juce::Colours::black.withAlpha(0.28f));
            g.fillRoundedRectangle(area, 7.0f);
        }
        else if (over && available)
        {
            g.setColour(juce::Colours::white.withAlpha(0.05f));
            g.fillRoundedRectangle(area, 7.0f);
        }
        g.setColour(juce::Colours::white.withAlpha(0.05f));
        g.drawLine(area.getX() + 6.0f, area.getY() + 1.0f, area.getRight() - 6.0f, area.getY() + 1.0f, 1.0f);
        g.setColour(juce::Colour(0xff0b0a09));
        g.drawRoundedRectangle(area, 7.0f, 1.0f);

        const auto ink = juce::Colour(0xffcfc6ad).withAlpha(available ? 1.0f : 0.35f);
        const auto shift = down ? 1.0f : 0.0f;

        if (activeSlot)
        {
            const auto slot = activeSlot();
            g.setFont(labelFont(14.0f, true, 1.0f));
            const auto third = area.getWidth() / 3.0f;
            g.setColour(slot == 0 ? cAmber : ink);
            g.drawText("A", area.withWidth(third).translated(third * 0.25f, shift), juce::Justification::centred, false);
            g.setColour(ink.withAlpha(0.5f));
            g.drawText("/", area.withWidth(third).translated(third, shift), juce::Justification::centred, false);
            g.setColour(slot == 1 ? cAmber : ink);
            g.drawText("B", area.withWidth(third).translated(third * 1.75f, shift), juce::Justification::centred, false);
            return;
        }
        if (icon == ToolIcon::none)
        {
            g.setColour(ink);
            g.setFont(labelFont(13.0f, true, 1.0f));
            g.drawText(text, area.translated(0.0f, shift), juce::Justification::centred, false);
            return;
        }
        drawGlyph(g, juce::Rectangle<float>(20.0f, 20.0f).withCentre(area.getCentre()).translated(0.0f, shift), ink);
    }

private:
    void drawGlyph(juce::Graphics& g, juce::Rectangle<float> box, juce::Colour ink) const
    {
        // the reference glyphs are drawn on a 24-unit grid
        const auto scale = box.getWidth() / 24.0f;
        const auto transform = juce::AffineTransform::scale(scale).translated(box.getX(), box.getY());
        const juce::PathStrokeType stroke(2.0f * scale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        const auto pi = juce::MathConstants<float>::pi;
        juce::Path path;
        switch (icon)
        {
            case ToolIcon::undo:
                path.startNewSubPath(9.0f, 14.0f); path.lineTo(4.0f, 9.0f); path.lineTo(9.0f, 4.0f);
                path.startNewSubPath(4.0f, 9.0f); path.lineTo(15.0f, 9.0f);
                path.addCentredArc(15.0f, 14.0f, 5.0f, 5.0f, 0.0f, 0.0f, pi, false);
                path.lineTo(12.0f, 19.0f);
                break;
            case ToolIcon::redo:
                path.startNewSubPath(15.0f, 14.0f); path.lineTo(20.0f, 9.0f); path.lineTo(15.0f, 4.0f);
                path.startNewSubPath(20.0f, 9.0f); path.lineTo(9.0f, 9.0f);
                path.addCentredArc(9.0f, 14.0f, 5.0f, 5.0f, 0.0f, 0.0f, -pi, false);
                path.lineTo(12.0f, 19.0f);
                break;
            case ToolIcon::gear:
                path.addEllipse(9.0f, 9.0f, 6.0f, 6.0f);
                path.addEllipse(5.0f, 5.0f, 14.0f, 14.0f);
                for (int i = 0; i < 8; ++i)
                {
                    const auto a = polar({ 12.0f, 12.0f }, 45.0f * static_cast<float>(i), 7.0f);
                    const auto b = polar({ 12.0f, 12.0f }, 45.0f * static_cast<float>(i), 10.5f);
                    path.startNewSubPath(a); path.lineTo(b);
                }
                break;
            case ToolIcon::menu:
                for (float y : { 6.0f, 12.0f, 18.0f }) { path.startNewSubPath(4.0f, y); path.lineTo(20.0f, y); }
                break;
            case ToolIcon::none: break;
        }
        g.setColour(ink);
        g.strokePath(path, stroke, transform);
    }

    juce::String text;
    ToolIcon icon;
};

/** Preset previous / next key, rounded on its outer side only. */
class NavKey final : public juce::Button
{
public:
    explicit NavKey(bool leftSide)
        : juce::Button(leftSide ? "Previous preset" : "Next preset"), left(leftSide) {}

    void paintButton(juce::Graphics& g, bool over, bool down) override
    {
        const auto area = getLocalBounds().toFloat().reduced(0.5f);
        juce::Path shape;
        shape.addRoundedRectangle(area.getX(), area.getY(), area.getWidth(), area.getHeight(), 8.0f, 8.0f,
                                  left, ! left, left, ! left);
        juce::ColourGradient face(juce::Colour(0xff2a2723), area.getCentreX(), area.getY(),
                                  juce::Colour(0xff1a1815), area.getCentreX(), area.getBottom(), false);
        g.setGradientFill(face);
        g.fillPath(shape);
        if (down || over)
        {
            g.setColour(down ? juce::Colours::black.withAlpha(0.28f) : juce::Colours::white.withAlpha(0.05f));
            g.fillPath(shape);
        }
        g.setColour(juce::Colour(0xff0b0a09));
        g.strokePath(shape, juce::PathStrokeType(1.0f));

        juce::Path chevron;
        const auto c = area.getCentre().translated(left ? -1.0f : 1.0f, down ? 1.0f : 0.0f);
        const auto d = left ? -1.0f : 1.0f;
        chevron.startNewSubPath(c.x - 4.0f * d, c.y - 7.0f);
        chevron.lineTo(c.x + 3.0f * d, c.y);
        chevron.lineTo(c.x - 4.0f * d, c.y + 7.0f);
        g.setColour(juce::Colour(0xffd6cdb6));
        g.strokePath(chevron, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

private:
    bool left;
};

/** The preset name window. Clicking it opens the preset menu. */
class PresetWindow final : public juce::Button
{
public:
    PresetWindow() : juce::Button("Preset") {}

    std::function<juce::String()> caption;
    std::function<bool()> isModified;
    juce::String status;   // transient confirmation such as SAVED or COPIED

    void paintButton(juce::Graphics& g, bool over, bool) override
    {
        const auto area = getLocalBounds().toFloat();
        g.setColour(juce::Colour(0xff0d0c0b));
        g.fillRect(area);
        juce::ColourGradient inset(juce::Colours::black.withAlpha(0.7f), 0.0f, area.getY(),
                                   juce::Colours::transparentBlack, 0.0f, area.getY() + 8.0f, false);
        g.setGradientFill(inset);
        g.fillRect(area.withHeight(8.0f));
        g.setColour(over ? juce::Colour(0xff3a3631) : juce::Colour(0xff2a2723));
        g.drawRect(area, 1.0f);

        if (status.isNotEmpty())
        {
            g.setColour(cAmber);
            g.setFont(labelFont(15.0f, true, 1.0f));
            g.drawText(status, area, juce::Justification::centred, false);
            return;
        }

        const auto modified = isModified && isModified();
        g.setColour(juce::Colour(0xffecdfc2));
        g.setFont(labelFont(19.0f, false, 1.0f));
        g.drawText(caption ? caption() : juce::String(), area.reduced(28.0f, 0.0f),
                   juce::Justification::centred, true);
        if (modified)
        {
            g.setColour(cAmber);
            g.fillEllipse(juce::Rectangle<float>(7.0f, 7.0f).withCentre({ area.getX() + 14.0f, area.getCentreY() }));
        }
        juce::Path chevron;
        const auto c = juce::Point<float>(area.getRight() - 15.0f, area.getCentreY());
        chevron.startNewSubPath(c.x - 5.0f, c.y - 2.5f);
        chevron.lineTo(c.x, c.y + 2.5f);
        chevron.lineTo(c.x + 5.0f, c.y - 2.5f);
        g.setColour(juce::Colour(0xff9a9483));
        g.strokePath(chevron, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
};

/** Dark look for the menus and dialogs the editor opens. */
class MenuLook final : public juce::LookAndFeel_V4
{
public:
    MenuLook()
    {
        setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff15130f));
        setColour(juce::PopupMenu::textColourId, juce::Colour(0xffd9d3c2));
        setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xff3a2f1c));
        setColour(juce::PopupMenu::highlightedTextColourId, juce::Colour(0xfff5e6c0));
        setColour(juce::PopupMenu::headerTextColourId, cGold);
        setColour(juce::AlertWindow::backgroundColourId, juce::Colour(0xff1a1815));
        setColour(juce::AlertWindow::textColourId, cText);
        setColour(juce::AlertWindow::outlineColourId, juce::Colour(0xff3a3631));
        setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff0d0c0b));
        setColour(juce::TextEditor::textColourId, juce::Colour(0xffecdfc2));
        setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff2a2723));
        setColour(juce::TextEditor::focusedOutlineColourId, cAmber);
        setColour(juce::TextEditor::highlightColourId, cAmber.withAlpha(0.35f));
        setColour(juce::TextButton::buttonColourId, juce::Colour(0xff26231f));
        setColour(juce::TextButton::textColourOffId, juce::Colour(0xffcfc6ad));
        setColour(juce::Label::textColourId, cText);
    }

    juce::Font getPopupMenuFont() override { return labelFont(16.0f, false, 1.0f); }
    juce::Font getAlertWindowTitleFont() override { return labelFont(19.0f, true, 1.0f); }
    juce::Font getAlertWindowMessageFont() override { return labelFont(16.0f, false, 1.0f); }
    juce::Font getAlertWindowFont() override { return labelFont(15.0f, false, 1.0f); }
    juce::Font getTextButtonFont(juce::TextButton&, int) override { return labelFont(14.0f, true, 1.0f); }
};

// --------------------------------------------------------------------- knob

/** Faceplate knob: printed tick ring, engraved legends around the skirt, an
    optional coloured value arc and a coloured cap, exactly as the panel shows. */
class HeritageKnob final : public juce::Slider
{
public:
    struct Legend { juce::String text; float angle; float radiusOffset = 0.0f; };

    HeritageKnob(float knobSize, juce::Colour capColour, juce::Colour arcColour,
                 int tickCount, int majorEvery, bool bipolarArc)
        : diameter(knobSize), cap(capColour), arc(arcColour),
          ticks(tickCount), major(majorEvery), bipolar(bipolarArc)
    {
        setSliderStyle(juce::Slider::RotaryVerticalDrag);
        setRotaryParameters(juce::degreesToRadians(180.0f + startDegrees),
                            juce::degreesToRadians(180.0f + startDegrees + sweepDegrees), true);
        setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
        setMouseDragSensitivity(200);
        setPopupDisplayEnabled(true, false, nullptr);   // value bubble while dragging
    }

    void setLegends(std::vector<Legend> items) { legends = std::move(items); repaint(); }
    void setLegendSize(float size) { legendSize = size; }

    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        auto adjusted = w;
        if (e.mods.isShiftDown()) { adjusted.deltaY *= 0.15f; adjusted.deltaX *= 0.15f; }
        juce::Slider::mouseWheelMove(e, adjusted);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        // Ctrl-click (Cmd on macOS) returns the control to its contract default.
        if (e.mods.isCommandDown() && ! e.mods.isPopupMenu())
        {
            setValue(getDoubleClickReturnValue(), juce::sendNotificationSync);
            return;
        }
        juce::Slider::mouseDown(e);
    }

    void mouseDoubleClick(const juce::MouseEvent&) override
    {
        juce::CallOutBox::launchAsynchronously(std::make_unique<ValueEntry>(*this), getScreenBounds(), nullptr);
    }

    void paint(juce::Graphics& g) override
    {
        const auto centre = getLocalBounds().toFloat().getCentre();
        const auto radius = diameter * 0.5f;
        const auto proportion = getRange().getLength() > 0.0
            ? static_cast<float>((getValue() - getMinimum()) / getRange().getLength()) : 0.0f;
        const auto angle = startDegrees + sweepDegrees * proportion;

        // printed tick ring
        const auto tickRadius = radius + 10.0f;
        for (int i = 0; i < ticks; ++i)
        {
            const auto t = static_cast<float>(i) / static_cast<float>(juce::jmax(1, ticks - 1));
            const auto a = startDegrees + sweepDegrees * t;
            const auto isMajor = major <= 1 || (i % major) == 0;
            g.setColour(cText.withAlpha(isMajor ? 0.9f : 0.5f));
            g.drawLine({ polar(centre, a, tickRadius),
                         polar(centre, a, tickRadius + (isMajor ? 7.0f : 4.0f)) },
                       isMajor ? 1.6f : 1.0f);
        }

        // engraved legends
        if (! legends.empty())
        {
            g.setFont(labelFont(legendSize, true, 0.95f));
            g.setColour(juce::Colour(0xffe4dccb));
            for (const auto& legend : legends)
            {
                const auto at = polar(centre, legend.angle, radius + 24.0f + legend.radiusOffset);
                g.drawText(legend.text, juce::Rectangle<float>(58.0f, 16.0f).withCentre(at),
                           juce::Justification::centred, false);
            }
        }

        // value arc
        if (! arc.isTransparent())
        {
            const auto arcRadius = radius + 6.0f;
            juce::Path track;
            track.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                                juce::degreesToRadians(startDegrees),
                                juce::degreesToRadians(startDegrees + sweepDegrees), true);
            g.setColour(arc.withAlpha(0.35f));
            g.strokePath(track, juce::PathStrokeType(2.0f));

            const auto from = bipolar ? 0.0f : startDegrees;
            if (std::abs(angle - from) > 0.5f)
            {
                juce::Path value;
                value.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                                    juce::degreesToRadians(juce::jmin(from, angle)),
                                    juce::degreesToRadians(juce::jmax(from, angle)), true);
                g.setColour(arc);
                g.strokePath(value, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved,
                                                        juce::PathStrokeType::rounded));
            }
        }

        // body: knurled ring, domed cap, cream pointer
        const auto body = juce::Rectangle<float>(diameter, diameter).withCentre(centre);
        for (int i = 4; i >= 1; --i)
        {
            g.setColour(juce::Colours::black.withAlpha(0.16f));
            g.fillEllipse(body.expanded(radius * 0.05f * static_cast<float>(i))
                              .translated(0.0f, radius * 0.05f * static_cast<float>(i)));
        }
        juce::ColourGradient ring(juce::Colour(0xff3a3834), centre.x - radius * 0.5f, centre.y - radius * 0.7f,
                                  juce::Colour(0xff0b0a09), centre.x + radius * 0.6f, centre.y + radius * 0.8f, true);
        g.setGradientFill(ring);
        g.fillEllipse(body);

        for (int i = 0; i < 40; ++i)
        {
            const auto a = juce::MathConstants<float>::twoPi * static_cast<float>(i) / 40.0f;
            const auto lift = 0.5f + 0.5f * std::cos(a + juce::MathConstants<float>::pi * 0.75f);
            g.setColour(juce::Colours::white.withAlpha(0.07f * lift));
            g.drawLine({ centre + juce::Point<float>(std::sin(a), -std::cos(a)) * (radius * 0.80f),
                         centre + juce::Point<float>(std::sin(a), -std::cos(a)) * (radius * 0.99f) },
                       radius * 0.05f);
        }

        const auto capArea = body.reduced(radius * 0.28f);
        juce::ColourGradient dome(cap.brighter(0.55f), capArea.getCentreX() - capArea.getWidth() * 0.15f,
                                  capArea.getY(), cap.darker(0.62f), capArea.getCentreX(),
                                  capArea.getBottom(), true);
        g.setGradientFill(dome);
        g.fillEllipse(capArea);
        g.setColour(juce::Colours::black.withAlpha(0.40f));
        g.drawEllipse(capArea, radius * 0.05f);
        g.setColour(juce::Colours::white.withAlpha(0.18f));
        g.drawEllipse(capArea.reduced(radius * 0.06f).translated(0.0f, -radius * 0.04f), radius * 0.035f);

        const auto tip = polar(centre, angle, radius * 0.84f);
        const auto tail = polar(centre, angle, radius * 0.30f);
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.drawLine({ tail.translated(0.0f, 1.5f), tip.translated(0.0f, 1.5f) }, radius * 0.10f);
        g.setColour(juce::Colour(0xfff4efe2));
        g.drawLine({ tail, tip }, radius * 0.075f);

        if (isMouseOverOrDragging())
        {
            g.setColour(juce::Colours::white.withAlpha(0.08f));
            g.fillEllipse(capArea);
        }
    }

private:
    float diameter;
    juce::Colour cap, arc;
    int ticks, major;
    bool bipolar;
    float legendSize = 13.0f;
    std::vector<Legend> legends;
};

// ------------------------------------------------------------------ buttons

/** Rectangular panel key used by the lower strip (18 dB, ON, 2x and so on). */
class PanelKey final : public juce::Component
{
public:
    PanelKey(juce::String label, juce::Colour litColour) : text(std::move(label)), lit(litColour) {}

    std::function<void()> onClick;
    std::function<bool()> isOn;

    // Hardware keys switch on the press, not the release: acting here makes
    // the response feel immediate.
    void mouseDown(const juce::MouseEvent&) override { down = true; if (onClick) onClick(); repaint(); }
    void mouseUp(const juce::MouseEvent&) override { down = false; repaint(); }
    void mouseEnter(const juce::MouseEvent&) override { setMouseCursor(juce::MouseCursor::PointingHandCursor); repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }

    /** Repaints only when the parameter behind the key has changed (preset
        load, host automation, undo). */
    void refresh()
    {
        const auto on = isOn && isOn();
        if (on != shownOn) { shownOn = on; repaint(); }
    }

    void paint(juce::Graphics& g) override
    {
        const auto area = getLocalBounds().toFloat().reduced(0.5f);
        const auto on = isOn && isOn();
        shownOn = on;

        if (on)
        {
            g.setColour(lit.withAlpha(0.45f));
            g.fillRoundedRectangle(area.expanded(3.0f), 7.0f);
            juce::ColourGradient face(lit.brighter(0.35f), area.getCentreX(), area.getY(),
                                      lit.darker(0.25f), area.getCentreX(), area.getBottom(), false);
            g.setGradientFill(face);
        }
        else
        {
            juce::ColourGradient face(juce::Colour(0xff24211d), area.getCentreX(), area.getY(),
                                      juce::Colour(0xff131211), area.getCentreX(), area.getBottom(), false);
            g.setGradientFill(face);
        }
        g.fillRoundedRectangle(area, 5.0f);

        g.setColour(juce::Colours::white.withAlpha(on ? 0.40f : 0.07f));
        g.drawLine(area.getX() + 5.0f, area.getY() + 1.0f, area.getRight() - 5.0f, area.getY() + 1.0f, 1.2f);
        g.setColour(juce::Colours::black.withAlpha(0.75f));
        g.drawRoundedRectangle(area, 5.0f, 1.0f);
        if (! on && isMouseOver())
        {
            g.setColour(juce::Colours::white.withAlpha(0.06f));
            g.fillRoundedRectangle(area, 5.0f);
        }

        g.setFont(labelFont(juce::jmin(15.5f, area.getHeight() * 0.52f), true, 0.94f));
        g.setColour(on ? juce::Colour(0xff2a1406) : juce::Colour(0xffd8d1bc));
        g.drawText(text, area.translated(0.0f, down ? 1.0f : 0.0f), juce::Justification::centred, false);
    }

private:
    juce::String text;
    juce::Colour lit;
    bool down = false;
    bool shownOn = false;
};

/** Illuminated lamp, square or round, that also toggles its parameter. */
class PanelLamp final : public juce::Component
{
public:
    /** Click target reaches this far beyond the drawn lamp on every side. */
    static constexpr int hitMargin = 12;

    PanelLamp(juce::Colour colour, bool square, bool interactive)
        : tint(colour), isSquare(square) { setInterceptsMouseClicks(interactive, false); }

    std::function<void()> onClick;
    std::function<bool()> isOn;

    /** Places the lamp so that its visible face is exactly the given rectangle. */
    void setLampBounds(int x, int y, int w, int h)
    {
        setBounds(x - hitMargin, y - hitMargin, w + 2 * hitMargin, h + 2 * hitMargin);
    }

    void mouseDown(const juce::MouseEvent&) override { if (onClick) onClick(); repaint(); }
    void mouseEnter(const juce::MouseEvent&) override { setMouseCursor(juce::MouseCursor::PointingHandCursor); }

    void refresh()
    {
        const auto on = isOn == nullptr || isOn();
        if (on != shownOn) { shownOn = on; repaint(); }
    }

    void paint(juce::Graphics& g) override
    {
        const auto area = getLocalBounds().toFloat().reduced(static_cast<float>(hitMargin));
        const auto on = isOn == nullptr || isOn();
        shownOn = on;
        const auto glow = on ? 1.0f : 0.18f;

        g.setColour(tint.withAlpha(0.45f * glow));
        if (isSquare) g.fillRoundedRectangle(area.expanded(area.getWidth() * 0.45f), 5.0f);
        else          g.fillEllipse(area.expanded(area.getWidth() * 0.55f));

        juce::ColourGradient lamp(tint.brighter(0.75f), area.getX() + area.getWidth() * 0.35f,
                                  area.getY() + area.getHeight() * 0.35f,
                                  tint.darker(on ? 0.35f : 0.80f), area.getRight(), area.getBottom(), true);
        g.setGradientFill(lamp);
        if (isSquare) g.fillRoundedRectangle(area, 3.0f);
        else          g.fillEllipse(area);

        g.setColour(juce::Colours::black.withAlpha(0.75f));
        if (isSquare) g.drawRoundedRectangle(area, 3.0f, 1.6f);
        else          g.drawEllipse(area, 1.2f);
    }

private:
    juce::Colour tint;
    bool isSquare;
    bool shownOn = true;
};

/** CLEAN / CONSOLE / TUBE / TAPE selector drawn as a printed row of lamps. */
class RadioRow final : public juce::Component
{
public:
    RadioRow(juce::StringArray items, juce::Colour litColour)
        : options(std::move(items)), lit(litColour) {}

    std::function<void(int)> onSelect;
    std::function<int()> selectedIndex;

    void mouseDown(const juce::MouseEvent& e) override
    {
        const auto slot = getWidth() / juce::jmax(1, options.size());
        const auto index = juce::jlimit(0, options.size() - 1, e.getPosition().x / juce::jmax(1, slot));
        if (onSelect) onSelect(index);
        repaint();
    }
    void mouseEnter(const juce::MouseEvent&) override { setMouseCursor(juce::MouseCursor::PointingHandCursor); }

    void refresh()
    {
        const auto index = selectedIndex ? selectedIndex() : -1;
        if (index != shownIndex) { shownIndex = index; repaint(); }
    }

    void paint(juce::Graphics& g) override
    {
        const auto slot = static_cast<float>(getWidth()) / static_cast<float>(juce::jmax(1, options.size()));
        const auto selected = selectedIndex ? selectedIndex() : 0;

        // hairline linking the stations, as printed on the panel
        g.setColour(juce::Colours::white.withAlpha(0.12f));
        g.drawLine(slot * 0.5f, 22.0f, static_cast<float>(getWidth()) - slot * 0.5f, 22.0f, 1.0f);

        for (int i = 0; i < options.size(); ++i)
        {
            const auto x = slot * (static_cast<float>(i) + 0.5f);
            const auto on = i == selected;

            g.setFont(labelFont(13.5f, true, 0.92f));
            g.setColour(on ? cAmber : cText);
            g.drawText(options[i], juce::Rectangle<float>(slot, 14.0f).withCentre({ x, 7.0f }),
                       juce::Justification::centred, false);

            const auto lamp = juce::Rectangle<float>(8.0f, 8.0f).withCentre({ x, 23.0f });
            if (on)
            {
                g.setColour(cAmber.withAlpha(0.55f));
                g.fillEllipse(lamp.expanded(5.0f));
                g.setColour(cAmber.brighter(0.4f));
            }
            else g.setColour(juce::Colour(0xff0d0c0b));
            g.fillEllipse(lamp);
            g.setColour(juce::Colours::black.withAlpha(0.7f));
            g.drawEllipse(lamp, 1.0f);
        }
    }

private:
    juce::StringArray options;
    juce::Colour lit;
    int shownIndex = -1;
};

// ------------------------------------------------------------------- meters

/** Cream ballistic VU with the printed scale from the reference face. */
class HeritageVu final : public juce::Component, private juce::Timer
{
public:
    explicit HeritageVu(std::function<float()> source) : level(std::move(source))
    {
        setInterceptsMouseClicks(false, false);
        startTimerHz(30);
    }

    /** Compressor face: 0 to 20 dB of gain reduction, needle resting at the left. */
    void setGainReductionScale(bool shouldShow) { gainReduction = shouldShow; repaint(); }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour(juce::Colour(0xff141311));
        g.fillRoundedRectangle(bounds, 8.0f);
        g.setColour(juce::Colour(0xff2d2823));
        g.drawRoundedRectangle(bounds.reduced(1.0f), 8.0f, 2.0f);

        auto face = bounds.reduced(12.0f, 10.0f);
        juce::ColourGradient plate(juce::Colour(0xffe8d9ae), face.getCentreX(), face.getY(),
                                   juce::Colour(0xffcdb98a), face.getCentreX(), face.getBottom(), false);
        g.setGradientFill(plate);
        g.fillRoundedRectangle(face, 6.0f);

        juce::Graphics::ScopedSaveState clip(g);
        g.reduceClipRegion(face.toNearestInt());

        // geometry follows the reference: pivot below the plate, shallow sweep
        const auto pivot = juce::Point<float>(face.getCentreX(), face.getY() + face.getHeight() * 1.27f);
        const auto radius = face.getHeight() * 0.99f;
        const auto redFrom = gainReduction ? 60.0f : 30.0f;

        static const std::array<std::pair<const char*, float>, 11> levelMarks {{
            { "-20", -52.0f }, { "-10", -32.0f }, { "-7", -24.0f }, { "-5", -16.0f }, { "-3", -8.0f },
            { "-2", -2.0f }, { "-1", 5.0f }, { "0", 12.0f }, { "+1", 22.0f }, { "+2", 34.0f }, { "+3", 48.0f } }};
        static const std::array<std::pair<const char*, float>, 11> reductionMarks {{
            { "0", -52.0f }, { "2", -42.0f }, { "4", -32.0f }, { "6", -22.0f }, { "8", -12.0f },
            { "10", -2.0f }, { "12", 8.0f }, { "14", 18.0f }, { "16", 28.0f }, { "18", 38.0f }, { "20", 48.0f } }};
        const auto& marks = gainReduction ? reductionMarks : levelMarks;

        juce::Path scale;
        scale.addCentredArc(pivot.x, pivot.y, radius, radius, 0.0f,
                            juce::degreesToRadians(-52.0f), juce::degreesToRadians(redFrom), true);
        g.setColour(juce::Colour(0xff2a2118));
        g.strokePath(scale, juce::PathStrokeType(2.0f));

        if (! gainReduction)
        {
            juce::Path red;
            red.addCentredArc(pivot.x, pivot.y, radius, radius, 0.0f,
                              juce::degreesToRadians(redFrom), juce::degreesToRadians(52.0f), true);
            g.setColour(juce::Colour(0xffc9331f));
            g.strokePath(red, juce::PathStrokeType(4.0f));
        }

        for (const auto& [text, angle] : marks)
        {
            const auto isRed = angle >= redFrom;
            g.setColour(isRed ? juce::Colour(0xffc9331f) : juce::Colour(0xff2a2118));
            g.drawLine({ polar(pivot, angle, radius), polar(pivot, angle, radius + 8.0f) }, 1.5f);
            g.setFont(labelFont(13.5f, true, 0.95f));
            g.setColour(isRed ? juce::Colour(0xffc9331f) : juce::Colour(0xff3a2e1e));
            g.drawText(text, juce::Rectangle<float>(30.0f, 14.0f).withCentre(polar(pivot, angle, radius + 21.0f)),
                       juce::Justification::centred, false);
        }

        g.setColour(juce::Colour(0xff2a2118));
        g.setFont(labelFont(gainReduction ? 16.0f : 22.0f, true, 0.95f));
        g.drawText(gainReduction ? "GAIN REDUCTION" : "VU",
                   face.withHeight(26.0f).translated(0.0f, face.getHeight() * 0.42f),
                   juce::Justification::centred, false);
        g.setColour(juce::Colour(0xff3a2e1e));
        g.setFont(labelFont(9.5f, true, 1.0f));
        g.drawText(gainReduction ? "dB" : "AMANORSAC STUDIO",
                   face.withHeight(12.0f).translated(0.0f, face.getHeight() * 0.66f),
                   juce::Justification::centred, false);

        const auto angle = gainReduction
            ? juce::jmap(juce::jlimit(0.0f, 1.0f, needle), 0.0f, 1.0f, -52.0f, 48.0f)
            : juce::jmap(juce::jlimit(0.0f, 1.06f, needle), 0.0f, 1.0f, -40.0f, 48.0f);
        g.setColour(juce::Colour(0xff1a140c));
        g.drawLine({ polar(pivot, angle, radius * 0.28f), polar(pivot, angle, radius + 2.0f) }, 2.0f);
        g.setColour(juce::Colour(0xff111111));
        g.fillEllipse(juce::Rectangle<float>(12.0f, 12.0f).withCentre({ pivot.x, face.getBottom() }));
    }

private:
    void timerCallback() override
    {
        const auto target = level ? juce::jlimit(0.0f, 1.2f, level()) : 0.0f;
        needle += (target - needle) * (target > needle ? 0.32f : 0.11f);
        repaint();
    }

    std::function<float()> level;
    bool gainReduction = false;
    float needle = 0.0f;
};

/** Twin LED ladders with the printed dB legend between them. */
class HeritageLedMeter final : public juce::Component, private juce::Timer
{
public:
    HeritageLedMeter(std::function<float()> l, std::function<float()> r)
        : left(std::move(l)), right(std::move(r))
    {
        setInterceptsMouseClicks(false, false);
        startTimerHz(30);
    }

    /** Ladder in the product colour (pale below, saturated at the top) instead
        of the green / amber / red stack. */
    void setAccent(juce::Colour colour) { accent = colour; useAccent = true; repaint(); }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour(juce::Colour(0xff0c0b0a));
        g.fillRoundedRectangle(bounds, 8.0f);
        g.setColour(juce::Colour(0xff2a2320));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 8.0f, 1.0f);

        static const std::array<const char*, 9> scale { "+18", "+12", "+6", "0", "-6", "-12", "-18", "-30", "-60" };
        const auto ladderTop = bounds.getY() + 14.0f;
        const auto ladderHeight = bounds.getHeight() - 42.0f;

        g.setFont(labelFont(14.0f, true, 0.95f));
        for (size_t i = 0; i < scale.size(); ++i)
        {
            const auto t = static_cast<float>(i) / static_cast<float>(scale.size() - 1);
            g.setColour(juce::Colour(0xffe8e2d0));
            g.drawText(scale[i],
                       juce::Rectangle<float>(bounds.getWidth(), 14.0f)
                           .withCentre({ bounds.getCentreX(), ladderTop + t * (ladderHeight - 14.0f) + 7.0f }),
                       juce::Justification::centred, false);
        }

        drawLadder(g, juce::Rectangle<float>(bounds.getX() + 14.0f, ladderTop, 20.0f, ladderHeight), shownL, useAccent ? &accent : nullptr);
        drawLadder(g, juce::Rectangle<float>(bounds.getRight() - 34.0f, ladderTop, 20.0f, ladderHeight), shownR, useAccent ? &accent : nullptr);

        g.setColour(juce::Colour(0xffddd6c2));
        g.setFont(labelFont(15.0f, true, 0.95f));
        g.drawText("L", juce::Rectangle<float>(20.0f, 16.0f)
                            .withCentre({ bounds.getX() + 24.0f, bounds.getBottom() - 14.0f }),
                   juce::Justification::centred, false);
        g.drawText("R", juce::Rectangle<float>(20.0f, 16.0f)
                            .withCentre({ bounds.getRight() - 24.0f, bounds.getBottom() - 14.0f }),
                   juce::Justification::centred, false);
    }

private:
    static void drawLadder(juce::Graphics& g, juce::Rectangle<float> column, float level,
                           const juce::Colour* accent = nullptr)
    {
        const int segments = accent != nullptr ? 26 : 14;
        const auto gap = accent != nullptr ? 3.0f : 5.0f;
        const auto cell = (column.getHeight() - gap * static_cast<float>(segments - 1)) / static_cast<float>(segments);
        for (int i = 0; i < segments; ++i)
        {
            const auto fromTop = static_cast<float>(i) / static_cast<float>(segments - 1);
            const auto lit = level >= 1.0f - fromTop;
            const auto colour = accent != nullptr
                ? accent->interpolatedWith(juce::Colour(0xffb9b4d8), juce::jlimit(0.0f, 1.0f, (fromTop - 0.25f) * 1.2f))
                : i < 2 ? juce::Colour(0xffe03a25)
                : i < 5 ? juce::Colour(0xfff2b030)
                        : juce::Colour(0xff37d63a);
            const auto cellArea = juce::Rectangle<float>(column.getX(),
                                                         column.getY() + static_cast<float>(i) * (cell + gap),
                                                         column.getWidth(), cell);
            if (lit)
            {
                g.setColour(colour.withAlpha(0.35f));
                g.fillRoundedRectangle(cellArea.expanded(2.0f), 3.0f);
                g.setColour(colour);
            }
            else g.setColour(colour.withAlpha(0.13f));
            g.fillRoundedRectangle(cellArea, 2.0f);
        }
    }

    void timerCallback() override
    {
        const auto l = left ? juce::jlimit(0.0f, 1.0f, left()) : 0.0f;
        const auto r = right ? juce::jlimit(0.0f, 1.0f, right()) : 0.0f;
        shownL = juce::jmax(l, shownL * 0.88f);
        shownR = juce::jmax(r, shownR * 0.88f);
        repaint();
    }

    std::function<float()> left, right;
    float shownL = 0.0f, shownR = 0.0f;
    juce::Colour accent;
    bool useAccent = false;
};

// ------------------------------------------------------------------ chassis

/** The chassis every product editor is built on. It knows nothing about which
    processor is hosting it, so the same editor serves the plugin and one slot
    of the rack. */
class AnalogChassis : public juce::Component,
                      private juce::ChangeListener,
                      private juce::Timer
{
public:
    explicit AnalogChassis(ProductHost owner) : host(std::move(owner)) {}
    ~AnalogChassis() override
    {
        if (host.presetManager != nullptr) host.presetManager->removeChangeListener(this);
        removeMouseListener(&transactions);
    }

protected:
    /** Creates the preset window, previous/next keys and the seven tool keys,
        wires them to the preset manager and undo, and starts the refresh timer. */
    void buildTopBar()
    {
        if (! host.ownsTopBar) { startTimerHz(20); return; }
        // ---------------------------------------------------- top bar keys
        auto& manager = *host.presetManager;
        const std::array<std::pair<const char*, ToolIcon>, 7> toolSpecs {{
            { "A / B", ToolIcon::none }, { "COPY", ToolIcon::none }, { "PASTE", ToolIcon::none },
            { "UNDO", ToolIcon::undo }, { "REDO", ToolIcon::redo },
            { "SETUP", ToolIcon::gear }, { "MENU", ToolIcon::menu } }};
        for (size_t i = 0; i < tools.size(); ++i)
        {
            tools[i] = std::make_unique<ToolKey>(toolSpecs[i].first, toolSpecs[i].second);
            addAndMakeVisible(*tools[i]);
        }
        tools[0]->activeSlot = [&manager] { return manager.activeSlot(); };
        tools[0]->onClick = [this, &manager]
        {
            manager.toggleAB();
            flash(manager.activeSlot() == 0 ? "SNAPSHOT A" : "SNAPSHOT B");
        };
        tools[1]->onClick = [this, &manager] { manager.copyToClipboard(); flash("COPIED"); };
        tools[2]->onClick = [this, &manager]
        {
            juce::String error;
            if (manager.pasteFromClipboard(&error)) flash("PASTED"); else report(error);
        };
        tools[3]->isAvailable = [this] { return (*host.undoManager).canUndo(); };
        tools[3]->onClick = [this] { (*host.undoManager).undo(); };
        tools[4]->isAvailable = [this] { return (*host.undoManager).canRedo(); };
        tools[4]->onClick = [this] { (*host.undoManager).redo(); };
        tools[5]->onClick = [this] { showSetupMenu(); };
        tools[6]->onClick = [this] { showMainMenu(); };

        presetPrevious = std::make_unique<NavKey>(true);
        presetNext = std::make_unique<NavKey>(false);
        presetWindow = std::make_unique<PresetWindow>();
        presetWindow->caption = [&manager] { return manager.currentName(); };
        presetWindow->isModified = [&manager] { return manager.isModified(); };
        presetWindow->onClick = [this] { showPresetMenu(); };
        presetPrevious->onClick = [&manager] { manager.loadPrevious(); };
        presetNext->onClick = [&manager] { manager.loadNext(); };
        addAndMakeVisible(*presetPrevious);
        addAndMakeVisible(*presetWindow);
        addAndMakeVisible(*presetNext);

        manager.addChangeListener(this);
        // Every click on the panel opens a fresh undo step, so UNDO walks back
        // one gesture at a time instead of one session at a time.
        addMouseListener(&transactions, true);
        startTimerHz(20);
    }

    /** Places the preset group at navX and right-aligns the tool keys to toolbarRight. */
    void layoutTopBar(int navX, int y, int height, int toolbarRight,
                      std::array<int, 7> widths = { 70, 70, 76, 54, 54, 54, 54 })
    {
        if (! host.ownsTopBar || presetWindow == nullptr) return;
        presetPrevious->setBounds(navX, y, 46, height);
        presetWindow->setBounds(navX + 46, y, 290, height);
        presetNext->setBounds(navX + 336, y, 46, height);
        int x = toolbarRight;
        for (int i = static_cast<int>(tools.size()) - 1; i >= 0; --i)
        {
            const auto width = widths[static_cast<size_t>(i)];
            x -= width;
            tools[static_cast<size_t>(i)]->setBounds(x, y, width - 6, height);
        }
    }

    /** Products with controls outside the factories refresh them here. */
    virtual void refreshExtras() {}

    // ------------------------------------------------------ control factories

    /** Evenly spaced printed legends. The mark at twelve o'clock is dropped
        unless it is the true centre of a bipolar scale: on a one-sided scale a
        number sitting directly under the caption reads as a value readout and
        contradicts where the pointer actually is. */
    static std::vector<HeritageKnob::Legend> evenly(const juce::StringArray& items, float radiusOffset,
                                                    bool keepCentre = false)
    {
        std::vector<HeritageKnob::Legend> legends;
        const auto centre = items.size() / 2;
        for (int i = 0; i < items.size(); ++i)
        {
            if (! keepCentre && items.size() % 2 == 1 && i == centre) continue;
            legends.push_back({ items[i],
                                startDegrees + sweepDegrees * static_cast<float>(i)
                                    / static_cast<float>(juce::jmax(1, items.size() - 1)),
                                radiusOffset });
        }
        return legends;
    }

    static void place(const std::unique_ptr<HeritageKnob>& knob, juce::Point<int> origin,
                      int cx, int cy, int size)
    {
        if (knob == nullptr) return;
        // Large scales print two-digit signed legends, which need more apron
//        than a small knob, or the outer numbers lose a character.
        const auto box = size + (size >= 66.0f ? 96 : 76);
        knob->setBounds(origin.x + cx - box / 2, origin.y + cy - box / 2, box, box);
    }

    void addKnob(std::unique_ptr<HeritageKnob>& knob, const juce::String& parameterId, float size,
                 juce::Colour cap, juce::Colour arc, int ticks, int majorEvery,
                 std::vector<HeritageKnob::Legend> legends, float legendSize)
    {
        const auto* descriptor = find(*host.spec, parameterId);
        if (descriptor == nullptr) return;

        knob = std::make_unique<HeritageKnob>(size, cap, arc, ticks, majorEvery,
                                              descriptor->minimum < 0.0f);
        knob->setLegends(std::move(legends));
        knob->setLegendSize(legendSize);
        knob->setDoubleClickReturnValue(false, descriptor->defaultValue);   // stored default; double-click types instead
        knob->setTitle(descriptor->name);
        knob->setDescription(descriptor->rangeText + "; double-click to type a value, Ctrl-click restores "
                             + descriptor->defaultText);
        addAndMakeVisible(*knob);
        attachments.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            *host.state, host.id(parameterId), *knob));
    }

    void addLamp(std::unique_ptr<PanelLamp>& lamp, const juce::String& parameterId,
                 juce::Colour colour, bool interactive)
    {
        lamp = std::make_unique<PanelLamp>(colour, true, interactive);
        auto& state = *host.state;
        const auto id = host.id(parameterId);
        lamp->isOn = [&state, id]
        {
            const auto* v = state.getRawParameterValue(id);
            return v != nullptr && v->load() > 0.5f;
        };
        if (interactive)
            lamp->onClick = [&state, id]
            {
                if (auto* p = state.getParameter(id))
                {
                    const auto next = p->getValue() > 0.5f ? 0.0f : 1.0f;
                    p->beginChangeGesture(); p->setValueNotifyingHost(next); p->endChangeGesture();
                }
            };
        addAndMakeVisible(*lamp);
        lamps.push_back(lamp.get());
    }

    void addKey(std::unique_ptr<PanelKey>& key, const juce::String& parameterId,
                const juce::String& text, juce::Colour lit)
    {
        key = std::make_unique<PanelKey>(text, lit);
        auto& state = *host.state;
        const auto id = host.id(parameterId);
        key->isOn = [&state, id]
        {
            const auto* v = state.getRawParameterValue(id);
            return v != nullptr && v->load() > 0.5f;
        };
        key->onClick = [&state, id]
        {
            if (auto* p = state.getParameter(id))
            {
                const auto next = p->getValue() > 0.5f ? 0.0f : 1.0f;
                p->beginChangeGesture(); p->setValueNotifyingHost(next); p->endChangeGesture();
            }
        };
        addAndMakeVisible(*key);
        keys.push_back(key.get());
    }

    void addChoiceKey(std::unique_ptr<PanelKey>& key, const juce::String& parameterId,
                      int index, const juce::String& text)
    {
        key = std::make_unique<PanelKey>(text, keyAccent);
        auto& state = *host.state;
        const auto id = host.id(parameterId);
        key->isOn = [&state, id, index]
        {
            const auto* v = state.getRawParameterValue(id);
            return v != nullptr && juce::roundToInt(v->load()) == index;
        };
        key->onClick = [&state, id, index]
        {
            if (auto* p = state.getParameter(id))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(index)));
                p->endChangeGesture();
            }
        };
        addAndMakeVisible(*key);
        keys.push_back(key.get());
    }


    // ------------------------------------------------------- presets & menus

    void changeListenerCallback(juce::ChangeBroadcaster*) override
    {
        presetWindow->repaint();
        tools[0]->repaint();
    }

    void timerCallback() override
    {
        if (statusUntil != 0 && juce::Time::currentTimeMillis() > statusUntil)
        {
            statusUntil = 0;
            presetWindow->status.clear();
            presetWindow->repaint();
        }
        tools[3]->repaint();   // undo / redo availability
        tools[4]->repaint();

        // keys and lamps follow the parameters wherever the change came from
        for (auto* key : keys) key->refresh();
        for (auto* lamp : lamps) lamp->refresh();
        for (auto* row : rows) row->refresh();
        refreshExtras();
    }

    void flash(const juce::String& message)
    {
        presetWindow->status = message;
        statusUntil = juce::Time::currentTimeMillis() + 1400;
        presetWindow->repaint();
    }

    void report(const juce::String& error)
    {
        if (error.isEmpty()) return;
        juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                         .withIconType(juce::MessageBoxIconType::WarningIcon)
                                         .withTitle((*host.spec).displayName)
                                         .withMessage(error)
                                         .withButton("OK")
                                         .withAssociatedComponent(this),
                                     nullptr);
    }

    void showPresetMenu()
    {
        auto& manager = *host.presetManager;
        const auto& list = manager.presets();
        const auto userPreset = manager.currentIsUserPreset();

        juce::PopupMenu menu;
        menu.setLookAndFeel(&menuLook);
        // Grouped the way an engineer reaches for a sound: pick the source
        // first, then the preset. Default stays at the top on its own.
        menu.addSectionHeader("PRESETS");
        if (! list.empty())
            menu.addItem(1000, list[0].name, true, manager.currentIndex() == 0);
        menu.addSeparator();

        for (const auto& category : manager.categories())
        {
            juce::PopupMenu group;
            auto holdsCurrent = false;
            for (size_t i = 1; i < list.size(); ++i)
            {
                const auto& preset = list[i];
                const auto its = preset.category.isNotEmpty() ? preset.category
                               : preset.builtIn ? juce::String("Utility")
                                                : juce::String(presets::PresetManager::userCategory);
                if (its != category) continue;
                const auto chosen = static_cast<int>(i) == manager.currentIndex();
                holdsCurrent = holdsCurrent || chosen;
                group.addItem(1000 + static_cast<int>(i), preset.name, true, chosen);
            }
            if (group.getNumItems() > 0)
                menu.addSubMenu(category, group, true, nullptr, holdsCurrent, 0);
        }
        if (list.size() == 1)
            menu.addItem(2, "No presets installed", false);
        menu.addSeparator();
        menu.addItem(10, "Save as new preset...");
        menu.addItem(11, "Save (overwrite " + manager.currentName() + ")", userPreset);
        menu.addItem(12, "Rename...", userPreset);
        menu.addItem(13, "Delete...", userPreset);
        menu.addSeparator();
        menu.addItem(20, "Copy settings");
        menu.addItem(21, "Paste settings");
        menu.addSeparator();
        menu.addItem(30, "Reveal preset folder");
        menu.addItem(31, "Rescan folder");

        juce::Component::SafePointer<AnalogChassis> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(*presetWindow).withMinimumWidth(290),
                           [safe](int result) { if (safe != nullptr) safe->handlePresetMenu(result); });
    }

    void handlePresetMenu(int result)
    {
        auto& manager = *host.presetManager;
        juce::String error;
        if (result >= 1000)
        {
            if (manager.load(result - 1000)) flash("LOADED");
            else report("Could not read that preset file.");
            return;
        }
        switch (result)
        {
            case 10: showSaveDialog(); break;
            case 11:
            {
                const auto info = manager.presets()[static_cast<size_t>(manager.currentIndex())];
                if (manager.save(info.name, info.author, info.tags, &error)) flash("SAVED"); else report(error);
                break;
            }
            case 12: showRenameDialog(); break;
            case 13: confirmDelete(); break;
            case 20: manager.copyToClipboard(); flash("COPIED"); break;
            case 21: if (manager.pasteFromClipboard(&error)) flash("PASTED"); else report(error); break;
            case 30: manager.userDirectory().createDirectory(); manager.userDirectory().revealToUser(); break;
            case 31: manager.rescan(); flash("RESCANNED"); break;
            default: break;
        }
    }

    void showSaveDialog()
    {
        auto& manager = *host.presetManager;
        const auto userPreset = manager.currentIsUserPreset();
        const auto current = manager.presets()[static_cast<size_t>(manager.currentIndex())];

        auto* alert = new juce::AlertWindow("Save preset",
                                            "Name the preset. Tags are optional and help you find it later.",
                                            juce::MessageBoxIconType::NoIcon, this);
        alert->setLookAndFeel(&menuLook);
        alert->addTextEditor("name", userPreset ? current.name : juce::String(), "Name");
        alert->addTextEditor("author", lastAuthor.isNotEmpty() ? lastAuthor : current.author, "Author");
        for (const auto& key : presets::PresetManager::tagKeys())
            alert->addTextEditor(key.toLowerCase(), userPreset ? current.tags.getValue(key, {}) : juce::String(), key);
        alert->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
        alert->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

        juce::Component::SafePointer<AnalogChassis> safe(this);
        alert->enterModalState(true, juce::ModalCallbackFunction::create([safe, alert](int result)
        {
            if (result != 1 || safe == nullptr) return;
            juce::StringPairArray tags;
            for (const auto& key : presets::PresetManager::tagKeys())
                tags.set(key, alert->getTextEditorContents(key.toLowerCase()).trim());
            lastAuthor = alert->getTextEditorContents("author").trim();
            juce::String error;
            if (safe->host.presetManager->save(alert->getTextEditorContents("name"), lastAuthor, tags, &error))
                safe->flash("SAVED");
            else
                safe->report(error);
        }), true);
    }

    void showRenameDialog()
    {
        auto& manager = *host.presetManager;
        const auto index = manager.currentIndex();
        auto* alert = new juce::AlertWindow("Rename preset", "", juce::MessageBoxIconType::NoIcon, this);
        alert->setLookAndFeel(&menuLook);
        alert->addTextEditor("name", manager.currentName(), "New name");
        alert->addButton("Rename", 1, juce::KeyPress(juce::KeyPress::returnKey));
        alert->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

        juce::Component::SafePointer<AnalogChassis> safe(this);
        alert->enterModalState(true, juce::ModalCallbackFunction::create([safe, alert, index](int result)
        {
            if (result != 1 || safe == nullptr) return;
            juce::String error;
            if (safe->host.presetManager->rename(index, alert->getTextEditorContents("name"), &error))
                safe->flash("RENAMED");
            else
                safe->report(error);
        }), true);
    }

    void confirmDelete()
    {
        auto& manager = *host.presetManager;
        const auto index = manager.currentIndex();
        juce::Component::SafePointer<AnalogChassis> safe(this);
        juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                         .withIconType(juce::MessageBoxIconType::QuestionIcon)
                                         .withTitle("Delete preset")
                                         .withMessage("Delete " + manager.currentName() + "? The file is removed from disk.")
                                         .withButton("Delete")
                                         .withButton("Cancel")
                                         .withAssociatedComponent(this),
                                     [safe, index](int result)
                                     {
                                         if (result != 1 || safe == nullptr) return;
                                         juce::String error;
                                         if (safe->host.presetManager->remove(index, &error)) safe->flash("DELETED");
                                         else safe->report(error);
                                     });
    }

    void showSetupMenu()
    {
        auto& undo = (*host.undoManager);
        juce::PopupMenu menu;
        menu.setLookAndFeel(&menuLook);
        menu.addSectionHeader("SETUP");
        menu.addItem(1, "Reset every control to Default");
        menu.addSeparator();
        menu.addItem(2, "Store current settings as A");
        menu.addItem(3, "Store current settings as B");
        menu.addSeparator();
        menu.addItem(4, undo.canUndo() ? "Undo " + undo.getUndoDescription() : juce::String("Undo"), undo.canUndo());
        menu.addItem(5, undo.canRedo() ? "Redo " + undo.getRedoDescription() : juce::String("Redo"), undo.canRedo());

        juce::Component::SafePointer<AnalogChassis> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(*tools[5]).withMinimumWidth(240),
                           [safe](int result)
                           {
                               if (safe == nullptr) return;
                               auto& manager = *safe->host.presetManager;
                               switch (result)
                               {
                                   case 1: manager.loadDefault(); safe->flash("DEFAULT"); break;
                                   case 2: manager.captureSlot(0); safe->flash("STORED A"); break;
                                   case 3: manager.captureSlot(1); safe->flash("STORED B"); break;
                                   case 4: safe->host.undoManager->undo(); break;
                                   case 5: safe->host.undoManager->redo(); break;
                                   default: break;
                               }
                           });
    }

    void showMainMenu()
    {
        juce::PopupMenu menu;
        menu.setLookAndFeel(&menuLook);
        menu.addSectionHeader((*host.spec).displayName.toUpperCase());
        menu.addItem(1, "About...");
        menu.addSeparator();
        menu.addItem(2, "Reveal preset folder");
        menu.addItem(3, "Rescan presets");

        juce::Component::SafePointer<AnalogChassis> safe(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(*tools[6]).withMinimumWidth(220),
                           [safe](int result)
                           {
                               if (safe == nullptr) return;
                               auto& manager = *safe->host.presetManager;
                               switch (result)
                               {
                                   case 1: safe->showAbout(); break;
                                   case 2: manager.userDirectory().createDirectory(); manager.userDirectory().revealToUser(); break;
                                   case 3: manager.rescan(); safe->flash("RESCANNED"); break;
                                   default: break;
                               }
                           });
    }

    void showAbout()
    {
       #ifdef JucePlugin_VersionString
        const juce::String version(JucePlugin_VersionString);
       #else
        const juce::String version("development build");
       #endif
        const auto message = (*host.spec).displayName + "  v" + version
            + "\nAmanorsac Studio Mixing Suite, Analog series"
            + "\nSample rate: " + juce::String(host.sampleRate(), 0) + " Hz"
            + "\nReported latency: " + juce::String(host.latency()) + " samples"
            + "\nPresets: " + host.presetManager->userDirectory().getFullPathName();
        juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                         .withIconType(juce::MessageBoxIconType::InfoIcon)
                                         .withTitle("About")
                                         .withMessage(message)
                                         .withButton("OK")
                                         .withAssociatedComponent(this),
                                     nullptr);
    }

    struct TransactionListener final : juce::MouseListener
    {
        explicit TransactionListener(juce::UndoManager& target) : undo(target) {}
        void mouseDown(const juce::MouseEvent&) override { undo.beginNewTransaction(); }
        juce::UndoManager& undo;
    };


    ProductHost host;
    juce::Colour keyAccent { cAmber };   // lit colour of choice keys, per product
    PrintedLogo brandPrint { brandLogoImage() };
    MenuLook menuLook;
    TransactionListener transactions { *host.undoManager };
    juce::int64 statusUntil = 0;

    std::array<std::unique_ptr<ToolKey>, 7> tools;
    std::unique_ptr<NavKey> presetPrevious, presetNext;
    std::unique_ptr<PresetWindow> presetWindow;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> attachments;

    // raw views of every key, lamp and radio row so the timer can refresh them
    std::vector<PanelKey*> keys;
    std::vector<PanelLamp*> lamps;
    std::vector<RadioRow*> rows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnalogChassis)
};
}
