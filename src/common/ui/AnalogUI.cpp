#include "AnalogUI.h"
#include "BinaryData.h"

#include <array>
#include <cmath>

namespace amanorsac::analog
{
namespace
{
constexpr float rotaryStart = juce::MathConstants<float>::pi * 1.24f;
constexpr float rotaryEnd = juce::MathConstants<float>::pi * 2.76f;

juce::Point<float> polar(juce::Point<float> centre, float angle, float radius)
{
    return centre + juce::Point<float>(std::sin(angle), -std::cos(angle)) * radius;
}

void glowLine(juce::Graphics& g, juce::Line<float> line, juce::Colour colour, float thickness)
{
    g.setColour(colour.withMultipliedAlpha(0.28f));
    g.drawLine(line, thickness * 2.6f);
    g.setColour(colour);
    g.drawLine(line, thickness);
}

void glowDot(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour)
{
    g.setColour(colour.withMultipliedAlpha(0.22f));
    g.fillEllipse(area.expanded(area.getWidth() * 0.7f));
    g.setColour(colour.withMultipliedAlpha(0.45f));
    g.fillEllipse(area.expanded(area.getWidth() * 0.3f));
    g.setColour(colour);
    g.fillEllipse(area);
}
}

// ------------------------------------------------------------------ palette

Palette Palette::forProduct(const juce::String& pluginId)
{
    Palette p;
    if (pluginId == "A03")      { p.accent = juce::Colour(0xffe0a12f); p.accentGlow = juce::Colour(0xffffc861); }
    else if (pluginId == "A04") { p.accent = juce::Colour(0xffd97b2a); p.accentGlow = juce::Colour(0xffffa457); }
    else if (pluginId == "A05") { p.accent = juce::Colour(0xffe8944a); p.accentGlow = juce::Colour(0xffffbe7d); }
    else if (pluginId == "A06") { p.accent = juce::Colour(0xff3fbf5a); p.accentGlow = juce::Colour(0xff74ef8c); }
    else if (pluginId == "A07") { p.accent = juce::Colour(0xff46b6d8); p.accentGlow = juce::Colour(0xff86e0ff); }
    else if (pluginId == "A08") { p.accent = juce::Colour(0xff5b8ce8); p.accentGlow = juce::Colour(0xff8fb6ff); }
    else if (pluginId == "A09") { p.accent = juce::Colour(0xffcf7bd6); p.accentGlow = juce::Colour(0xffefa9f5); }
    else if (pluginId == "A10") { p.accent = juce::Colour(0xffc558d8); p.accentGlow = juce::Colour(0xfff08cff); }

    // Each faceplate carries a faint wash of its own accent, which is what
    // separates the products on the anchors without breaking the family.
    p.chassis = juce::Colour(0xff22272a).interpolatedWith(p.accent, 0.022f);
    p.recess = juce::Colour(0xff111417).interpolatedWith(p.accent, 0.018f);
    p.wood = juce::Colour(0xff392519).interpolatedWith(p.accent, 0.03f);
    return p;
}

// --------------------------------------------------------------- primitives

const juce::Image& grainTexture()
{
    static const juce::Image texture = []
    {
        juce::Image image(juce::Image::ARGB, 128, 128, true);
        juce::Random random(0x5eed1201);
        for (int y = 0; y < image.getHeight(); ++y)
            for (int x = 0; x < image.getWidth(); ++x)
            {
                const auto v = static_cast<juce::uint8>(random.nextInt(256));
                image.setPixelAt(x, y, juce::Colour(v, v, v).withAlpha(0.055f));
            }
        return image;
    }();
    return texture;
}

namespace
{
/** Eden Mills is embedded with the product (CC0), so panel lettering is identical
    on every machine instead of depending on what the host has installed. */
/** First installed family from the list, so every platform gets the closest
    clean grotesque to the approved artwork instead of a fallback serif. */
juce::String firstInstalled(const juce::StringArray& candidates)
{
    static const auto installed = juce::Font::findAllTypefaceNames();
    for (const auto& name : candidates)
        if (installed.contains(name)) return name;
    return juce::Font::getDefaultSansSerifFontName();
}
}

juce::Font labelFont(float height, bool bold, float squeeze)
{
    // Panel lettering: a clean, open grotesque set slightly tracked, the way
    // the approved faceplates are lettered. Never squeezed: horizontal scaling
    // distorts glyphs.
    juce::ignoreUnused(squeeze);
    static const auto family = firstInstalled({ "Inter", "Segoe UI", "Roboto", "Helvetica Neue", "Arial" });
    // Segoe UI is drawn small for its em height, so it is set a touch larger
    // than the requested cap size to keep the same optical weight on the panel.
    // Panel captions and legends use the true bold cut so they read from
    // across the room, as printed hardware lettering does.
    const auto optical = height * 1.08f;
    auto font = juce::Font(juce::FontOptions(family, optical, bold ? juce::Font::bold : juce::Font::plain));
    return font.withExtraKerningFactor(bold ? 0.06f : 0.03f);
}

juce::Font displayFont(float height)
{
    // Engraved product name: a wide classical serif, generously tracked.
    static const auto serif = firstInstalled({ "Cinzel", "Trajan Pro", "Cambria", "Georgia", "Times New Roman" });
    return juce::Font(juce::FontOptions(serif, height, juce::Font::bold)).withExtraKerningFactor(0.16f);
}

void dropShadow(juce::Graphics& g, juce::Rectangle<float> bounds, float corner, float depth)
{
    for (int i = 6; i >= 1; --i)
    {
        const auto spread = depth * static_cast<float>(i) / 6.0f;
        g.setColour(juce::Colours::black.withAlpha(0.11f));
        g.fillRoundedRectangle(bounds.expanded(spread).translated(0.0f, spread * 0.55f), corner + spread);
    }
}

void fillPanel(juce::Graphics& g, juce::Rectangle<float> bounds, const Palette& palette,
               float corner, bool recessed)
{
    const auto base = recessed ? palette.recess : palette.chassis;
    if (! recessed) dropShadow(g, bounds, corner, 7.0f);
    juce::Path shape;
    shape.addRoundedRectangle(bounds, corner);

    juce::ColourGradient fill(base.brighter(recessed ? 0.025f : 0.065f), bounds.getCentreX(), bounds.getY(),
                              base.darker(recessed ? 0.30f : 0.18f), bounds.getCentreX(), bounds.getBottom(), false);
    g.setGradientFill(fill);
    g.fillPath(shape);

    g.saveState();
    g.reduceClipRegion(shape);
    g.setTiledImageFill(grainTexture(), 0, 0, 1.0f);
    g.fillRect(bounds);
    g.restoreState();

    // Fine horizontal brushing and a restrained top-left reflection make the
    // panel read as powder-coated metal instead of a flat coloured rectangle.
    g.saveState();
    juce::Path panelClip;
    panelClip.addRoundedRectangle(bounds, corner);
    g.reduceClipRegion(panelClip);
    for (int y = juce::roundToInt(bounds.getY()) + 3; y < juce::roundToInt(bounds.getBottom()); y += 4)
    {
        const auto wave = 0.5f + 0.5f * std::sin(static_cast<float>(y) * 0.37f);
        g.setColour(juce::Colours::white.withAlpha((recessed ? 0.008f : 0.012f) + wave * 0.012f));
        g.drawHorizontalLine(y, bounds.getX(), bounds.getRight());
    }
    juce::ColourGradient wash(juce::Colours::white.withAlpha(recessed ? 0.035f : 0.055f),
                              bounds.getX() + bounds.getWidth() * 0.14f,
                              bounds.getY() + bounds.getHeight() * 0.10f,
                              juce::Colours::transparentWhite,
                              bounds.getRight(), bounds.getBottom(), true);
    g.setGradientFill(wash);
    g.fillRoundedRectangle(bounds, corner);
    g.restoreState();

    if (recessed)
    {
        juce::ColourGradient lip(juce::Colours::black.withAlpha(0.55f), bounds.getCentreX(), bounds.getY(),
                                 juce::Colours::transparentBlack, bounds.getCentreX(),
                                 bounds.getY() + juce::jmin(18.0f, bounds.getHeight() * 0.3f), false);
        g.setGradientFill(lip);
        g.fillRoundedRectangle(bounds, corner);
        g.setColour(juce::Colours::white.withAlpha(0.05f));
        g.drawLine(bounds.getX() + corner, bounds.getBottom() - 1.0f,
                   bounds.getRight() - corner, bounds.getBottom() - 1.0f, 1.2f);
    }
    else
    {
        g.setColour(juce::Colours::white.withAlpha(0.09f));
        g.drawLine(bounds.getX() + corner, bounds.getY() + 1.0f, bounds.getRight() - corner, bounds.getY() + 1.0f, 1.2f);
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.drawLine(bounds.getX() + corner, bounds.getBottom() - 1.0f, bounds.getRight() - corner, bounds.getBottom() - 1.0f, 1.2f);
        g.setColour(palette.bronze.withAlpha(0.20f));
        g.drawRoundedRectangle(bounds.reduced(2.0f), juce::jmax(1.0f, corner - 2.0f), 0.55f);
    }
}

void strokePanel(juce::Graphics& g, juce::Rectangle<float> bounds, const Palette& palette,
                 float corner, float thickness, float alpha)
{
    g.setColour(palette.bronze.withAlpha(alpha));
    g.drawRoundedRectangle(bounds, corner, thickness);
}

void drawScrew(juce::Graphics& g, juce::Point<float> centre, float radius)
{
    const auto area = juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(centre);
    g.setColour(juce::Colours::black.withAlpha(0.55f));
    g.fillEllipse(area.translated(0.0f, radius * 0.22f).expanded(radius * 0.12f));

    juce::ColourGradient body(juce::Colour(0xff8d8878), area.getX(), area.getY(),
                              juce::Colour(0xff2b2823), area.getRight(), area.getBottom(), false);
    g.setGradientFill(body);
    g.fillEllipse(area);
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.drawEllipse(area, radius * 0.14f);

    g.setColour(juce::Colour(0xff17150f).withAlpha(0.9f));
    const auto slot = radius * 0.62f;
    g.drawLine(centre.x - slot, centre.y, centre.x + slot, centre.y, radius * 0.24f);
    g.drawLine(centre.x, centre.y - slot, centre.x, centre.y + slot, radius * 0.24f);
    g.setColour(juce::Colours::white.withAlpha(0.16f));
    g.drawEllipse(area.reduced(radius * 0.28f).translated(-radius * 0.12f, -radius * 0.12f), radius * 0.12f);
}

void drawPanelScrews(juce::Graphics& g, juce::Rectangle<float> bounds, float inset, float radius)
{
    drawScrew(g, { bounds.getX() + inset, bounds.getY() + inset }, radius);
    drawScrew(g, { bounds.getRight() - inset, bounds.getY() + inset }, radius);
    drawScrew(g, { bounds.getX() + inset, bounds.getBottom() - inset }, radius);
    drawScrew(g, { bounds.getRight() - inset, bounds.getBottom() - inset }, radius);
}

void drawWoodFrame(juce::Graphics& g, juce::Rectangle<float> bounds, const Palette& palette)
{
    juce::ColourGradient grain(palette.wood.brighter(0.16f), bounds.getX(), bounds.getY(),
                               palette.wood.darker(0.60f), bounds.getRight(), bounds.getBottom(), false);
    g.setGradientFill(grain);
    g.fillRoundedRectangle(bounds, 14.0f);

    g.saveState();
    juce::Path clip;
    clip.addRoundedRectangle(bounds, 14.0f);
    g.reduceClipRegion(clip);
    g.setTiledImageFill(grainTexture(), 0, 0, 0.8f);
    g.fillRect(bounds);
    g.restoreState();

    g.setColour(palette.accent.withAlpha(0.35f));
    g.drawRoundedRectangle(bounds.reduced(1.0f), 13.0f, 1.4f);
}

void drawSectionCaption(juce::Graphics& g, juce::Rectangle<float> row, const juce::String& text,
                        juce::Colour colour, float height)
{
    g.setColour(colour);
    g.setFont(labelFont(height, true, 0.9f));
    g.drawText(text.toUpperCase(), row, juce::Justification::centred, false);
}

void drawBrandMark(juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour barColour,
                   juce::Colour textColour)
{
    // Official lockup: a symmetric waveform mirrored about the centre line with
    // the AMANORSAC wordmark set across it and STUDIO tucked at the upper right.
    if (bounds.getWidth() <= 0.0f || bounds.getHeight() <= 0.0f) return;

    const auto centreY = bounds.getCentreY();
    const auto compact = bounds.getHeight() < 34.0f;
    const auto wordHeight = bounds.getHeight() * (compact ? 0.90f : 0.34f);

    if (compact)
    {
        // Too small for the waveform to read; the wordmark alone stays legible.
        g.setColour(textColour);
        g.setFont(labelFont(wordHeight, true, 0.98f));
        g.drawText("AMANORSAC", bounds, juce::Justification::centred, false);
        return;
    }

    // 23 bars, tallest in the middle, exactly as the brand mark is drawn
    static const std::array<float, 23> profile {
        0.16f, 0.22f, 0.34f, 0.52f, 0.74f, 1.00f, 0.86f, 0.62f, 0.94f, 0.70f, 0.48f,
        0.30f, 0.44f, 0.66f, 0.40f, 0.26f, 0.34f, 0.22f, 0.30f, 0.20f, 0.26f, 0.18f, 0.14f };

    const auto slot = bounds.getWidth() / static_cast<float>(profile.size());
    const auto barWidth = juce::jmax(1.0f, slot * 0.46f);
    const auto reach = bounds.getHeight() * 0.50f;

    for (size_t i = 0; i < profile.size(); ++i)
    {
        const auto height = reach * profile[i];
        const auto x = bounds.getX() + slot * (static_cast<float>(i) + 0.5f) - barWidth * 0.5f;
        g.setColour(barColour.withMultipliedAlpha(0.75f + 0.25f * profile[i]));
        g.fillRoundedRectangle(x, centreY - height, barWidth, height * 2.0f,
                               juce::jmin(barWidth * 0.5f, 2.5f));
    }

    // the wordmark sits over the waveform, knocked out of it
    auto word = bounds.withHeight(wordHeight).withCentre({ bounds.getCentreX(), centreY });
    g.setColour(juce::Colours::black.withAlpha(0.72f));
    g.fillRect(word.expanded(0.0f, wordHeight * 0.04f));
    g.setColour(textColour);
    g.setFont(labelFont(wordHeight * 1.02f, true, 0.98f));
    g.drawText("AMANORSAC", word, juce::Justification::centred, false);

    g.setColour(textColour.withAlpha(0.92f));
    g.setFont(labelFont(bounds.getHeight() * 0.155f, true, 1.0f));
    g.drawText("STUDIO",
               bounds.withHeight(bounds.getHeight() * 0.18f).withRightX(bounds.getRight() - 2.0f),
               juce::Justification::centredRight, false);
}

void drawHexEmblem(juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour colour)
{
    const auto centre = bounds.getCentre();
    const auto radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.42f;

    juce::Path hex;
    for (int i = 0; i < 6; ++i)
    {
        const auto angle = juce::MathConstants<float>::twoPi * static_cast<float>(i) / 6.0f;
        const auto point = polar(centre, angle, radius);
        if (i == 0) hex.startNewSubPath(point); else hex.lineTo(point);
    }
    hex.closeSubPath();
    g.setColour(colour.withAlpha(0.85f));
    g.strokePath(hex, juce::PathStrokeType(radius * 0.075f));

    // inner radiating arrows
    for (int i = 0; i < 6; ++i)
    {
        const auto angle = juce::MathConstants<float>::twoPi * static_cast<float>(i) / 6.0f
                         + juce::MathConstants<float>::pi / 6.0f;
        const auto inner = polar(centre, angle, radius * 0.18f);
        const auto outer = polar(centre, angle, radius * 0.70f);
        g.setColour(colour.withAlpha(0.7f));
        g.drawLine({ inner, outer }, radius * 0.055f);
        const auto head = polar(centre, angle, radius * 0.56f);
        g.drawLine({ head, polar(head, angle + 2.4f, radius * 0.16f) }, radius * 0.05f);
        g.drawLine({ head, polar(head, angle - 2.4f, radius * 0.16f) }, radius * 0.05f);
    }
    g.setColour(colour.withAlpha(0.9f));
    g.fillEllipse(juce::Rectangle<float>(radius * 0.16f, radius * 0.16f).withCentre(centre));
}

// ------------------------------------------------------------ look and feel

AnalogLookAndFeel::AnalogLookAndFeel(Palette p) : palette(std::move(p))
{
    setColour(juce::Label::textColourId, palette.cream);
    setColour(juce::TextEditor::highlightColourId, palette.accent.withAlpha(0.4f));
    setColour(juce::TextEditor::highlightedTextColourId, palette.cream);
    setColour(juce::PopupMenu::backgroundColourId, palette.recess);
    setColour(juce::PopupMenu::textColourId, palette.cream);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, palette.accent.withAlpha(0.35f));
    setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
}

void AnalogLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                         float position, float startAngle, float endAngle,
                                         juce::Slider& slider)
{
    const auto area = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y),
                                             static_cast<float>(width), static_cast<float>(height));
    const auto span = juce::jmin(area.getWidth(), area.getHeight());
    const auto tickRoom = span * 0.115f;
    const auto knob = juce::Rectangle<float>(span - tickRoom * 2.0f, span - tickRoom * 2.0f)
                          .withCentre(area.getCentre());
    const auto centre = knob.getCentre();
    const auto radius = knob.getWidth() * 0.5f;
    const auto angle = startAngle + position * (endAngle - startAngle);
    const auto hot = slider.isMouseOverOrDragging();
    const auto faceColour = trimStyle ? palette.trim
                                      : slider.findColour(juce::Slider::rotarySliderFillColourId);

    // ---------------------------------------------------------------- ticks
    // Marks below the current value stay lit, so the ring reads as a scale.
    const int tickCount = 21;
    for (int i = 0; i < tickCount; ++i)
    {
        const auto t = static_cast<float>(i) / static_cast<float>(tickCount - 1);
        const auto a = startAngle + t * (endAngle - startAngle);
        const auto lit = t <= position + 1.0e-3f;
        const auto inner = polar(centre, a, radius * 1.11f);
        const auto outer = polar(centre, a, radius * (lit ? 1.26f : 1.22f));
        if (lit)
            glowLine(g, { inner, outer }, palette.accentGlow.withAlpha(hot ? 1.0f : 0.9f), radius * 0.036f);
        else
        {
            g.setColour(palette.accent.withAlpha(0.18f));
            g.drawLine({ inner, outer }, radius * 0.03f);
        }
    }

    // --------------------------------------------------------------- shadow
    // Layered, offset downward: one hard ellipse reads as a sticker, several
    // soft rings read as a part sitting above the panel.
    for (int i = 5; i >= 1; --i)
    {
        const auto spread = radius * 0.03f * static_cast<float>(i);
        g.setColour(juce::Colours::black.withAlpha(0.13f));
        g.fillEllipse(knob.expanded(spread).translated(0.0f, radius * 0.045f + spread * 0.35f));
    }

    // ----------------------------------------------------------- outer rim
    // Knurled metal collar lit from the upper left.
    juce::ColourGradient collar(juce::Colour(0xff5c574c), centre.x - radius * 0.6f, centre.y - radius * 0.75f,
                                juce::Colour(0xff0e0d0c), centre.x + radius * 0.55f, centre.y + radius * 0.8f, true);
    collar.addColour(0.55, juce::Colour(0xff2a2721));
    g.setGradientFill(collar);
    g.fillEllipse(knob);

    // knurling: fine radial notches, brighter on the lit side
    const int notches = 44;
    for (int i = 0; i < notches; ++i)
    {
        const auto a = juce::MathConstants<float>::twoPi * static_cast<float>(i) / static_cast<float>(notches);
        const auto lift = 0.5f + 0.5f * std::cos(a + juce::MathConstants<float>::pi * 0.75f);
        g.setColour(juce::Colours::white.withAlpha(0.055f * lift));
        g.drawLine({ polar(centre, a, radius * 0.905f), polar(centre, a, radius * 0.995f) }, radius * 0.028f);
        g.setColour(juce::Colours::black.withAlpha(0.20f));
        g.drawLine({ polar(centre, a + 0.055f, radius * 0.905f),
                     polar(centre, a + 0.055f, radius * 0.995f) }, radius * 0.020f);
    }

    // ------------------------------------------------------------ brass ring
    const auto ring = knob.reduced(radius * 0.115f);
    g.setColour(palette.bronze.withAlpha(0.55f));
    g.drawEllipse(ring, radius * 0.045f);
    juce::Path arc;
    arc.addCentredArc(centre.x, centre.y, ring.getWidth() * 0.5f, ring.getHeight() * 0.5f, 0.0f,
                      -2.3f, -0.5f, true);
    g.setColour(juce::Colours::white.withAlpha(0.22f));
    g.strokePath(arc, juce::PathStrokeType(radius * 0.03f));

    // ------------------------------------------------------------------ face
    const auto face = knob.reduced(radius * 0.185f);
    juce::ColourGradient body(faceColour.brighter(trimStyle ? 0.34f : 0.22f),
                              centre.x - radius * 0.35f, centre.y - radius * 0.55f,
                              faceColour.darker(trimStyle ? 0.62f : 0.80f),
                              centre.x + radius * 0.45f, centre.y + radius * 0.7f, true);
    g.setGradientFill(body);
    g.fillEllipse(face);

    {
        juce::Graphics::ScopedSaveState state(g);
        juce::Path clip;
        clip.addEllipse(face);
        g.reduceClipRegion(clip);
        g.setTiledImageFill(grainTexture(), 0, 0, 0.55f);
        g.fillRect(face);
    }

    // specular crescent across the top of the cap
    juce::ColourGradient sheen(juce::Colours::white.withAlpha(0.16f), centre.x, face.getY(),
                               juce::Colours::transparentWhite, centre.x, centre.y + radius * 0.1f, false);
    g.setGradientFill(sheen);
    g.fillEllipse(face.reduced(radius * 0.03f));

    // contact shadow where the cap meets the collar
    g.setColour(juce::Colours::black.withAlpha(0.45f));
    g.drawEllipse(face.expanded(radius * 0.012f), radius * 0.045f);

    if (hot)
    {
        g.setColour(palette.accentGlow.withAlpha(0.30f));
        g.drawEllipse(face.reduced(radius * 0.02f), radius * 0.035f);
    }

    // --------------------------------------------------------------- pointer
    const auto tail = polar(centre, angle, radius * 0.14f);
    const auto tip = polar(centre, angle, radius * 0.70f);
    g.setColour(juce::Colours::black.withAlpha(0.50f));
    g.drawLine({ tail.translated(0.0f, radius * 0.035f), tip.translated(0.0f, radius * 0.035f) },
               radius * 0.115f);
    g.setColour(palette.cream.darker(0.45f));
    g.drawLine({ tail, tip }, radius * 0.115f);
    g.setColour(palette.cream);
    g.drawLine({ tail, tip }, radius * 0.075f);
    g.setColour(juce::Colours::white.withAlpha(0.6f));
    g.drawLine({ polar(centre, angle, radius * 0.22f), polar(centre, angle, radius * 0.64f) },
               radius * 0.022f);

    // ------------------------------------------------------------------- hub
    const auto hub = juce::Rectangle<float>(radius * 0.20f, radius * 0.20f).withCentre(centre);
    juce::ColourGradient cap(juce::Colour(0xff2b2926), hub.getX(), hub.getY(),
                             juce::Colour(0xff090807), hub.getRight(), hub.getBottom(), false);
    g.setGradientFill(cap);
    g.fillEllipse(hub);
    g.setColour(juce::Colours::white.withAlpha(0.10f));
    g.drawEllipse(hub.reduced(radius * 0.02f), radius * 0.014f);
}

void AnalogLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                              const juce::Colour&, bool over, bool down)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);
    for (int i = 4; i >= 1; --i)
    {
        g.setColour(juce::Colours::black.withAlpha(0.08f * static_cast<float>(5 - i)));
        g.fillRoundedRectangle(bounds.expanded(static_cast<float>(i)).translated(0.0f, 1.5f), 5.0f + i);
    }
    juce::ColourGradient body(down ? juce::Colour(0xff0b0d0e) : juce::Colour(0xff34383b),
                              bounds.getX(), bounds.getY(), juce::Colour(0xff0c0e10),
                              bounds.getX(), bounds.getBottom(), false);
    body.addColour(0.38, juce::Colour(0xff191c1f));
    g.setGradientFill(body);
    g.fillRoundedRectangle(bounds, 5.0f);
    g.setColour(juce::Colours::white.withAlpha(over ? 0.15f : 0.07f));
    g.drawLine(bounds.getX() + 4.0f, bounds.getY() + 1.0f, bounds.getRight() - 4.0f, bounds.getY() + 1.0f, 1.0f);
    g.setColour(palette.accent.withAlpha(over ? 0.95f : 0.52f));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 5.0f, down ? 1.6f : 0.9f);
}

void AnalogLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button,
                                       bool over, bool down)
{
    g.setColour(down ? juce::Colours::white : (over ? palette.cream : palette.label));
    g.setFont(labelFont(juce::jmin(14.0f, static_cast<float>(button.getHeight()) * 0.38f), true, 0.92f));
    g.drawText(button.getButtonText().toUpperCase(), button.getLocalBounds().reduced(4, 1),
               juce::Justification::centred, false);
}

void AnalogLookAndFeel::drawLabel(juce::Graphics& g, juce::Label& label)
{
    if (label.isBeingEdited())
    {
        LookAndFeel_V4::drawLabel(g, label);
        return;
    }
    g.setColour(label.findColour(juce::Label::textColourId));
    g.setFont(labelFont(static_cast<float>(label.getHeight()) * 0.74f, true, 0.95f));
    g.drawText(label.getText(), label.getLocalBounds(), label.getJustificationType(), false);
}

juce::Label* AnalogLookAndFeel::createSliderTextBox(juce::Slider& slider)
{
    auto* label = LookAndFeel_V4::createSliderTextBox(slider);
    label->setColour(juce::Label::textColourId, palette.accentGlow);
    label->setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    label->setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    label->setColour(juce::TextEditor::textColourId, palette.accentGlow);
    label->setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    label->setColour(juce::TextEditor::focusedOutlineColourId, palette.accent);
    label->setJustificationType(juce::Justification::centred);
    return label;
}

void AnalogLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height)
{
    const auto bounds = juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
    g.setColour(palette.recess);
    g.fillRoundedRectangle(bounds, 6.0f);
    g.setColour(palette.bronze.withAlpha(0.7f));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 1.0f);
}

juce::Font AnalogLookAndFeel::getPopupMenuFont() { return labelFont(15.0f, false, 1.0f); }

// ---------------------------------------------------------------- controls

AnalogSlider::AnalogSlider()
{
    setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    setRotaryParameters(rotaryStart, rotaryEnd, true);
    setVelocityBasedMode(false);
    setMouseDragSensitivity(220);
}

void AnalogSlider::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    auto adjusted = wheel;
    if (event.mods.isShiftDown())
    {
        adjusted.deltaY *= 0.15f;
        adjusted.deltaX *= 0.15f;
    }
    juce::Slider::mouseWheelMove(event, adjusted);
}

// ------------------------------------------------------------------ KnobCell

KnobCell::KnobCell(juce::AudioProcessorValueTreeState& state, const ParameterDescriptor& descriptor,
                   const Palette& p, AnalogLookAndFeel& look, bool showCaption)
    : palette(p), caption(showCaption ? descriptor.name.toUpperCase() : juce::String()),
      unit(descriptor.unit)
{
    slider.setLookAndFeel(&look);
    slider.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff26262a));
    slider.setDoubleClickReturnValue(true, descriptor.defaultValue);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 86, 24);
    slider.setPopupDisplayEnabled(false, false, nullptr);

    addAndMakeVisible(slider);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state, descriptor.id, slider);

    if (descriptor.kind == ParameterDescriptor::Kind::choice && descriptor.choices.size() > 0)
    {
        // A stepped parameter still reads as a knob on the anchor, so map the
        // index onto the rotary and print the option name in the value window.
        // This has to run after the attachment, which installs its own
        // text conversion functions.
        const auto options = descriptor.choices;
        slider.textFromValueFunction = [options](double value)
        {
            const auto index = juce::jlimit(0, options.size() - 1, static_cast<int>(std::round(value)));
            return options[index].toUpperCase();
        };
        slider.valueFromTextFunction = [options](const juce::String& text)
        {
            const auto index = options.indexOf(text.trim(), true);
            return index >= 0 ? static_cast<double>(index) : 0.0;
        };
        slider.updateText();
    }

    setTitle(descriptor.name);
    setDescription(descriptor.rangeText + "; double-click restores " + descriptor.defaultText);
}

KnobCell::~KnobCell() { slider.setLookAndFeel(nullptr); }

void KnobCell::setEndLegend(const juce::String& low, const juce::String& high)
{
    lowLegend = low;
    highLegend = high;
    useLegend = true;
    slider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    resized();
    repaint();
}

void KnobCell::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    if (caption.isNotEmpty())
    {
        g.setColour(palette.cream);
        const auto twoLines = caption.length() > 13;
        g.setFont(labelFont(juce::jmin(twoLines ? 11.5f : 14.5f, captionHeight() * 0.80f), true, 0.88f));
        g.drawFittedText(caption, bounds.withHeight(captionHeight()).toNearestInt(),
                         juce::Justification::centred, twoLines ? 2 : 1, 0.85f);
    }

    if (useLegend)
    {
        auto legend = bounds.withTop(bounds.getBottom() - unitHeight() - 2.0f);
        g.setColour(palette.label.withAlpha(0.85f));
        g.setFont(labelFont(juce::jmin(12.0f, unitHeight() * 0.78f), false, 0.95f));
        g.drawText(lowLegend, legend.removeFromLeft(legend.getWidth() * 0.45f),
                   juce::Justification::centredLeft, false);
        g.drawText(highLegend, legend, juce::Justification::centredRight, false);
        return;
    }

    // inset window sits exactly where the slider prints its value
    const auto window = valueBounds();
    g.setColour(juce::Colour(0xff08090a));
    g.fillRoundedRectangle(window, 4.0f);
    g.setColour(juce::Colours::black.withAlpha(0.55f));
    g.drawRoundedRectangle(window.reduced(0.4f), 4.0f, 1.6f);
    g.setColour(palette.accent.withAlpha(0.45f));
    g.drawRoundedRectangle(window.reduced(1.2f), 3.4f, 1.0f);

    if (unit.isNotEmpty())
    {
        g.setColour(palette.label.withAlpha(0.85f));
        g.setFont(labelFont(juce::jmin(12.0f, unitHeight() * 0.80f), true, 0.95f));
        g.drawText(unit, bounds.withTop(bounds.getBottom() - unitHeight()),
                   juce::Justification::centred, false);
    }
}

float KnobCell::captionHeight() const
{
    return caption.isEmpty() ? 0.0f : juce::jmin(22.0f, static_cast<float>(getHeight()) * 0.16f);
}

float KnobCell::unitHeight() const
{
    return unit.isEmpty() && ! useLegend ? 0.0f
                                         : juce::jmin(16.0f, static_cast<float>(getHeight()) * 0.12f);
}

float KnobCell::valueHeight() const
{
    return useLegend ? 0.0f : juce::jmin(26.0f, static_cast<float>(getHeight()) * 0.16f);
}

juce::Rectangle<float> KnobCell::valueBounds() const
{
    const auto width = juce::jmin(static_cast<float>(getWidth()) * 0.78f, 108.0f);
    return juce::Rectangle<float>(width, valueHeight())
               .withCentre({ static_cast<float>(getWidth()) * 0.5f,
                             static_cast<float>(getHeight()) - unitHeight() - valueHeight() * 0.5f - 2.0f });
}

void KnobCell::resized()
{
    auto area = getLocalBounds().toFloat();
    area.removeFromTop(captionHeight());
    area.removeFromBottom(unitHeight() + 2.0f);

    if (useLegend)
    {
        const auto side = juce::jmin(juce::jmin(area.getWidth(), area.getHeight()), 150.0f);
        slider.setBounds(juce::Rectangle<float>(side, side)
                             .withCentre(area.getCentre()).toNearestInt());
        return;
    }

    // The rotary and its value window share one component, so cap the knob and
    // let the text box occupy exactly the drawn inset.
    const auto value = valueBounds();
    auto knobArea = area.withTrimmedBottom(area.getBottom() - value.getY());
    const auto side = juce::jmin(juce::jmin(knobArea.getWidth(), knobArea.getHeight()), 150.0f);
    const auto square = juce::Rectangle<float>(side, side).withCentre(knobArea.getCentre());

    slider.setBounds(square.getUnion(value).toNearestInt());
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false,
                           juce::roundToInt(value.getWidth()), juce::roundToInt(value.getHeight()));
}

// ------------------------------------------------------------------ TrimKnob

TrimKnob::TrimKnob(juce::AudioProcessorValueTreeState& state, const ParameterDescriptor& descriptor,
                   const Palette& p, juce::String captionText, juce::String footerText)
    : palette(p), caption(std::move(captionText)), footer(std::move(footerText)), look(p)
{
    look.setTrimStyle(true);
    slider.setLookAndFeel(&look);
    slider.setDoubleClickReturnValue(true, descriptor.defaultValue);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    addAndMakeVisible(slider);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(state, descriptor.id, slider);
    setTitle(descriptor.name);
    setDescription(descriptor.rangeText + "; double-click restores " + descriptor.defaultText);
}

TrimKnob::~TrimKnob() { slider.setLookAndFeel(nullptr); }

void TrimKnob::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour(palette.cream);
    g.setFont(labelFont(juce::jmin(17.0f, bounds.getHeight() * 0.10f), true, 0.9f));
    g.drawText(caption, bounds.removeFromTop(bounds.getHeight() * 0.16f), juce::Justification::centred, false);

    // numeric scale ring around the knob, as printed on the faceplate
    const auto knob = slider.getBounds().toFloat();
    const auto centre = knob.getCentre();
    const auto radius = knob.getWidth() * 0.5f;
    static const std::array<const char*, 9> marks { "-24", "-18", "-12", "-6", "0", "+6", "+12", "+18", "+24" };
    g.setFont(labelFont(radius * 0.19f, true, 0.92f));
    for (size_t i = 0; i < marks.size(); ++i)
    {
        const auto t = static_cast<float>(i) / static_cast<float>(marks.size() - 1);
        const auto angle = rotaryStart + t * (rotaryEnd - rotaryStart);
        const auto at = polar(centre, angle, radius * 1.24f);
        g.setColour(palette.label.withAlpha(0.9f));
        g.drawText(juce::String(marks[i]),
                   juce::Rectangle<float>(radius * 0.62f, radius * 0.26f).withCentre(at),
                   juce::Justification::centred, false);
    }

    if (footer.isNotEmpty())
    {
        g.setColour(palette.accent);
        g.setFont(labelFont(getHeight() * 0.082f, true, 0.92f));
        g.drawText(footer, getLocalBounds().toFloat().removeFromBottom(getHeight() * 0.12f),
                   juce::Justification::centred, false);
    }
}

void TrimKnob::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop(juce::roundToInt(getHeight() * 0.13f));
    area.removeFromBottom(juce::roundToInt(getHeight() * 0.12f));
    const auto side = juce::jmin(juce::roundToInt(area.getWidth() * 0.60f),
                                area.getHeight() - juce::roundToInt(getHeight() * 0.10f));
    slider.setBounds(area.withSizeKeepingCentre(side, side));
}

// -------------------------------------------------------------- SegmentGroup

SegmentGroup::SegmentGroup(juce::AudioProcessorValueTreeState& valueTree,
                           const ParameterDescriptor& descriptor, const Palette& p,
                           juce::String captionText, int columnCount)
    : palette(p), caption(std::move(captionText)), columns(juce::jmax(1, columnCount)),
      options(descriptor.choices), state(valueTree), parameterId(descriptor.id)
{
    parameter = dynamic_cast<juce::AudioParameterChoice*>(state.getParameter(parameterId));

    if (options.isEmpty() && descriptor.kind == ParameterDescriptor::Kind::boolean)
        options = { "OFF", "ON" };

    juce::String shared;
    if (options.size() > 1 && options[0].containsChar('_'))
    {
        const auto stem = options[0].upToLastOccurrenceOf("_", true, false);
        bool all = true;
        for (const auto& option : options) all = all && option.startsWith(stem);
        if (all) shared = stem;
    }

    for (int i = 0; i < options.size(); ++i)
    {
        const auto printed = (shared.isNotEmpty() ? options[i].substring(shared.length()) : options[i])
                                 .replaceCharacter('_', ' ').toUpperCase();
        auto* button = buttons.add(new juce::TextButton(printed));
        button->setClickingTogglesState(false);
        // the group paints the key; the button only carries the mouse behaviour
        for (const int colourId : { static_cast<int>(juce::TextButton::buttonColourId),
                                    static_cast<int>(juce::TextButton::buttonOnColourId),
                                    static_cast<int>(juce::TextButton::textColourOffId),
                                    static_cast<int>(juce::TextButton::textColourOnId),
                                    static_cast<int>(juce::ComboBox::outlineColourId) })
            button->setColour(colourId, juce::Colours::transparentBlack);
        button->onClick = [this, i]
        {
            if (parameter != nullptr)
            {
                parameter->beginChangeGesture();
                parameter->setValueNotifyingHost(parameter->convertTo0to1(static_cast<float>(i)));
                parameter->endChangeGesture();
            }
            else if (auto* raw = state.getParameter(parameterId))
            {
                raw->beginChangeGesture();
                raw->setValueNotifyingHost(options.size() > 1
                                               ? static_cast<float>(i) / static_cast<float>(options.size() - 1)
                                               : 0.0f);
                raw->endChangeGesture();
            }
            refresh();
        };
        addAndMakeVisible(button);
    }

    if (auto* raw = state.getParameter(parameterId))
        sync = std::make_unique<juce::ParameterAttachment>(*raw, [this](float) { refresh(); });

    refresh();
    setTitle(descriptor.name);
    setDescription(descriptor.rangeText);
}

SegmentGroup::~SegmentGroup() = default;

void SegmentGroup::setOptionLabels(const juce::StringArray& labels)
{
    for (int i = 0; i < buttons.size() && i < labels.size(); ++i)
        buttons[i]->setButtonText(labels[i]);
    repaint();
}

void SegmentGroup::refresh()
{
    int selected = 0;
    if (parameter != nullptr) selected = parameter->getIndex();
    else if (const auto* value = state.getRawParameterValue(parameterId))
        selected = juce::jlimit(0, buttons.size() - 1, static_cast<int>(std::round(value->load())));

    for (int i = 0; i < buttons.size(); ++i)
        buttons[i]->setToggleState(i == selected, juce::dontSendNotification);
    repaint();
}

void SegmentGroup::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    if (caption.isNotEmpty())
    {
        g.setColour(palette.label);
        g.setFont(labelFont(juce::jmin(13.0f, bounds.getHeight() * 0.20f), true, 0.9f));
        g.drawFittedText(caption.toUpperCase(), bounds.removeFromTop(bounds.getHeight() * 0.30f).toNearestInt(),
                         juce::Justification::centred, 2);
    }

    // Keys are machined metal: engraved and recessed when idle, raised and lit
    // when selected, so the bank reads as hardware rather than a list.
    for (auto* button : buttons)
    {
        const auto area = button->getBounds().toFloat().reduced(2.0f);
        const auto on = button->getToggleState();
        const auto hot = button->isOver();
        const auto corner = juce::jmin(5.0f, area.getHeight() * 0.24f);

        if (on)
        {
            g.setColour(palette.accent.withAlpha(0.22f));
            g.fillRoundedRectangle(area.expanded(3.5f), corner + 3.0f);
        }
        else
        {
            g.setColour(juce::Colours::black.withAlpha(0.45f));
            g.fillRoundedRectangle(area.translated(0.0f, 1.4f), corner);
        }

        const auto top = on ? palette.accent.brighter(0.30f) : juce::Colour(0xff34373b);
        const auto bottom = on ? palette.accent.darker(0.55f) : juce::Colour(0xff15171a);
        juce::ColourGradient body(top, area.getCentreX(), area.getY(),
                                  bottom, area.getCentreX(), area.getBottom(), false);
        g.setGradientFill(body);
        g.fillRoundedRectangle(area, corner);

        {
            juce::Graphics::ScopedSaveState state(g);
            juce::Path clip;
            clip.addRoundedRectangle(area, corner);
            g.reduceClipRegion(clip);
            g.setTiledImageFill(grainTexture(), 0, 0, 0.45f);
            g.fillRect(area);
        }

        // bevel: catch-light along the top edge, shade along the bottom
        g.setColour(juce::Colours::white.withAlpha(on ? 0.34f : 0.10f));
        g.drawLine(area.getX() + corner, area.getY() + 1.0f,
                   area.getRight() - corner, area.getY() + 1.0f, 1.2f);
        g.setColour(juce::Colours::black.withAlpha(0.42f));
        g.drawLine(area.getX() + corner, area.getBottom() - 1.0f,
                   area.getRight() - corner, area.getBottom() - 1.0f, 1.2f);

        g.setColour(on ? palette.accentGlow.brighter(0.20f)
                       : palette.bronze.withAlpha(hot ? 0.85f : 0.42f));
        g.drawRoundedRectangle(area.reduced(0.5f), corner, on ? 1.5f : 1.0f);

        if (! on && hot)
        {
            g.setColour(juce::Colours::white.withAlpha(0.06f));
            g.fillRoundedRectangle(area, corner);
        }

        const auto text = juce::jmin(16.5f, area.getHeight() * 0.66f);
        g.setColour(juce::Colours::black.withAlpha(on ? 0.45f : 0.55f));
        g.setFont(labelFont(text, true, 0.9f));
        g.drawText(button->getButtonText(), area.translated(0.0f, 1.0f),
                   juce::Justification::centred, false);
        g.setColour(on ? juce::Colours::white : palette.cream.withAlpha(hot ? 1.0f : 0.90f));
        g.drawText(button->getButtonText(), area, juce::Justification::centred, false);
    }
}

void SegmentGroup::resized()
{
    auto area = getLocalBounds();
    if (caption.isNotEmpty()) area.removeFromTop(juce::roundToInt(getHeight() * 0.30f));

    const auto rows = (buttons.size() + columns - 1) / columns;
    if (rows <= 0) return;

    // A key is a physical size, not a share of the panel: cap it and centre the
    // block so a wide bank never stretches into flat list rows.
    const auto cellWidth = juce::jlimit(56, 132, area.getWidth() / columns);
    const auto cellHeight = juce::jlimit(24, 42, area.getHeight() / rows);
    const auto blockWidth = cellWidth * columns;
    const auto blockHeight = cellHeight * rows;
    const auto left = area.getCentreX() - blockWidth / 2;
    const auto top = area.getY() + (area.getHeight() - blockHeight) / 2;
    for (int i = 0; i < buttons.size(); ++i)
        buttons[i]->setBounds(left + (i % columns) * cellWidth,
                              top + (i / columns) * cellHeight,
                              cellWidth, cellHeight);
}

// ---------------------------------------------------------------- LampButton

LampButton::LampButton(juce::AudioProcessorValueTreeState& state, const ParameterDescriptor& descriptor,
                       const Palette& p, juce::String glyphText, juce::String captionText, bool square)
    : palette(p), glyph(std::move(glyphText)), caption(std::move(captionText)), squareLamp(square)
{
    addChildComponent(hidden);
    hidden.onStateChange = [this] { repaint(); };
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(state, descriptor.id, hidden);
    setTitle(descriptor.name);
    setDescription(descriptor.rangeText);
    setInterceptsMouseClicks(true, false);
}

LampButton::~LampButton() = default;

void LampButton::mouseDown(const juce::MouseEvent&)
{
    hidden.setToggleState(! hidden.getToggleState(), juce::sendNotificationSync);
}

void LampButton::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    auto captionRow = caption.isNotEmpty() ? bounds.removeFromBottom(bounds.getHeight() * 0.26f)
                                           : juce::Rectangle<float>();
    const auto on = hidden.getToggleState();
    const auto hot = isMouseOver();
    auto pad = bounds.reduced(bounds.getWidth() * 0.06f, bounds.getHeight() * 0.08f);
    // clamp to a believable key shape, centred in whatever cell we were given
    const auto keyHeight = juce::jmin(pad.getHeight(), 40.0f);
    const auto keyWidth = juce::jlimit(44.0f, keyHeight * 2.6f, pad.getWidth());
    pad = juce::Rectangle<float>(keyWidth, keyHeight).withCentre(pad.getCentre());

    juce::ColourGradient body(juce::Colour(0xff2b2c30), pad.getCentreX(), pad.getY(),
                              juce::Colour(0xff141517), pad.getCentreX(), pad.getBottom(), false);
    g.setGradientFill(body);
    g.fillRoundedRectangle(pad, 5.0f);
    g.setColour(on ? palette.accentGlow.withAlpha(0.95f) : palette.bronze.withAlpha(hot ? 0.9f : 0.5f));
    g.drawRoundedRectangle(pad.reduced(0.5f), 5.0f, on ? 1.6f : 1.0f);

    if (glyph.isNotEmpty())
    {
        g.setColour(on ? palette.accentGlow : palette.cream.withAlpha(0.86f));
        g.setFont(labelFont(pad.getHeight() * 0.52f, true, 0.95f));
        g.drawText(glyph, pad, juce::Justification::centred, false);
    }
    else
    {
        const auto lampSize = juce::jmin(pad.getWidth(), pad.getHeight()) * 0.52f;
        const auto lamp = juce::Rectangle<float>(lampSize, lampSize).withCentre(pad.getCentre());
        if (squareLamp)
        {
            g.setColour(on ? palette.accent.withAlpha(0.35f) : juce::Colours::transparentBlack);
            g.fillRoundedRectangle(lamp.expanded(lampSize * 0.45f), 4.0f);
            g.setColour(on ? palette.accentGlow : juce::Colour(0xff3a2a20));
            g.fillRoundedRectangle(lamp, 3.0f);
            g.setColour(juce::Colours::white.withAlpha(on ? 0.65f : 0.08f));
            g.fillRoundedRectangle(lamp.reduced(lampSize * 0.28f), 2.0f);
        }
        else if (on) glowDot(g, lamp, palette.accentGlow);
        else
        {
            g.setColour(juce::Colour(0xff3a2a20));
            g.fillEllipse(lamp);
        }
    }

    if (caption.isNotEmpty())
    {
        g.setColour(palette.label.withAlpha(0.9f));
        g.setFont(labelFont(juce::jmin(13.0f, captionRow.getHeight() * 0.82f), true, 0.9f));
        g.drawText(caption.toUpperCase(), captionRow, juce::Justification::centred, false);
    }
}

void LampButton::resized() {}

// ------------------------------------------------------------------ VuMeter

VuMeter::VuMeter(std::function<float()> levelSource, juce::String caption)
    : source(std::move(levelSource)), scaleCaption(std::move(caption))
{
    startTimerHz(30);
}

void VuMeter::timerCallback()
{
    const auto target = source ? juce::jlimit(0.0f, 1.25f, source()) : 0.0f;
    // classic VU ballistics: fast rise, slow fall
    needle += (target - needle) * (target > needle ? 0.34f : 0.12f);
    repaint();
}

void VuMeter::paint(juce::Graphics& g)
{
    auto outer = getLocalBounds().toFloat();

    // bezel
    juce::ColourGradient bezel(juce::Colour(0xff2a2b2d), outer.getX(), outer.getY(),
                               juce::Colour(0xff0c0d0e), outer.getRight(), outer.getBottom(), false);
    g.setGradientFill(bezel);
    g.fillRoundedRectangle(outer, 9.0f);
    g.setColour(juce::Colour(0xff8a6a3c).withAlpha(0.8f));
    g.drawRoundedRectangle(outer.reduced(1.5f), 8.0f, 1.4f);

    auto face = outer.reduced(outer.getWidth() * 0.022f, outer.getHeight() * 0.05f);
    juce::ColourGradient plate(juce::Colour(0xfff2dcae), face.getCentreX(), face.getY(),
                               juce::Colour(0xffd9ab68), face.getCentreX(), face.getBottom(), false);
    g.setGradientFill(plate);
    g.fillRoundedRectangle(face, 5.0f);
    g.setColour(juce::Colour(0xff5a3f22).withAlpha(0.55f));
    g.drawRoundedRectangle(face.reduced(0.5f), 5.0f, 1.0f);

    // A real VU pivots well below the plate so the arc reads almost flat. The
    // radius comes from the chord we want the scale to span, not from the plate
    // height, otherwise the numbers bunch up in the middle of the window.
    const auto sweep = 0.40f;   // radians either side of vertical
    const auto radius = (face.getWidth() * 0.84f) / (2.0f * std::sin(sweep));
    const auto pivot = juce::Point<float>(face.getCentreX(),
                                          face.getY() + face.getHeight() * 0.345f + radius);

    g.saveState();
    juce::Path faceClip;
    faceClip.addRoundedRectangle(face, 5.0f);
    g.reduceClipRegion(faceClip);

    static const std::array<const char*, 11> labels { "-20", "-10", "-7", "-5", "-3", "-2", "-1", "0", "+1", "+2", "+3" };
    static const std::array<float, 11> positions { 0.00f, 0.26f, 0.39f, 0.49f, 0.60f, 0.67f, 0.74f, 0.81f, 0.88f, 0.94f, 1.00f };
    const auto redFrom = 0.81f;

    // minor ticks
    for (int i = 0; i <= 44; ++i)
    {
        const auto t = static_cast<float>(i) / 44.0f;
        const auto angle = juce::jmap(t, 0.0f, 1.0f, -sweep, sweep);
        g.setColour((t >= redFrom ? juce::Colour(0xffa8291b) : juce::Colour(0xff33220f)).withAlpha(0.5f));
        g.drawLine({ polar(pivot, angle, radius * 0.972f), polar(pivot, angle, radius * 0.998f) }, 1.0f);
    }

    const auto roomForNumerals = face.getWidth() > 300.0f;
    for (size_t i = 0; i < labels.size(); ++i)
    {
        const auto angle = juce::jmap(positions[i], 0.0f, 1.0f, -sweep, sweep);
        const auto isRed = positions[i] >= redFrom;
        if (! roomForNumerals && (i % 2) == 1) continue;
        g.setColour(isRed ? juce::Colour(0xffa8291b) : juce::Colour(0xff2b1c0c));
        g.drawLine({ polar(pivot, angle, radius * 0.958f), polar(pivot, angle, radius * 1.002f) }, 2.0f);

        const auto textAt = polar(pivot, angle, radius * 1.044f);
        g.setFont(labelFont(face.getHeight() * 0.105f, true, 0.92f));
        g.drawText(juce::String(labels[i]),
                   juce::Rectangle<float>(face.getWidth() * 0.085f, face.getHeight() * 0.14f).withCentre(textAt),
                   juce::Justification::centred, false);
    }

    // red overload arc riding just inside the scale
    juce::Path redArc;
    redArc.addCentredArc(pivot.x, pivot.y, radius * 1.006f, radius * 1.006f, 0.0f,
                         juce::jmap(redFrom, 0.0f, 1.0f, -sweep, sweep), sweep, true);
    g.setColour(juce::Colour(0xffb02a1a));
    g.strokePath(redArc, juce::PathStrokeType(3.4f));

    // captions
    g.setColour(juce::Colour(0xff3a2712));
    g.setFont(labelFont(face.getHeight() * 0.095f, true, 0.9f));
    g.drawText(scaleCaption, face.withHeight(face.getHeight() * 0.12f).translated(0.0f, face.getHeight() * 0.02f),
               juce::Justification::centred, false);

    g.setFont(labelFont(face.getHeight() * 0.26f, true, 0.95f));
    g.setColour(juce::Colour(0xff2e1f0e));
    g.drawText("VU", face.withHeight(face.getHeight() * 0.26f).translated(0.0f, face.getHeight() * 0.52f),
               juce::Justification::centred, false);

    auto brandRow = face.withHeight(face.getHeight() * 0.13f).translated(0.0f, face.getHeight() * 0.80f)
                        .reduced(face.getWidth() * 0.33f, 0.0f);
    drawBrandMark(g, brandRow, juce::Colour(0xff54401f), juce::Colour(0xff3a2712));

    g.setColour(juce::Colour(0xff3a2712).withAlpha(0.85f));
    g.setFont(labelFont(face.getHeight() * 0.13f, true, 1.0f));
    g.drawText("-", face.withWidth(face.getWidth() * 0.10f).withHeight(face.getHeight() * 0.2f)
                        .translated(face.getWidth() * 0.05f, face.getHeight() * 0.68f),
               juce::Justification::centred, false);
    g.drawText("+", face.withWidth(face.getWidth() * 0.10f).withHeight(face.getHeight() * 0.2f)
                        .translated(face.getWidth() * 0.85f, face.getHeight() * 0.68f),
               juce::Justification::centred, false);

    // needle
    const auto angle = juce::jmap(juce::jlimit(0.0f, 1.08f, needle), 0.0f, 1.0f, -sweep, sweep);
    const auto tip = polar(pivot, angle, radius * 0.985f);
    const auto tail = polar(pivot, angle, radius * 0.86f);
    g.setColour(juce::Colours::black.withAlpha(0.20f));
    g.drawLine({ tail.translated(2.5f, 2.5f), tip.translated(2.5f, 2.5f) }, 2.4f);
    g.setColour(juce::Colour(0xff141414));
    g.drawLine({ tail, tip }, 2.2f);

    g.restoreState();

    // glass
    juce::ColourGradient glass(juce::Colours::white.withAlpha(0.20f), face.getX(), face.getY(),
                               juce::Colours::transparentWhite, face.getX() + face.getWidth() * 0.55f,
                               face.getY() + face.getHeight() * 0.75f, false);
    g.setGradientFill(glass);
    g.fillRoundedRectangle(face, 5.0f);
}

// -------------------------------------------------------------- LedBargraph

LedBargraph::LedBargraph(std::function<float()> leftSource, std::function<float()> rightSource,
                         const Palette& p)
    : left(std::move(leftSource)), right(std::move(rightSource)), palette(p)
{
    startTimerHz(30);
}

void LedBargraph::timerCallback()
{
    const auto l = left ? juce::jlimit(0.0f, 1.0f, left()) : 0.0f;
    const auto r = right ? juce::jlimit(0.0f, 1.0f, right()) : 0.0f;
    leftLevel = juce::jmax(l, leftLevel * 0.88f);
    rightLevel = juce::jmax(r, rightLevel * 0.88f);
    repaint();
}

void LedBargraph::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    const auto footer = bounds.removeFromBottom(bounds.getHeight() * 0.055f);

    const auto columnWidth = bounds.getWidth() * 0.30f;
    auto leftColumn = bounds.removeFromLeft(columnWidth);
    auto rightColumn = bounds.removeFromRight(columnWidth);
    auto legend = bounds;

    static const std::array<const char*, 10> scale { "+18", "+12", "+6", "0", "-6", "-12", "-18", "-30", "-48", "-60" };
    g.setFont(labelFont(juce::jmin(12.0f, legend.getHeight() * 0.045f), true, 0.92f));
    for (size_t i = 0; i < scale.size(); ++i)
    {
        const auto t = static_cast<float>(i) / static_cast<float>(scale.size() - 1);
        const auto y = juce::jmap(t, 0.0f, 1.0f, legend.getY() + 6.0f, legend.getBottom() - 6.0f);
        g.setColour(palette.accent.withAlpha(0.8f));
        g.drawText(juce::String(scale[i]),
                   juce::Rectangle<float>(legend.getWidth(), legend.getHeight() * 0.06f)
                       .withCentre({ legend.getCentreX(), y }),
                   juce::Justification::centred, false);
    }

    const int segments = 24;
    auto drawColumn = [&](juce::Rectangle<float> column, float level)
    {
        g.setColour(juce::Colour(0xff0b0c0d));
        g.fillRoundedRectangle(column, 4.0f);
        g.setColour(palette.bronze.withAlpha(0.4f));
        g.drawRoundedRectangle(column.reduced(0.5f), 4.0f, 1.0f);

        auto inner = column.reduced(column.getWidth() * 0.18f, column.getHeight() * 0.012f);
        const auto gap = inner.getHeight() * 0.012f;
        const auto cell = (inner.getHeight() - gap * static_cast<float>(segments - 1)) / static_cast<float>(segments);

        for (int i = 0; i < segments; ++i)
        {
            const auto fromTop = static_cast<float>(i) / static_cast<float>(segments - 1);
            const auto value = 1.0f - fromTop;             // 0 at bottom, 1 at top
            const auto lit = level >= value;
            const auto y = inner.getY() + static_cast<float>(i) * (cell + gap);
            const auto segment = juce::Rectangle<float>(inner.getX(), y, inner.getWidth(), cell);

            juce::Colour colour = value > 0.86f ? juce::Colour(0xffe0431f)
                                : value > 0.66f ? juce::Colour(0xfff0a51f)
                                                : juce::Colour(0xff3fbf46);
            if (lit)
            {
                g.setColour(colour.withAlpha(0.25f));
                g.fillRoundedRectangle(segment.expanded(2.0f), 2.0f);
                g.setColour(colour);
            }
            else g.setColour(colour.withAlpha(0.10f));
            g.fillRoundedRectangle(segment, 1.6f);
        }
    };

    drawColumn(leftColumn.reduced(2.0f, 0.0f), leftLevel);
    drawColumn(rightColumn.reduced(2.0f, 0.0f), rightLevel);

    g.setColour(palette.label);
    g.setFont(labelFont(juce::jmin(13.0f, footer.getHeight() * 0.9f), true, 0.95f));
    g.drawText("L", footer.withWidth(columnWidth), juce::Justification::centred, false);
    g.drawText("R", footer.withTrimmedLeft(footer.getWidth() - columnWidth), juce::Justification::centred, false);
}

// -------------------------------------------------------------------- TopBar

TopBar::TopBar(const Palette& p, juce::String preset) : palette(p), look(p), presetName(std::move(preset))
{
    for (const auto* text : { "A / B", "COPY", "PASTE" })
        addAndMakeVisible(actions.add(new juce::TextButton(text)));
    for (const auto* text : { "UNDO", "REDO", "SAVE", "MENU" })
        addAndMakeVisible(actions.add(new juce::TextButton(text)));

    for (auto* button : actions)
    {
        button->setLookAndFeel(&look);
    }
    addAndMakeVisible(previousPreset);
    addAndMakeVisible(nextPreset);
    for (auto* button : { &previousPreset, &nextPreset })
    {
        button->setLookAndFeel(&look);
    }
}

TopBar::~TopBar()
{
    for (auto* button : actions) button->setLookAndFeel(nullptr);
    previousPreset.setLookAndFeel(nullptr);
    nextPreset.setLookAndFeel(nullptr);
}

void TopBar::setProductTitle(juce::String title, juce::String subtitle)
{
    productTitle = std::move(title);
    productSubtitle = std::move(subtitle);
    repaint();
}

void TopBar::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    // The product PNG supplies the top plate, texture, borders and lighting.

    auto logoArea = bounds.reduced(18.0f, 0.0f).removeFromLeft(bounds.getWidth() * 0.19f)
                          .withSizeKeepingCentre(bounds.getWidth() * 0.19f, bounds.getHeight() * 0.60f);
    drawBrandMark(g, logoArea, juce::Colour(0xff2f6fd0), palette.cream);

    if (productTitle.isNotEmpty())
    {
        auto titleArea = bounds.withSizeKeepingCentre(bounds.getWidth() * 0.44f, bounds.getHeight() * 0.52f)
                               .withY(bounds.getY() + bounds.getHeight() * 0.06f);
        g.setColour(palette.cream);
        g.setFont(labelFont(titleArea.getHeight() * 0.62f, true, 0.94f));
        g.drawText(productTitle.toUpperCase(), titleArea, juce::Justification::centredTop, false);
        g.setColour(palette.accent);
        g.setFont(labelFont(titleArea.getHeight() * 0.30f, true, 0.94f));
        g.drawText(productSubtitle.toUpperCase(),
                   titleArea.withTrimmedTop(titleArea.getHeight() * 0.62f),
                   juce::Justification::centredTop, false);
    }

    // preset window
    const auto window = presetBounds.toFloat();
    if (! window.isEmpty())
    {
        g.setColour(juce::Colour(0xff0c0d0f));
        g.fillRoundedRectangle(window, 5.0f);
        g.setColour(palette.bronze.withAlpha(0.65f));
        g.drawRoundedRectangle(window.reduced(0.5f), 5.0f, 1.0f);
        g.setColour(palette.cream);
        g.setFont(labelFont(window.getHeight() * 0.42f, false, 0.98f));
        g.drawText(presetName, window, juce::Justification::centred, false);
    }
}

void TopBar::resized()
{
    if (getHeight() < 100 && productTitle.isNotEmpty())
    {
        auto row=getLocalBounds().removeFromRight(getWidth()/4).reduced(4,10);
        const int w=row.getWidth()/juce::jmax(1,actions.size());
        for(auto* button:actions) button->setBounds(row.removeFromLeft(w).withSizeKeepingCentre(w-3,28));
        previousPreset.setBounds(0,0,0,0);
        nextPreset.setBounds(0,0,0,0);
        presetBounds={};
        return;
    }
    auto bounds = getLocalBounds().reduced(14, 0);
    const auto rowHeight = juce::jmin(38, getHeight() - 12);
    auto row = bounds.withSizeKeepingCentre(bounds.getWidth(), rowHeight);
    if (productTitle.isNotEmpty())
        row = row.withY(getHeight() - rowHeight - 12);

    auto rightSide = row.removeFromRight(juce::roundToInt(row.getWidth() * 0.38f));
    const auto actionWidth = rightSide.getWidth() / juce::jmax(1, actions.size());
    for (auto* button : actions)
        button->setBounds(rightSide.removeFromLeft(actionWidth).reduced(3, 0));

    auto centre = row.withSizeKeepingCentre(juce::jmin(520, row.getWidth()), rowHeight);
    previousPreset.setBounds(centre.removeFromLeft(46).reduced(2));
    nextPreset.setBounds(centre.removeFromRight(46).reduced(2));
    presetBounds = centre.reduced(6, 2);
}

// --------------------------------------------------------------- FooterStrip

FooterStrip::FooterStrip(const Palette& p, std::vector<Hint> items)
    : palette(p), hints(std::move(items)) {}

void FooterStrip::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    // The product PNG supplies the footer plate, texture, borders and lighting.

    if (hints.empty()) return;
    const auto cellWidth = bounds.getWidth() / static_cast<float>(hints.size());

    for (size_t i = 0; i < hints.size(); ++i)
    {
        auto cell = juce::Rectangle<float>(bounds.getX() + cellWidth * static_cast<float>(i),
                                           bounds.getY(), cellWidth, bounds.getHeight()).reduced(18.0f, 8.0f);
        if (i > 0)
        {
            g.setColour(palette.bronze.withAlpha(0.25f));
            g.drawVerticalLine(juce::roundToInt(cell.getX() - 14.0f),
                               bounds.getY() + 10.0f, bounds.getBottom() - 10.0f);
        }

        // The legend sits in whatever strip the artwork leaves, so the badge is
        // a fixed physical size rather than a share of that strip.
        const auto badge = juce::jmin(30.0f, cell.getHeight() * 0.62f);
        auto iconArea = cell.removeFromLeft(badge * 1.3f);
        const auto icon = juce::Rectangle<float>(badge, badge).withCentre(iconArea.getCentre());
        g.setColour(palette.accent.withAlpha(0.85f));
        g.drawEllipse(icon, 1.3f);
        g.setFont(labelFont(icon.getHeight() * 0.58f, true, 0.9f));
        g.drawText(hints[i].glyph, icon, juce::Justification::centred, false);

        cell.removeFromLeft(8.0f);
        cell = cell.withSizeKeepingCentre(cell.getWidth(), juce::jmin(cell.getHeight(), 40.0f));
        auto titleRow = cell.removeFromTop(cell.getHeight() * 0.46f);
        g.setColour(palette.accent);
        g.setFont(labelFont(juce::jmin(13.0f, titleRow.getHeight() * 0.88f), true, 0.92f));
        g.drawText(hints[i].title.toUpperCase(), titleRow, juce::Justification::centredLeft, false);

        g.setColour(palette.label.withAlpha(0.85f));
        g.setFont(labelFont(juce::jmin(12.0f, cell.getHeight() * 0.46f), false, 0.95f));
        g.drawFittedText(hints[i].detail, cell.toNearestInt(), juce::Justification::centredLeft, 2);
    }
}
}
