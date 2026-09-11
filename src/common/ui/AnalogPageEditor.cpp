#include "AnalogPageEditor.h"

#include "ActivationView.h"

#include "AnalogChassis.h"

#include <cmath>

namespace amanorsac
{
namespace
{
using namespace analog;
using namespace hw;

float vuDeflection(float peak)
{
    const auto db = juce::Decibels::gainToDecibels(juce::jmax(peak, 1.0e-5f));
    return juce::jlimit(0.0f, 1.15f, juce::jmap(db, -26.0f, 3.0f, 0.0f, 1.0f));
}

float ledDeflection(float peak)
{
    const auto db = juce::Decibels::gainToDecibels(juce::jmax(peak, 1.0e-6f));
    return juce::jlimit(0.0f, 1.0f, juce::jmap(db, -60.0f, 18.0f, 0.0f, 1.0f));
}

/** Per-product hero artwork shown above the control rows. */
class HeroVisual final : public juce::Component, private juce::Timer
{
public:
    HeroVisual(ProductHost owner, Palette p)
        : host(std::move(owner)), palette(std::move(p)), productId(host.spec->id)
    {
        startTimerHz(30);
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        // The plate chamber sits inside a brass window the backdrop already
        // draws, so it gets the whole bounds and no second frame or caption.
        if (productId == "A10") { drawPlate(g, bounds); return; }

        fillPanel(g, bounds, palette, 9.0f, true);
        strokePanel(g, bounds.reduced(0.6f), palette, 9.0f, 1.2f, 0.6f);

        auto stage = bounds.reduced(16.0f, 14.0f);
        auto caption = stage.removeFromTop(20.0f);
        g.setColour(palette.accent.withAlpha(0.9f));
        g.setFont(labelFont(13.0f, true, 0.9f));
        g.drawText(captionText().toUpperCase(), caption, juce::Justification::centred, false);
        stage.removeFromTop(6.0f);

        if (productId == "A04")      drawReels(g, stage);
        else if (productId == "A05") drawTube(g, stage);
        else if (productId == "A09") drawCurve(g, stage);
        else if (productId == "A10") drawPlate(g, stage);
        else if (productId == "A03") drawSignalPath(g, stage);
        else if (productId == "A07") drawOptoCell(g, stage);
        else                          drawScope(g, stage);
    }

private:
    ProductHost host;
    Palette palette;
    juce::String productId;
    float phase = 0.0f;
    float level = 0.0f;

    void timerCallback() override
    {
        const auto target = juce::jmax(host.peakOut(0), host.peakOut(1));
        level = juce::jmax(juce::jlimit(0.0f, 1.0f, target), level * 0.90f);
        phase = std::fmod(phase + 0.03f + level * 0.15f, juce::MathConstants<float>::twoPi);
        repaint();
    }

    juce::String captionText() const
    {
        if (productId == "A03") return "Console signal path";
        if (productId == "A04") return "Dual reel transport";
        if (productId == "A05") return "Harmonic valve chamber";
        if (productId == "A07") return "Opto cell";
        if (productId == "A09") return "Passive program curve";
        if (productId == "A10") return "Plate chamber";
        return "Gain reduction / output";
    }

    float parameter(const juce::String& id, float fallback = 0.0f) const
    {
        if (const auto* value = host.value(id)) return value->load();
        return fallback;
    }

    void drawReels(juce::Graphics& g, juce::Rectangle<float> area)
    {
        const auto radius = juce::jmin(area.getHeight() * 0.44f, area.getWidth() * 0.19f);
        const std::array<float, 2> xs { area.getX() + area.getWidth() * 0.27f,
                                        area.getX() + area.getWidth() * 0.73f };
        for (const auto x : xs)
        {
            const auto centre = juce::Point<float>(x, area.getCentreY());
            juce::ColourGradient metal(juce::Colour(0xffb9a279), centre.x - radius, centre.y - radius,
                                       juce::Colour(0xff2c251b), centre.x + radius, centre.y + radius, false);
            g.setGradientFill(metal);
            g.fillEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);
            g.setColour(juce::Colour(0xff0a0907));
            for (int spoke = 0; spoke < 6; ++spoke)
            {
                const auto a = phase + static_cast<float>(spoke) * juce::MathConstants<float>::twoPi / 6.0f;
                const auto inner = centre + juce::Point<float>(std::cos(a), std::sin(a)) * (radius * 0.26f);
                const auto outer = centre + juce::Point<float>(std::cos(a), std::sin(a)) * (radius * 0.86f);
                g.drawLine({ inner, outer }, radius * 0.16f);
            }
            g.setColour(palette.bronze.brighter(0.2f));
            g.fillEllipse(centre.x - radius * 0.17f, centre.y - radius * 0.17f, radius * 0.34f, radius * 0.34f);
            g.setColour(palette.accent.withAlpha(0.5f));
            g.drawEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 1.4f);
        }
        // tape path
        g.setColour(palette.accent.withAlpha(0.75f));
        g.drawLine(xs[0], area.getBottom() - 10.0f, xs[1], area.getBottom() - 10.0f, 3.0f);
    }

    void drawTube(juce::Graphics& g, juce::Rectangle<float> area)
    {
        const auto glow = juce::jlimit(0.10f, 1.0f, parameter("drive") * 0.008f + level * 0.6f);
        const auto centre = area.getCentre();
        g.setColour(palette.accentGlow.withAlpha(glow * 0.22f));
        g.fillEllipse(juce::Rectangle<float>(area.getHeight() * 1.5f, area.getHeight() * 1.5f).withCentre(centre));

        auto tube = juce::Rectangle<float>(area.getHeight() * 0.46f, area.getHeight() * 0.86f).withCentre(centre);
        juce::ColourGradient glass(palette.accentGlow.withAlpha(0.75f), tube.getCentreX(), tube.getBottom(),
                                   juce::Colour(0xff25100a).withAlpha(0.5f), tube.getCentreX(), tube.getY(), false);
        g.setGradientFill(glass);
        g.fillRoundedRectangle(tube, tube.getWidth() * 0.42f);
        g.setColour(palette.cream.withAlpha(0.45f));
        g.drawRoundedRectangle(tube.reduced(2.5f), tube.getWidth() * 0.40f, 1.4f);
        for (int line = 0; line < 4; ++line)
        {
            const auto x = tube.getX() + tube.getWidth() * (0.22f + 0.19f * static_cast<float>(line));
            g.setColour(palette.accentGlow.withAlpha(0.7f * glow));
            g.drawLine(x, tube.getY() + tube.getHeight() * 0.22f, x, tube.getBottom() - tube.getHeight() * 0.12f, 2.0f);
        }
    }

    void drawOptoCell(juce::Graphics& g, juce::Rectangle<float> area)
    {
        const auto centre = area.getCentre();
        const auto glow = juce::jlimit(0.08f, 1.0f, parameter("peak_reduction") * 0.012f + level * 0.6f);
        const auto radius = juce::jmin(area.getWidth(), area.getHeight()) * 0.34f;
        g.setColour(palette.accentGlow.withAlpha(glow * 0.30f));
        g.fillEllipse(juce::Rectangle<float>(radius * 3.0f, radius * 3.0f).withCentre(centre));
        juce::ColourGradient cell(palette.accentGlow.withAlpha(0.95f), centre.x, centre.y,
                                  palette.accent.withAlpha(0.10f), centre.x + radius, centre.y + radius, true);
        g.setGradientFill(cell);
        g.fillEllipse(juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(centre));
        g.setColour(palette.cream.withAlpha(0.5f));
        g.drawEllipse(juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(centre), 1.5f);
    }

    void drawCurve(juce::Graphics& g, juce::Rectangle<float> area)
    {
        g.setColour(juce::Colour(0xff0d1113));
        g.fillRoundedRectangle(area, 5.0f);
        for (int x = 1; x < 10; ++x)
        {
            g.setColour(palette.bronze.withAlpha(0.16f));
            g.drawVerticalLine(juce::roundToInt(area.getX() + static_cast<float>(x) * area.getWidth() / 10.0f),
                               area.getY(), area.getBottom());
        }
        g.setColour(palette.bronze.withAlpha(0.28f));
        g.drawHorizontalLine(juce::roundToInt(area.getCentreY()), area.getX(), area.getRight());

        juce::Path curve;
        for (int x = 0; x <= juce::roundToInt(area.getWidth()); ++x)
        {
            const auto n = static_cast<float>(x) / area.getWidth();
            const auto low = parameter("low_boost") * std::exp(-n * 8.0f)
                           - parameter("low_cut") * 0.7f * std::exp(-n * 11.0f);
            const auto mid = parameter("mid_gain") * std::exp(-std::pow((n - 0.52f) / 0.16f, 2.0f));
            const auto high = parameter("high_boost") * std::exp(-(1.0f - n) * 7.0f)
                            - parameter("high_cut") * 0.7f * std::exp(-(1.0f - n) * 10.0f);
            const auto y = area.getCentreY()
                         - juce::jlimit(-15.0f, 15.0f, low + mid + high) * area.getHeight() / 36.0f;
            if (x == 0) curve.startNewSubPath(area.getX(), y);
            else curve.lineTo(area.getX() + static_cast<float>(x), y);
        }
        g.setColour(palette.accent.withAlpha(0.22f));
        g.strokePath(curve, juce::PathStrokeType(7.0f));
        g.setColour(palette.accentGlow);
        g.strokePath(curve, juce::PathStrokeType(2.0f));
    }

    void drawPlate(juce::Graphics& g, juce::Rectangle<float> area)
    {
        // Dark chamber holding a suspended steel plate, lit from within by the
        // product accent; the brass surround comes from the backdrop.
        auto chamber = area;
        juce::ColourGradient inside(juce::Colour(0xff1d1420), chamber.getCentreX(), chamber.getY(),
                                    juce::Colour(0xff08060a), chamber.getCentreX(), chamber.getBottom(), false);
        g.setGradientFill(inside);
        g.fillRoundedRectangle(chamber, 5.0f);
        // floor reflection of the lamps
        juce::ColourGradient floor(palette.accentGlow.withAlpha(0.22f), chamber.getCentreX(), chamber.getBottom(),
                                   juce::Colours::transparentBlack, chamber.getCentreX(), chamber.getBottom() - chamber.getHeight() * 0.35f, false);
        g.setGradientFill(floor);
        g.fillRect(chamber.withTop(chamber.getBottom() - chamber.getHeight() * 0.35f));

        const auto glow = 0.32f + level * 0.55f;

        // lamps along the top edge wash the cavity
        for (int i = 0; i < 5; ++i)
        {
            const auto x = chamber.getX() + chamber.getWidth() * (0.10f + 0.20f * static_cast<float>(i));
            juce::ColourGradient wash(palette.accentGlow.withAlpha(0.42f * glow), x, chamber.getY(),
                                      juce::Colours::transparentBlack, x, chamber.getBottom(), false);
            g.setGradientFill(wash);
            g.fillEllipse(x - chamber.getHeight() * 0.55f, chamber.getY() - chamber.getHeight() * 0.1f,
                          chamber.getHeight() * 1.1f, chamber.getHeight() * 1.2f);
            g.setColour(palette.accentGlow.withAlpha(0.85f));
            g.fillEllipse(x - 3.0f, chamber.getY() + 3.0f, 6.0f, 6.0f);
        }

        // the plate itself: brushed steel, hung on tensioners
        auto plate = chamber.reduced(chamber.getWidth() * 0.14f, chamber.getHeight() * 0.22f);
        juce::ColourGradient steel(juce::Colour(0xffa9a2ad), plate.getX(), plate.getY(),
                                   juce::Colour(0xff3e3742), plate.getRight(), plate.getBottom(), false);
        steel.addColour(0.45, juce::Colour(0xff7d757f).interpolatedWith(palette.accent, 0.18f));
        g.setGradientFill(steel);
        g.fillRoundedRectangle(plate, 3.0f);

        g.setColour(juce::Colours::white.withAlpha(0.06f));
        for (float x = plate.getX() + 2.0f; x < plate.getRight(); x += 3.0f)
            g.drawLine(x, plate.getY() + 1.0f, x, plate.getBottom() - 1.0f, 0.6f);

        // vibration of the plate follows the signal
        g.setColour(palette.accentGlow.withAlpha(0.30f + level * 0.4f));
        for (int i = 1; i < 5; ++i)
        {
            const auto y = plate.getY() + plate.getHeight() * static_cast<float>(i) / 5.0f;
            const auto lift = std::sin(phase * 1.9f + static_cast<float>(i)) * level * 5.0f;
            g.drawLine(plate.getX() + 8.0f, y, plate.getRight() - 8.0f, y + lift, 1.0f);
        }

        g.setColour(juce::Colour(0xff2a2d30).withAlpha(0.8f));
        g.drawRoundedRectangle(plate.reduced(0.5f), 3.0f, 1.2f);

        // tensioning rods top and bottom
        g.setColour(juce::Colour(0xffcdd2d6).withAlpha(0.75f));
        for (int i = 0; i < 4; ++i)
        {
            const auto x = plate.getX() + plate.getWidth() * (0.12f + 0.253f * static_cast<float>(i));
            g.drawLine(x, chamber.getY() + 8.0f, x, plate.getY(), 1.6f);
            g.drawLine(x, plate.getBottom(), x, chamber.getBottom() - 8.0f, 1.6f);
        }

        drawHexEmblem(g, juce::Rectangle<float>(plate.getHeight() * 0.62f, plate.getHeight() * 0.62f)
                             .withCentre(plate.getCentre()), juce::Colour(0xff33373a).withAlpha(0.85f));

        g.setColour(juce::Colours::black.withAlpha(0.35f));
        g.drawRoundedRectangle(chamber.reduced(0.6f), 5.0f, 1.4f);
    }

    void drawSignalPath(juce::Graphics& g, juce::Rectangle<float> area)
    {
        const std::array<juce::String, 5> labels { "INPUT", "PREAMP", "FILTER", "EQ", "COLOR" };
        const auto y = area.getCentreY();
        const auto radius = juce::jmin(area.getHeight() * 0.34f, area.getWidth() * 0.07f);
        for (int index = 0; index < 5; ++index)
        {
            const auto x = area.getX() + (static_cast<float>(index) + 0.5f) * area.getWidth() / 5.0f;
            const auto lit = level > static_cast<float>(index) * 0.16f;
            g.setColour(palette.accent.withAlpha(lit ? 0.30f : 0.14f));
            g.fillEllipse(x - radius * 1.35f, y - radius * 1.35f, radius * 2.7f, radius * 2.7f);
            g.setColour(lit ? palette.accentGlow : palette.accent.withAlpha(0.6f));
            g.drawEllipse(x - radius, y - radius, radius * 2.0f, radius * 2.0f, 1.6f);
            g.setColour(palette.cream);
            g.setFont(labelFont(juce::jmin(11.0f, radius * 0.52f), true, 0.9f));
            g.drawText(labels[static_cast<size_t>(index)],
                       juce::Rectangle<float>(radius * 3.0f, 14.0f).withCentre({ x, y }),
                       juce::Justification::centred, false);
            if (index < 4)
            {
                g.setColour(palette.bronze);
                g.drawArrow({ x + radius * 1.5f, y, x + area.getWidth() / 5.0f - radius * 1.5f, y },
                            1.4f, 7.0f, 6.0f);
            }
        }
    }

    void drawScope(juce::Graphics& g, juce::Rectangle<float> area)
    {
        g.setColour(juce::Colour(0xff0d1113));
        g.fillRoundedRectangle(area, 5.0f);
        for (int i = 1; i < 8; ++i)
        {
            g.setColour(palette.bronze.withAlpha(0.14f));
            g.drawVerticalLine(juce::roundToInt(area.getX() + static_cast<float>(i) * area.getWidth() / 8.0f),
                               area.getY(), area.getBottom());
        }
        g.setColour(palette.bronze.withAlpha(0.26f));
        g.drawHorizontalLine(juce::roundToInt(area.getCentreY()), area.getX(), area.getRight());

        juce::Path wave;
        for (int x = 0; x <= juce::roundToInt(area.getWidth()); ++x)
        {
            const auto n = static_cast<float>(x) / area.getWidth();
            const auto envelope = std::exp(-std::pow((n - 0.5f) / 0.42f, 2.0f));
            const auto y = area.getCentreY()
                         - std::sin(n * 26.0f + phase) * envelope * area.getHeight() * 0.36f * (0.18f + level);
            if (x == 0) wave.startNewSubPath(area.getX(), y);
            else wave.lineTo(area.getX() + static_cast<float>(x), y);
        }
        g.setColour(palette.accent.withAlpha(0.22f));
        g.strokePath(wave, juce::PathStrokeType(6.0f));
        g.setColour(palette.accentGlow);
        g.strokePath(wave, juce::PathStrokeType(1.9f));
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HeroVisual)
};

/** Product accent: the one colour that separates the family members. */
juce::Colour accentFor(const juce::String& id)
{
    if (id == "A02") return juce::Colour(0xffe2571f);
    if (id == "A03") return juce::Colour(0xffe0a12f);
    if (id == "A04") return juce::Colour(0xffd97b2a);
    if (id == "A05") return juce::Colour(0xffe8944a);
    if (id == "A06") return juce::Colour(0xff3fbf5a);
    if (id == "A07") return juce::Colour(0xff46b6d8);
    if (id == "A08") return juce::Colour(0xff8f6cf0);
    if (id == "A09") return juce::Colour(0xffd3b26a);
    if (id == "A10") return juce::Colour(0xffc558d8);
    return cAmber;
}

const juce::Colour cDark { 0xff2a2825 };

/** Legend text for a scale value in the product's unit, in the printed style
    used on the HERITAGE faceplate: 1k2, +12, 0.5. */
juce::String legendText(float value, const juce::String& unit, bool signedScale)
{
    juce::String text;
    if (unit == "Hz" && std::abs(value) >= 1000.0f)
    {
        const auto k = value / 1000.0f;
        const auto whole = static_cast<int>(k);
        const auto tenth = juce::roundToInt((k - static_cast<float>(whole)) * 10.0f);
        text = tenth == 0 || tenth == 10 ? juce::String(tenth == 10 ? whole + 1 : whole) + "k"
                                         : juce::String(whole) + "k" + juce::String(tenth);
    }
    else if (std::abs(value) >= 10.0f || value == 0.0f)
        text = juce::String(juce::roundToInt(value));
    else
        text = juce::String(value, 1).trimCharactersAtEnd("0").trimCharactersAtEnd(".");
    if (signedScale && value > 0.0f) text = "+" + text;
    return text;
}

juce::String choiceLabel(const juce::String& raw)
{
    auto text = raw.trim();
    if (text.startsWithIgnoreCase("iron_")) text = text.substring(5);
    if (text.equalsIgnoreCase("medium")) text = "MED";
    if (text.equalsIgnoreCase("ms")) text = "M/S";
    if (text.equalsIgnoreCase("solid")) text = "SOLID";
    return text.replaceCharacter('_', ' ').toUpperCase();
}
}

// ---------------------------------------------------------------- Surface

class AnalogPageEditor::Surface final : public hw::AnalogChassis
{
public:
    explicit Surface(ProductHost owner)
        : AnalogChassis(owner),
          backdrop(backdropImage(owner.spec->id)),
          palette(Palette::forProduct(owner.spec->id)),
          accent(accentFor(owner.spec->id))
    {
        keyAccent = accent;
        const auto& id = (*host.spec).id;
        if (id == "A02")      buildIronPre();
        else if (id == "A03") buildConsoleOne();
        else if (id == "A04") buildReelsat();
        else if (id == "A05") buildValveDrive();
        else if (id == "A06") buildStrikeFet();
        else if (id == "A07") buildLumenOpto();
        else if (id == "A08") buildBusforge();
        else if (id == "A09") buildSilkPassive();
        else                  buildPlateFour();

        buildTopBar();
        setSize(backdrop.isValid() ? backdrop.getWidth() : 1536,
                backdrop.isValid() ? backdrop.getHeight() : 1024);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff0a0908));
        if (backdrop.isValid())
            g.drawImage(backdrop, getLocalBounds().toFloat(), juce::RectanglePlacement::stretchToFit);

        drawTopBar(g);

        for (const auto& line : dividers)
        {
            g.setColour(juce::Colours::black.withAlpha(0.55f));
            g.drawLine(line, 1.0f);
            g.setColour(juce::Colours::white.withAlpha(0.05f));
            g.drawLine({ line.getStart().translated(0.0f, 1.0f), line.getEnd().translated(0.0f, 1.0f) }, 1.0f);
        }
        for (const auto& text : texts)
        {
            g.setColour(text.colour);
            g.setFont(text.display ? displayFont(text.size) : labelFont(text.size, text.bold, 1.0f));
            g.drawText(text.text, text.area, text.justification, false);
        }
        for (const auto& lamp : pilotLamps)
        {
            g.setColour(accent.withAlpha(0.35f));
            g.fillEllipse(lamp.expanded(5.0f));
            g.setColour(accent.brighter(0.5f));
            g.fillEllipse(lamp);
            g.setColour(juce::Colours::white.withAlpha(0.7f));
            g.fillEllipse(lamp.reduced(lamp.getWidth() * 0.32f).translated(-1.0f, -1.0f));
        }
        for (const auto& badge : badges)
        {
            const auto side = juce::jmin(badge.getWidth(), badge.getHeight()) * 0.62f;
            drawHexBadge(g, juce::Rectangle<float>(side, side).withCentre(badge.getCentre()),
                         cGold.withAlpha(0.75f), 3.0f);
        }
        for (const auto& plate : plates)
        {
            g.setColour(juce::Colour(0xff0f0e0c));
            g.fillRoundedRectangle(plate, 6.0f);
            g.setColour(juce::Colour(0xff2a2723));
            g.drawRoundedRectangle(plate, 6.0f, 1.0f);
            brandPrint.draw(g, plate.withHeight(plate.getHeight() * 0.62f).translated(0.0f, 6.0f).reduced(14.0f, 0.0f), 0.8f);
            g.setColour(juce::Colour(0xffcfc4a8));
            g.setFont(labelFont(juce::jmin(15.0f, plate.getHeight() * 0.12f), true, 1.0f));
            g.drawText((*host.spec).primaryRole.toUpperCase(),
                       plate.withHeight(20.0f).translated(0.0f, plate.getHeight() * 0.70f),
                       juce::Justification::centred, false);
        }
        for (const auto& print : prints)
            brandPrint.draw(g, print, 0.55f);
    }

    void resized() override
    {
        const auto keyHeight = juce::jmin(44, header.getHeight() - 16);
        const auto y = header.getCentreY() - keyHeight / 2;
        layoutTopBar(navX, y, keyHeight, header.getRight() - 14);
    }

private:
    struct Text
    {
        juce::Rectangle<float> area;
        juce::String text;
        float size;
        bool bold;
        juce::Colour colour;
        juce::Justification justification { juce::Justification::centred };
        bool display = false;   // engraved serif for the product name
    };

    // ------------------------------------------------------------ top bar

    void drawTopBar(juce::Graphics& g)
    {
        const auto h = header.toFloat();
        const auto logo = juce::Rectangle<float>(h.getX() + 16.0f, h.getCentreY() - 30.0f, 190.0f, 60.0f);
        brandPrint.draw(g, logo, 0.86f);

        g.setColour(juce::Colour(0xffd0c8b2));
        g.setFont(labelFont(23.0f, true, 1.0f));
        g.drawText((*host.spec).displayName.toUpperCase(),
                   juce::Rectangle<float>(h.getX() + 236.0f, h.getCentreY() - 24.0f, 290.0f, 26.0f),
                   juce::Justification::centredLeft, false);
        g.setColour(accent);
        g.setFont(labelFont(13.0f, true, 1.0f));
        g.drawText((*host.spec).primaryRole.toUpperCase(),
                   juce::Rectangle<float>(h.getX() + 236.0f, h.getCentreY() + 4.0f, 290.0f, 16.0f),
                   juce::Justification::centredLeft, false);
    }

    void setHeader(juce::Rectangle<int> area)
    {
        header = area;
        // preset group sits centred in the bar, never over the title block
        navX = juce::jmax(header.getX() + 590, header.getCentreX() - 191);
    }

    // ---------------------------------------------------------- captions

    void caption(float x, float y, float w, const juce::String& text, float size = 16.0f,
                 juce::Colour colour = cText, bool bold = true)
    {
        texts.push_back({ { x, y, w, size + 6.0f }, text, size, bold, colour });
    }

    void divider(float x1, float y1, float x2, float y2) { dividers.push_back({ x1, y1, x2, y2 }); }

    void titleBlock(float x, float y, float w, float titleSize)
    {
        texts.push_back({ { x, y, w, titleSize + 8.0f }, (*host.spec).displayName.toUpperCase(),
                          titleSize, true, juce::Colour(0xffe6dcc4), juce::Justification::centred, true });
        texts.push_back({ { x, y + titleSize + 10.0f, w, 20.0f }, (*host.spec).primaryRole.toUpperCase(),
                          14.0f, true, accent });
        // engraved rules either side of the name, as on the faceplate
        juce::GlyphArrangement measure;
        measure.addLineOfText(displayFont(titleSize), (*host.spec).displayName.toUpperCase(), 0.0f, 0.0f);
        const auto nameWidth = measure.getBoundingBox(0, -1, true).getWidth();
        const auto mid = y + titleSize * 0.55f;
        divider(x + w * 0.5f - nameWidth * 0.5f - 200.0f, mid, x + w * 0.5f - nameWidth * 0.5f - 40.0f, mid);
        divider(x + w * 0.5f + nameWidth * 0.5f + 40.0f, mid, x + w * 0.5f + nameWidth * 0.5f + 200.0f, mid);
    }

    /** Small lit pilot lamp printed next to a section title. */
    void pilot(float cx, float cy) { pilotLamps.push_back(juce::Rectangle<float>(9.0f, 9.0f).withCentre({ cx, cy })); }

    /** Two end legends printed under the sweep instead of a numeric ring. */
    static std::vector<HeritageKnob::Legend> ends(const juce::String& low, const juce::String& high)
    {
        return { { low, -135.0f, 10.0f }, { high, 135.0f, 10.0f } };
    }

    // ------------------------------------------------------------- knobs

    /** Five printed legends spread over the sweep, taken from the parameter's
        own range so skewed frequency scales print the right numbers. */
    static float niceValue(float v)
    {
        if (v == 0.0f) return 0.0f;
        const auto magnitude = std::pow(10.0f, std::floor(std::log10(std::abs(v))));
        const auto n = std::abs(v) / magnitude;
        // the 1 / 1.5 / 2 / 3 / 5 / 7 / 10 series printed on hardware scales
        const float rounded = n < 1.25f ? 1.0f : n < 1.75f ? 1.5f : n < 2.5f ? 2.0f : n < 4.0f ? 3.0f
                            : n < 6.0f ? 5.0f : n < 8.5f ? 7.0f : 10.0f;
        return (v < 0.0f ? -1.0f : 1.0f) * rounded * magnitude;
    }

    std::vector<HeritageKnob::Legend> autoLegends(const ParameterDescriptor& d, bool compact)
    {
        const auto* parameter = (*host.state).getParameter(d.id);
        const auto signedScale = d.minimum < 0.0f;
        auto fromT = [&](float t) { return parameter != nullptr ? parameter->convertFrom0to1(t) : juce::jmap(t, d.minimum, d.maximum); };
        auto toT = [&](float v) { return parameter != nullptr ? parameter->convertTo0to1(v) : juce::jmap(v, d.minimum, d.maximum, 0.0f, 1.0f); };

        std::vector<HeritageKnob::Legend> legends;
        juce::StringArray used;
        const auto marks = compact ? 3 : 5;
        for (int i = 0; i < marks; ++i)
        {
            auto t = static_cast<float>(i) / static_cast<float>(marks - 1);
            auto value = fromT(t);
            if (i > 0 && i < marks - 1)
            {
                // interior marks read as round numbers, printed at their true angle
                value = signedScale && i == 2 ? 0.0f : niceValue(value);
                t = juce::jlimit(0.05f, 0.95f, toT(value));
            }
            // no number directly under the caption on a one-sided scale
            if (i == marks / 2 && ! signedScale) continue;
            const auto text = legendText(value, d.unit, signedScale);
            if (used.contains(text)) continue;
            used.add(text);
            legends.push_back({ text, startDegrees + sweepDegrees * t, 6.0f });
        }
        return legends;
    }

    HeritageKnob* knob(const juce::String& id, float cx, float cy, float size, juce::Colour cap,
                       juce::Colour arc = {}, const juce::String& titleOverride = {},
                       std::vector<HeritageKnob::Legend> legends = {}, const juce::String& unitOverride = {})
    {
        const auto* d = find((*host.spec), id);
        if (d == nullptr) return nullptr;
        if (legends.empty()) legends = autoLegends(*d, size < 52.0f);

        std::unique_ptr<HeritageKnob> made;
        addKnob(made, id, size, cap, arc, size >= 60.0f ? 11 : 9, size >= 60.0f ? 2 : 1,
                std::move(legends), size >= 60.0f ? 15.0f : 14.0f);
        if (made == nullptr) return nullptr;
        auto* raw = made.get();
        knobs.push_back(std::move(made));
        // wider apron for the big trims so their outer legends are never clipped
        const auto box = static_cast<int>(size) + (size >= 70.0f ? 96 : 76);
        raw->setBounds(static_cast<int>(cx) - box / 2, static_cast<int>(cy) - box / 2, box, box);

        const auto title = titleOverride.isNotEmpty() ? titleOverride : d->name.toUpperCase();
        caption(cx - 110.0f, cy - size * 0.5f - 58.0f - (size >= 60.0f ? 8.0f : 0.0f), 220.0f, title,
                size >= 60.0f ? 18.0f : 16.0f);
        const auto unit = unitOverride.isNotEmpty() ? unitOverride : d->unit;
        if (unit.isNotEmpty())
            caption(cx - 60.0f, cy + size * 0.5f + 24.0f, 120.0f, unit, 13.0f, cMuted);
        return raw;
    }

    HeritageKnob* trimKnob(const juce::String& id, float cx, float cy, const juce::String& title)
    {
        auto* made = knob(id, cx, cy, 70.0f, cRed, {}, title,
                          evenly({ "-24", "-18", "-12", "-6", "0", "+6", "+12", "+18", "+24" }, 9.0f, true), "dB TRIM");
        return made;
    }

    HeritageKnob* mixKnob(float cx, float cy, float size = 60.0f)
    {
        auto legends = evenly({ "0", "25", "50", "75", "100" }, 4.0f);
        legends.push_back({ "DRY", -152.0f, 14.0f });
        legends.push_back({ "WET", 152.0f, 14.0f });
        return knob("mix", cx, cy, size, cDark, {}, "MIX", std::move(legends), "%");
    }

    // -------------------------------------------------------------- keys

    /** One machined key per choice, in a row or a column, with a caption. */
    void choiceKeys(const juce::String& id, juce::Rectangle<int> area, bool vertical,
                    const juce::String& title, juce::StringArray labels = {})
    {
        const auto* d = find((*host.spec), id);
        if (d == nullptr) return;
        if (labels.isEmpty())
            for (const auto& choice : d->choices) labels.add(choiceLabel(choice));

        if (title.isNotEmpty())
            caption(static_cast<float>(area.getX()) - 30.0f, static_cast<float>(area.getY()) - 28.0f,
                    static_cast<float>(area.getWidth()) + 60.0f, title, 15.0f);

        const auto count = juce::jmin(labels.size(), d->choices.size());
        const auto gap = 6;
        for (int i = 0; i < count; ++i)
        {
            std::unique_ptr<PanelKey> key;
            addChoiceKey(key, id, i, labels[i]);
            if (vertical)
            {
                const auto h = (area.getHeight() - gap * (count - 1)) / count;
                key->setBounds(area.getX(), area.getY() + i * (h + gap), area.getWidth(), h);
            }
            else
            {
                const auto w = juce::jmin(112, (area.getWidth() - gap * (count - 1)) / count);
                const auto total = w * count + gap * (count - 1);
                key->setBounds(area.getCentreX() - total / 2 + i * (w + gap), area.getY(), w, area.getHeight());
            }
            keyStore.push_back(std::move(key));
        }
    }

    /** Square panel lamp switching a boolean parameter, as the IN lamps on the faceplates. */
    void lampKey(const juce::String& id, float cx, float cy, float size = 18.0f)
    {
        if (find((*host.spec), id) == nullptr) return;
        std::unique_ptr<PanelLamp> lamp;
        addLamp(lamp, id, accent, true);
        lamp->setLampBounds(static_cast<int>(cx - size * 0.5f), static_cast<int>(cy - size * 0.5f),
                            static_cast<int>(size), static_cast<int>(size));
        lampStore.push_back(std::move(lamp));
    }

    void toggleKey(const juce::String& id, juce::Rectangle<int> area, const juce::String& text,
                   const juce::String& title = {})
    {
        if (find((*host.spec), id) == nullptr) return;
        std::unique_ptr<PanelKey> key;
        addKey(key, id, text, accent);
        key->setBounds(area);
        keyStore.push_back(std::move(key));
        if (title.isNotEmpty())
            caption(static_cast<float>(area.getX()) - 40.0f, static_cast<float>(area.getY()) - 28.0f,
                    static_cast<float>(area.getWidth()) + 80.0f, title, 15.0f);
    }

    // ------------------------------------------------------------ meters

    HeritageVu* vu(juce::Rectangle<int> area, std::function<float()> source, bool gainReduction = false)
    {
        auto meter = std::make_unique<HeritageVu>(std::move(source));
        meter->setGainReductionScale(gainReduction);
        meter->setBounds(area);
        addAndMakeVisible(*meter);
        auto* raw = meter.get();
        vus.push_back(std::move(meter));
        return raw;
    }

    HeritageVu* levelVu(juce::Rectangle<int> area, bool input = false)
    {
        return vu(area, [this, input]
        {
            const auto peak = input ? juce::jmax(host.peakIn(0), host.peakIn(1))
                                    : juce::jmax(host.peakOut(0), host.peakOut(1));
            return vuDeflection(peak);
        });
    }

    HeritageVu* reductionVu(juce::Rectangle<int> area)
    {
        return vu(area, [this] { return juce::jlimit(0.0f, 1.0f, -host.reductionDb() / 20.0f); }, true);
    }

    void ledMeter(juce::Rectangle<int> area, const juce::String& title, bool input)
    {
        auto meter = std::make_unique<HeritageLedMeter>(
            [this, input] { return ledDeflection(input ? host.peakIn(0) : host.peakOut(0)); },
            [this, input] { return ledDeflection(input ? host.peakIn(1) : host.peakOut(1)); });
        meter->setAccent(accent);
        meter->setBounds(area);
        addAndMakeVisible(*meter);
        leds.push_back(std::move(meter));
        caption(static_cast<float>(area.getX()) - 20.0f, static_cast<float>(area.getY()) - 28.0f,
                static_cast<float>(area.getWidth()) + 40.0f, title, 16.0f);
    }

    void heroVisual(juce::Rectangle<int> area)
    {
        hero = std::make_unique<HeroVisual>(host, palette);
        hero->setBounds(area);
        addAndMakeVisible(*hero);
    }

    void badge(float x, float y, float w, float h) { badges.push_back({ x, y, w, h }); }
    void plate(float x, float y, float w, float h) { plates.push_back({ x, y, w, h }); }
    void print(float x, float y, float w, float h) { prints.push_back({ x, y, w, h }); }

    // =============================================================== products
    // Coordinates are pixels in each product's own backdrop, measured from the
    // supplied artwork, so every control lands inside its machined bay.

    void buildIronPre()                                            // A02, 1536 x 1024
    {
        setHeader({ 10, 8, 1516, 120 });

        // left bay 20..305 x 145..710
        trimKnob("input", 162.0f, 268.0f, "INPUT");
        toggleKey("phase", { 132, 392, 60, 38 }, juce::String::fromUTF8("\xC3\x98"));
        caption(102.0f, 436.0f, 120.0f, "PHASE", 14.0f, cMuted);
        divider(60.0f, 468.0f, 265.0f, 468.0f);
        knob("hpf", 162.0f, 572.0f, 62.0f, cDark, {}, "HPF");
        lampKey("hpf_in", 236.0f, 500.0f);
        toggleKey("pad", { 112, 660, 100, 36 }, "PAD");
        badge(40.0f, 735.0f, 245.0f, 150.0f);

        // centre 322..1225 x 145..895
        levelVu({ 470, 168, 600, 236 });
        knob("drive", 520.0f, 540.0f, 84.0f, accent);
        knob("saturation", 860.0f, 540.0f, 84.0f, cDark, accent);
        knob("low_tone", 520.0f, 770.0f, 72.0f, cDark, accent);
        knob("high_tone", 860.0f, 770.0f, 72.0f, cDark, accent);
        choiceKeys("transformer", { 1085, 466, 120, 120 }, true, "TRANSFORMER");
        choiceKeys("impedance", { 1085, 636, 120, 120 }, true, "IMPEDANCE");
        choiceKeys("stereo_mode", { 1085, 806, 120, 36 }, false, "LINK", { "L/R", "M/S" });
        divider(1065.0f, 440.0f, 1065.0f, 860.0f);

        // right bay 1245..1515 x 145..895
        trimKnob("output", 1380.0f, 268.0f, "OUTPUT");
        mixKnob(1380.0f, 486.0f);
        ledMeter({ 1300, 598, 160, 272 }, "OUTPUT", false);

        // footer strip
        print(668.0f, 925.0f, 200.0f, 70.0f);
        toggleKey("bypass", { 1400, 940, 96, 40 }, "BYPASS");

    }

    void buildConsoleOne()                                         // A03, 1586 x 992
    {
        setHeader({ 50, 8, 1485, 80 });

        // left 55..265 x 100..905
        trimKnob("input", 160.0f, 218.0f, "INPUT");
        toggleKey("phase", { 130, 322, 60, 34 }, juce::String::fromUTF8("\xC3\x98"));
        knob("hpf", 160.0f, 470.0f, 62.0f, cDark, {}, "HPF");
        lampKey("hpf_in", 232.0f, 398.0f);
        ledMeter({ 95, 598, 130, 284 }, "INPUT", true);

        // preamp 280..525
        caption(280.0f, 118.0f, 245.0f, "PREAMP", 17.0f);
        knob("drive", 402.0f, 296.0f, 70.0f, accent);
        choiceKeys("stereo_mode", { 292, 590, 222, 38 }, false, "MODE");

        // equalizer 545..1070, three columns
        caption(545.0f, 118.0f, 525.0f, "EQUALIZER", 17.0f);
        lampKey("eq_in", 1048.0f, 129.0f);
        knob("low", 632.0f, 340.0f, 66.0f, cLow, {}, "LOW");
        knob("mid_freq", 807.0f, 300.0f, 60.0f, cDark, cMid, "MID");
        knob("mid", 807.0f, 520.0f, 66.0f, cMid, {}, "GAIN");
        knob("high", 982.0f, 340.0f, 66.0f, cHighMid, {}, "HIGH");

        // colour 1090..1315
        caption(1090.0f, 118.0f, 225.0f, "COLOR", 17.0f);
        knob("bus_color", 1202.0f, 300.0f, 70.0f, cRed, {}, "BUS COLOR");
        toggleKey("noise", { 1152, 480, 100, 38 }, "ON", "NOISE");

        // meter strip 280..1315 x 745..905
        levelVu({ 300, 755, 300, 140 }, true);
        plate(620.0f, 755.0f, 355.0f, 140.0f);
        levelVu({ 990, 755, 232, 140 });
        toggleKey("bypass", { 1232, 806, 78, 38 }, "BYP", "BYPASS");

        // right 1330..1530
        trimKnob("output", 1430.0f, 218.0f, "OUTPUT");
        mixKnob(1430.0f, 446.0f);
        ledMeter({ 1365, 598, 130, 284 }, "OUTPUT", false);

    }

    void buildReelsat()                                            // A04, 1536 x 1024
    {
        setHeader({ 50, 8, 1440, 77 });

        // left 50..245 x 95..730
        trimKnob("input", 147.0f, 208.0f, "INPUT");
        toggleKey("phase", { 117, 310, 60, 34 }, juce::String::fromUTF8("\xC3\x98"));
        knob("hpf", 147.0f, 448.0f, 50.0f, cDark, {}, "HPF", ends("OFF", "300"), "Hz");
        ledMeter({ 82, 520, 130, 190 }, "", true);

        heroVisual({ 262, 104, 1024, 378 });

        // transport strip 258..1290 x 500..725, eight machined cells
        choiceKeys("formula", { 272, 550, 90, 122 }, true, "FORMULA");
        choiceKeys("speed", { 398, 550, 90, 122 }, true, "SPEED");
        caption(375.0f, 684.0f, 137.0f, "ips", 12.0f, cMuted);
        const std::array<std::pair<const char*, float>, 6> cells {{
            { "bias", 573.0f }, { "saturation", 697.0f }, { "wow", 820.0f },
            { "flutter", 942.0f }, { "head_bump", 1072.0f }, { "hf_rolloff", 1215.0f } }};
        for (const auto& [id, cx] : cells)
            knob(id, cx, 624.0f, 54.0f, cDark, accent);

        // lower deck 740..905
        knob("hiss", 255.0f, 832.0f, 50.0f, cDark, accent);
        levelVu({ 525, 752, 235, 140 }, true);
        levelVu({ 785, 752, 235, 140 });
        toggleKey("bypass", { 1040, 812, 96, 40 }, "BYPASS");
        plate(1150.0f, 752.0f, 270.0f, 143.0f);

        // right 1300..1490
        trimKnob("output", 1395.0f, 208.0f, "OUTPUT");
        mixKnob(1395.0f, 412.0f, 56.0f);
        ledMeter({ 1330, 520, 130, 190 }, "OUTPUT", false);

    }

    void buildValveDrive()                                         // A05, 1586 x 992
    {
        setHeader({ 40, 8, 1505, 92 });

        // left 45..260
        trimKnob("input", 152.0f, 222.0f, "INPUT");
        toggleKey("phase", { 122, 322, 60, 34 }, juce::String::fromUTF8("\xC3\x98"));
        knob("hpf", 152.0f, 468.0f, 50.0f, cDark, {}, "HPF", ends("OFF", "300"), "Hz");
        ledMeter({ 78, 560, 150, 300 }, "", true);

        titleBlock(265.0f, 122.0f, 1045.0f, 40.0f);
        toggleKey("bypass", { 1206, 132, 96, 40 }, "BYPASS");
        choiceKeys("topology", { 327, 290, 112, 130 }, true, "TOPOLOGY");
        heroVisual({ 546, 242, 488, 196 });
        knob("pre_emphasis", 1195.0f, 300.0f, 40.0f, cDark, accent, "PRE");
        knob("post_emphasis", 1195.0f, 400.0f, 40.0f, cDark, accent, "POST");

        // knob strip 275..1300 x 470..710, six cells
        knob("drive", 360.0f, 604.0f, 66.0f, cRed);
        knob("bias", 531.0f, 604.0f, 60.0f, cLowMid);
        knob("density", 702.0f, 604.0f, 60.0f, cMid);
        knob("harmonic_balance", 873.0f, 604.0f, 60.0f, cHighMid, {}, "EVEN / ODD");
        knob("tone", 1044.0f, 604.0f, 60.0f, cDark, accent);
        toggleKey("soft_clip", { 1165, 584, 100, 40 }, "ON", "SOFT CLIP");

        levelVu({ 285, 730, 295, 145 }, true);
        plate(615.0f, 730.0f, 350.0f, 145.0f);
        levelVu({ 1000, 730, 290, 145 });

        // right 1320..1540
        trimKnob("output", 1430.0f, 232.0f, "OUTPUT");
        mixKnob(1430.0f, 452.0f);
        ledMeter({ 1365, 598, 130, 272 }, "OUTPUT", false);

    }

    void buildStrikeFet()                                          // A06, 1536 x 1024
    {
        setHeader({ 25, 30, 1485, 75 });

        // left 30..245 x 120..905
        trimKnob("input", 137.0f, 248.0f, "INPUT");
        toggleKey("phase", { 74, 396, 54, 34 }, juce::String::fromUTF8("\xC3\x98"));
        toggleKey("external_sc", { 136, 396, 70, 34 }, "EXT SC");
        toggleKey("hpf", { 74, 440, 132, 34 }, "HPF");
        knob("sc_hpf", 137.0f, 584.0f, 60.0f, cDark, accent, "SC HPF");
        badge(55.0f, 712.0f, 165.0f, 175.0f);

        // centre 260..1275
        reductionVu({ 515, 150, 505, 215 });
        knob("attack", 450.0f, 520.0f, 80.0f, cDark, accent);
        knob("release", 690.0f, 520.0f, 80.0f, cDark, accent);
        knob("makeup", 930.0f, 520.0f, 80.0f, cDark, accent);
        knob("saturation", 1160.0f, 520.0f, 64.0f, accent);
        choiceKeys("ratio", { 360, 700, 800, 44 }, false, "RATIO");
        print(668.0f, 928.0f, 200.0f, 66.0f);
        toggleKey("bypass", { 1380, 938, 96, 40 }, "BYPASS");

        // right 1285..1505
        trimKnob("output", 1395.0f, 248.0f, "OUTPUT");
        mixKnob(1395.0f, 466.0f);
        ledMeter({ 1330, 588, 130, 284 }, "OUTPUT", false);

    }

    void buildLumenOpto()                                          // A07, 1535 x 1024
    {
        setHeader({ 40, 10, 1460, 85 });

        // left 45..310
        trimKnob("input", 177.0f, 232.0f, "INPUT");
        toggleKey("phase", { 147, 334, 60, 34 }, juce::String::fromUTF8("\xC3\x98"));
        knob("sc_hpf", 177.0f, 488.0f, 54.0f, cDark, accent, "SC HPF");
        ledMeter({ 110, 590, 140, 290 }, "", true);

        // centre 330..1215
        reductionVu({ 445, 160, 645, 215 });
        knob("peak_reduction", 442.0f, 546.0f, 66.0f, accent);
        knob("gain", 660.0f, 546.0f, 66.0f, cDark, accent);
        choiceKeys("response", { 790, 530, 170, 40 }, false, "RESPONSE");
        knob("release", 1100.0f, 546.0f, 66.0f, cDark, accent);
        knob("saturation", 492.0f, 806.0f, 56.0f, cDark, accent);
        knob("stereo_link", 770.0f, 806.0f, 56.0f, cDark, accent);
        print(960.0f, 745.0f, 220.0f, 100.0f);
        toggleKey("bypass", { 1380, 944, 96, 40 }, "BYPASS");

        // right 1225..1500
        trimKnob("output", 1362.0f, 254.0f, "OUTPUT");
        mixKnob(1362.0f, 474.0f);
        ledMeter({ 1292, 598, 140, 284 }, "OUTPUT", false);

    }

    void buildBusforge()                                           // A08, 1586 x 992
    {
        setHeader({ 10, 8, 1565, 77 });

        // left 30..260
        trimKnob("input", 145.0f, 212.0f, "INPUT");
        toggleKey("phase", { 115, 316, 60, 34 }, juce::String::fromUTF8("\xC3\x98"));
        knob("sc_hpf", 145.0f, 470.0f, 54.0f, cDark, accent, "SC HPF");
        ledMeter({ 78, 570, 135, 258 }, "", true);

        // centre 265..1310
        reductionVu({ 475, 175, 630, 215 });
        knob("threshold", 385.0f, 534.0f, 66.0f, accent);
        knob("attack", 640.0f, 534.0f, 66.0f, cDark, accent);
        knob("release", 900.0f, 534.0f, 66.0f, cDark, accent);
        knob("makeup", 1180.0f, 534.0f, 66.0f, cDark, accent);
        choiceKeys("ratio", { 560, 650, 460, 38 }, false, "RATIO");

        choiceKeys("knee", { 310, 740, 190, 40 }, false, "KNEE");
        toggleKey("auto_release", { 560, 740, 160, 40 }, "ON", "AUTO RELEASE");
        knob("stereo_link", 900.0f, 778.0f, 50.0f, cDark, accent);
        print(1040.0f, 720.0f, 250.0f, 105.0f);
        toggleKey("bypass", { 1420, 856, 96, 40 }, "BYPASS");

        // right 1320..1545
        trimKnob("output", 1432.0f, 214.0f, "OUTPUT");
        mixKnob(1432.0f, 420.0f, 56.0f);
        ledMeter({ 1365, 530, 135, 290 }, "", false);

    }

    void buildSilkPassive()                                        // A09, 1586 x 992
    {
        setHeader({ 40, 8, 1505, 62 });

        // left 45..240
        trimKnob("input", 142.0f, 202.0f, "INPUT");
        toggleKey("phase", { 112, 304, 60, 34 }, juce::String::fromUTF8("\xC3\x98"));
        knob("hpf", 142.0f, 456.0f, 50.0f, cDark, {}, "HPF", ends("OFF", "300"), "Hz");
        ledMeter({ 75, 555, 140, 330 }, "", true);

        titleBlock(250.0f, 86.0f, 1090.0f, 34.0f);

        caption(280.0f, 186.0f, 300.0f, "LOW", 17.0f, cLow);
        caption(645.0f, 186.0f, 300.0f, "MID PRESENCE", 17.0f, cMid);
        caption(1010.0f, 186.0f, 300.0f, "HIGH", 17.0f, cHighMid);
        knob("low_freq", 430.0f, 306.0f, 66.0f, cDark, cLow, "FREQUENCY");
        knob("low_boost", 345.0f, 506.0f, 58.0f, cLow, {}, "BOOST");
        knob("low_cut", 515.0f, 506.0f, 58.0f, cDark, cLow, "CUT");
        knob("mid_freq", 795.0f, 306.0f, 66.0f, cDark, cMid, "FREQUENCY");
        knob("mid_gain", 795.0f, 506.0f, 62.0f, cMid, {}, "GAIN");
        knob("high_freq", 1160.0f, 306.0f, 66.0f, cDark, cHighMid, "FREQUENCY");
        knob("high_boost", 1075.0f, 506.0f, 58.0f, cHighMid, {}, "BOOST");
        knob("high_cut", 1245.0f, 506.0f, 58.0f, cDark, cHighMid, "CUT");
        divider(612.0f, 200.0f, 612.0f, 640.0f);
        divider(977.0f, 200.0f, 977.0f, 640.0f);

        // option strip 250..1340 x 660..810
        choiceKeys("output_stage", { 275, 706, 200, 40 }, false, "OUTPUT STAGE");
        knob("drive", 622.0f, 748.0f, 50.0f, cDark, accent);
        toggleKey("bypass", { 1170, 706, 100, 40 }, "BYPASS");

        // meter row 815..915
        levelVu({ 405, 820, 240, 92 }, true);
        plate(660.0f, 820.0f, 270.0f, 92.0f);
        levelVu({ 945, 820, 245, 92 });

        // right 1350..1545
        trimKnob("output", 1447.0f, 196.0f, "OUTPUT");
        mixKnob(1447.0f, 410.0f, 56.0f);
        ledMeter({ 1380, 520, 135, 370 }, "", false);

    }

    void buildPlateFour()                                          // A10, 1584 x 993
    {
        setHeader({ 40, 8, 1505, 77 });

        // left bay 45..270: input trim, phase / hpf keys, input meter
        trimKnob("input", 150.0f, 205.0f, "INPUT");
        pilot(236.0f, 127.0f);
        toggleKey("phase", { 70, 305, 74, 36 }, "PHASE");
        toggleKey("hpf", { 160, 305, 74, 36 }, "HPF");
        divider(70.0f, 358.0f, 240.0f, 358.0f);
        ledMeter({ 72, 405, 168, 290 }, "", true);

        // centre: engraved name, chamber, 2 x 4 knob grid, plate character
        titleBlock(285.0f, 98.0f, 1010.0f, 34.0f);
        caption(640.0f, 172.0f, 300.0f, "PLATE CHAMBER", 14.5f, juce::Colour(0xffe6dcc4));
        heroVisual({ 336, 196, 914, 204 });

        const std::array<std::tuple<const char*, float, const char*, const char*>, 4> upper {{
            { "pre_delay", 400.0f, "0 ms", "500" }, { "decay", 597.0f, "0.4 s", "8" },
            { "damp", 792.0f, "LOW", "HIGH" },      { "bass_cut", 995.0f, "20 Hz", "500" } }};
        const std::array<std::tuple<const char*, float, const char*, const char*>, 4> lower {{
            { "drive", 400.0f, "0", "100" },        { "crosstalk", 597.0f, "0%", "100%" },
            { "noise", 792.0f, "OFF", "MAX" },      { "width", 995.0f, "MONO", "WIDE" } }};
        for (const auto& [id, cx, lo, hi] : upper) knob(id, cx, 540.0f, 54.0f, cDark, accent, {}, ends(lo, hi), " ");
        for (const auto& [id, cx, lo, hi] : lower) knob(id, cx, 686.0f, 54.0f, cDark, accent, {}, ends(lo, hi), " ");
        for (float x : { 498.0f, 695.0f, 893.0f }) divider(x, 462.0f, x, 735.0f);
        divider(300.0f, 607.0f, 1092.0f, 607.0f);
        choiceKeys("plate", { 1130, 520, 115, 180 }, true, "PLATE CHARACTER");

        // right bay 1305..1545: output trim, mix, output meter
        trimKnob("output", 1418.0f, 205.0f, "OUTPUT");
        pilot(1505.0f, 127.0f);
        mixKnob(1418.0f, 400.0f, 56.0f);
        ledMeter({ 1345, 500, 168, 232 }, "", false);

        // lower deck 760..905: VU, era and tension, nameplate, treble, bypass, VU
        levelVu({ 60, 772, 340, 122 }, true);
        pilot(388.0f, 826.0f);
        choiceKeys("era", { 436, 792, 92, 84 }, true, "");
        choiceKeys("tension", { 546, 782, 92, 104 }, true, "");
        print(660.0f, 775.0f, 255.0f, 115.0f);
        knob("treble", 985.0f, 840.0f, 40.0f, cDark, accent, "TREBLE", ends("-12", "+12"), " ");
        toggleKey("bypass", { 1058, 812, 80, 40 }, "BYPASS");
        levelVu({ 1180, 772, 340, 122 });
        pilot(1508.0f, 826.0f);
    }

    juce::Image backdrop;
    Palette palette;
    juce::Colour accent;

    juce::Rectangle<int> header;
    int navX = 0;

    std::vector<Text> texts;
    std::vector<juce::Line<float>> dividers;
    std::vector<juce::Rectangle<float>> badges, plates, prints, pilotLamps;

    std::vector<std::unique_ptr<HeritageKnob>> knobs;
    std::vector<std::unique_ptr<PanelKey>> keyStore;
    std::vector<std::unique_ptr<PanelLamp>> lampStore;
    std::vector<std::unique_ptr<HeritageVu>> vus;
    std::vector<std::unique_ptr<HeritageLedMeter>> leds;
    std::unique_ptr<HeroVisual> hero;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Surface)
};

// ----------------------------------------------------------------- editor

std::unique_ptr<juce::Component> AnalogPageEditor::createSurface(ProductHost host)
{
    return std::make_unique<Surface>(std::move(host));
}

juce::Rectangle<float> AnalogPageEditor::surfaceSize(const juce::String& productId)
{
    const auto backdrop = backdropImage(productId);
    return backdrop.isValid() ? juce::Rectangle<float>(static_cast<float>(backdrop.getWidth()),
                                                       static_cast<float>(backdrop.getHeight()))
                              : juce::Rectangle<float>(1536.0f, 1024.0f);
}

AnalogPageEditor::AnalogPageEditor(juce::AudioProcessor& owner, ProductHost host)
    : AudioProcessorEditor(owner), surface(std::make_unique<Surface>(std::move(host)))
{
    addAndMakeVisible(*surface);
    // One activation page for the whole bundle: the first plug-in the customer
    // opens asks for the key, and every other one is already unlocked.
    gate = std::make_unique<ActivationView>(owner.getName(), [this] { if (gate != nullptr) gate->setVisible(false); });
    addAndMakeVisible(*gate);
    gate->setVisible(! licensing::LicenseClient::getInstance().isLicensed());
    setResizable(true, true);
    const auto ratio = static_cast<double>(surface->getWidth()) / surface->getHeight();
    setResizeLimits(960, juce::roundToInt(960 / ratio), 1920, juce::roundToInt(1920 / ratio));
    getConstrainer()->setFixedAspectRatio(ratio);
    setSize(1280, juce::roundToInt(1280 / ratio));
}

AnalogPageEditor::~AnalogPageEditor() = default;

void AnalogPageEditor::paint(juce::Graphics& g) { g.fillAll(juce::Colour(0xff080807)); }

void AnalogPageEditor::resized()
{
    if (gate != nullptr) gate->setBounds(getLocalBounds());
    const auto scale = juce::jmin(static_cast<float>(getWidth()) / static_cast<float>(surface->getWidth()),
                                  static_cast<float>(getHeight()) / static_cast<float>(surface->getHeight()));
    surface->setTransform(juce::AffineTransform::scale(scale));
    surface->setTopLeftPosition(0, 0);
}
}
