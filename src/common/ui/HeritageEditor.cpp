#include "HeritageEditor.h"

#include "ActivationView.h"

#include "AnalogChassis.h"

#include <array>
#include <cmath>

namespace amanorsac
{
namespace
{
using namespace analog;
using namespace hw;

// The supplied backdrop and the reference markup share one 1536x1024 stage, so
// every coordinate below is taken straight from that layout.
constexpr float stageWidth = 1536.0f;
constexpr float stageHeight = 1024.0f;
}


// ---------------------------------------------------------------- Surface

class HeritageEditor::Surface final : public hw::AnalogChassis
{
public:
    explicit Surface(ProductHost owner) : AnalogChassis(std::move(owner)), backdrop(backdropImage("A01"))
    {
        const auto& spec = (*host.spec);

        // ---------------------------------------------------------- trims
        addKnob(inputTrim, "input_trim", 70.0f, cRed, {}, 17, 2,
                evenly({ "-24", "-18", "-12", "-6", "0", "+6", "+12", "+18", "+24" }, 9.0f, true), 15.0f);
        addKnob(outputTrim, "output_trim", 70.0f, cRed, {}, 17, 2,
                evenly({ "-24", "-18", "-12", "-6", "0", "+6", "+12", "+18", "+24" }, 9.0f, true), 15.0f);
        addKnob(hpfKnob, "hpf", 70.0f, juce::Colour(0xff2a2825), {}, 9, 2,
                evenly({ "OFF", "40", "80", "160", "300" }, 6.0f), 15.5f);
        {
            auto legends = evenly({ "0", "25", "50", "75", "100" }, 4.0f);
            legends.push_back({ "DRY", -152.0f, 14.0f });
            legends.push_back({ "WET", 152.0f, 14.0f });
            addKnob(mixKnob, "mix", 70.0f, juce::Colour(0xff2a2825), {}, 9, 2, legends, 15.5f);
        }
        addKnob(lpfKnob, "lpf", 38.0f, juce::Colour(0xff2a2825), {}, 7, 1, {}, 11.0f);
        addKnob(driveKnob, "drive", 48.0f, juce::Colour(0xff2a2825), {}, 9, 1,
                { { "10", 135.0f, 0.0f } }, 11.0f);

        // ------------------------------------------------------- EQ bands
        const std::array<BandSpec, 5> bandSpecs {{
            { "low",     "LOW",      cLow,     "SHELF", { "30", "60", "110", "220" } },
            { "lowmid",  "LOW MID",  cLowMid,  "BELL",  { "80", "160", "360", "700" } },
            { "mid",     "MID",      cMid,     "BELL",  { "300", "600", "1k2", "2k5" } },
            { "highmid", "HIGH MID", cHighMid, "BELL",  { "1k2", "2k5", "5k", "8k" } },
            { "high",    "HIGH",     cHigh,    "SHELF", { "4k", "8k", "12k", "16k" } } }};

        for (size_t i = 0; i < bandSpecs.size(); ++i)
        {
            const auto& b = bandSpecs[i];
            bands[i].spec = b;
            const std::array<float, 4> angles { -45.0f, -102.0f, 45.0f, 102.0f };
            std::vector<HeritageKnob::Legend> freqLegends;
            for (size_t k = 0; k < b.frequencies.size(); ++k)
                freqLegends.push_back({ b.frequencies[k], angles[k], 0.0f });

            addKnob(bands[i].freq, "band." + b.id + ".freq", 64.0f, juce::Colour(0xff2a2825),
                    b.colour, 11, 1, freqLegends, 15.5f);
            addKnob(bands[i].gain, "band." + b.id + ".gain", 66.0f, b.colour, {}, 11, 1,
                    { { "-15", -135.0f, 0.0f }, { "0", 0.0f, 0.0f }, { "+15", 135.0f, 0.0f } }, 15.5f);
            const auto shapeId = "band." + b.id + (b.id == "high" ? ".slope" : ".q");
            addKnob(bands[i].shape, shapeId, 56.0f, juce::Colour(0xff2a2825), {}, 9, 1,
                    b.id == "high" ? std::vector<HeritageKnob::Legend>{ { "3", 135.0f, 0.0f } }
                                   : std::vector<HeritageKnob::Legend>{ { "0.3", -135.0f, 0.0f }, { "3", 135.0f, 0.0f } },
                    15.0f);
            addLamp(bands[i].enable, "band." + b.id + ".enabled", b.colour, true);
        }

        // ------------------------------------------------------- switches
        addLamp(eqInLamp, "eq_in", cAmber, true);
        addLamp(filterLamp, "filter_in", cAmber, true);
        addLamp(filterIndicator, "filter_in", cGreen, false);
        addKey(phaseLeft, "phase", juce::String::fromUTF8("\xC3\x98"), cAmber);
        addKey(phaseRight, "phase", juce::String::fromUTF8("\xC3\x98"), cAmber);

        addChoiceKey(slope18, "filter_slope", 0, "18 dB");
        addChoiceKey(slope24, "filter_slope", 1, "24 dB");
        addChoiceKey(autoGainOff, "auto_gain", 0, "OFF");
        addChoiceKey(autoGainOn, "auto_gain", 1, "ON");
        addChoiceKey(oversampleOff, "oversampling", 0, "OFF");
        addChoiceKey(oversample2x, "oversampling", 1, "2x");
        addChoiceKey(oversample4x, "oversampling", 2, "4x");

        stereoKey = std::make_unique<PanelKey>("L / R", cAmber);
        if (const auto* d = find(spec, "stereo_mode"))
        {
            juce::ignoreUnused(d);
            auto& state = (*host.state);
            const auto stereoId = host.id("stereo_mode");
            stereoKey->onClick = [&state, stereoId]
            {
                if (auto* p = state.getParameter(stereoId))
                {
                    const auto next = p->getValue() > 0.5f ? 0.0f : 1.0f;
                    p->beginChangeGesture(); p->setValueNotifyingHost(next); p->endChangeGesture();
                }
            };
            stereoKey->isOn = [&state, stereoId]
            {
                const auto* v = state.getRawParameterValue(stereoId);
                return v != nullptr && v->load() > 0.5f;
            };
        }
        addAndMakeVisible(*stereoKey);
        keys.push_back(stereoKey.get());

        characterRow = std::make_unique<RadioRow>(juce::StringArray { "CLEAN", "CONSOLE", "TUBE", "TAPE" }, cAmber);
        {
            auto& state = (*host.state);
            const auto characterId = host.id("character");
            characterRow->onSelect = [&state, characterId](int index)
            {
                if (auto* p = state.getParameter(characterId))
                {
                    p->beginChangeGesture();
                    p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(index)));
                    p->endChangeGesture();
                }
            };
            characterRow->selectedIndex = [&state, characterId]
            {
                const auto* v = state.getRawParameterValue(characterId);
                return v != nullptr ? juce::jlimit(0, 3, juce::roundToInt(v->load())) : 0;
            };
        }
        addAndMakeVisible(*characterRow);
        rows.push_back(characterRow.get());

        // --------------------------------------------------------- meters
        vuLeft = std::make_unique<HeritageVu>([this] { return vuLevel(host.peakOut(0)); });
        vuRight = std::make_unique<HeritageVu>([this] { return vuLevel(host.peakOut(1)); });
        addAndMakeVisible(*vuLeft);
        addAndMakeVisible(*vuRight);
        ledMeter = std::make_unique<HeritageLedMeter>(
            [this] { return ledLevel(host.peakOut(0)); },
            [this] { return ledLevel(host.peakOut(1)); });
        addAndMakeVisible(*ledMeter);


        buildTopBar();
        setSize(static_cast<int>(stageWidth), static_cast<int>(stageHeight));
    }


    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff0a0908));
        if (backdrop.isValid())
            g.drawImage(backdrop, getLocalBounds().toFloat(), juce::RectanglePlacement::stretchToFit);

        drawTopBar(g);
        drawSidePanels(g);
        drawBandLegends(g);
        drawStripLegends(g);
        drawNameplate(g);
    }

    void resized() override
    {
        // ------------------------------------------------- top bar (h 84)
        layoutTopBar(606, 20, 44, 1536 - 66);

        // ---------------------------------------------------- left panel
        const auto left = juce::Point<int>(36, 96);
        place(inputTrim, left, 103, 118, 70);
        phaseLeft->setBounds(left.x + 72, left.y + 192, 48, 46);
        place(hpfKnob, left, 100, 380, 70);
        filterLamp->setLampBounds(left.x + 152, left.y + 296, 14, 14);
        filterIndicator->setBounds(left.x + 88, left.y + 512, 16, 16);

        // --------------------------------------------------- right panel
        const auto right = juce::Point<int>(1536 - 36 - 206, 96);
        place(outputTrim, right, 103, 118, 70);
        phaseRight->setBounds(right.x + 78, right.y + 192, 48, 46);
        place(mixKnob, right, 100, 380, 70);
        ledMeter->setBounds(right.x + 16, right.y + 496, 172, 270);

        // -------------------------------------------------- centre bands
        const auto centre = juce::Point<int>(250, 96);
        for (size_t i = 0; i < bands.size(); ++i)
        {
            const auto bandX = centre.x + 8 + static_cast<int>(i) * 204;
            auto& band = bands[i];
            band.bounds = { bandX, centre.y, 200, 590 };
            band.enable->setLampBounds(bandX + 90, centre.y + 44, 20, 20);
            place(band.freq, { bandX, centre.y }, 100, 160, 64);
            place(band.gain, { bandX, centre.y }, 100, 348, 66);
            place(band.shape, { bandX, centre.y }, 100, 498, 56);
        }

        // --------------------------------------------------- lower strip
        const auto strip = juce::Point<int>(250, 696);
        eqInLamp->setLampBounds(strip.x + 40, strip.y + 47, 18, 18);
        slope18->setBounds(strip.x + 118, strip.y + 38, 60, 34);
        slope24->setBounds(strip.x + 182, strip.y + 38, 60, 34);
        place(lpfKnob, strip, 278, 55, 38);
        characterRow->setBounds(strip.x + 344, strip.y + 30, 232, 34);
        place(driveKnob, strip, 445, 100, 48);
        autoGainOff->setBounds(strip.x + 590, strip.y + 38, 46, 34);
        autoGainOn->setBounds(strip.x + 640, strip.y + 38, 46, 34);
        stereoKey->setBounds(strip.x + 590, strip.y + 84, 96, 34);
        oversampleOff->setBounds(strip.x + 738, strip.y + 38, 52, 34);
        oversample2x->setBounds(strip.x + 794, strip.y + 38, 46, 34);
        oversample4x->setBounds(strip.x + 844, strip.y + 38, 46, 34);

        // -------------------------------------------------------- meters
        vuLeft->setBounds(250, 818, 330, 130);
        vuRight->setBounds(955, 818, 330, 130);
    }

private:
    struct BandSpec
    {
        juce::String id, title;
        juce::Colour colour;
        juce::String type;
        std::array<juce::String, 4> frequencies;
    };

    struct Band
    {
        BandSpec spec;
        juce::Rectangle<int> bounds;
        std::unique_ptr<HeritageKnob> freq, gain, shape;
        std::unique_ptr<PanelLamp> enable;
    };

    static float vuLevel(float peak)
    {
        const auto db = juce::Decibels::gainToDecibels(juce::jmax(peak, 1.0e-5f));
        return juce::jlimit(0.0f, 1.15f, juce::jmap(db, -26.0f, 3.0f, 0.0f, 1.0f));
    }

    static float ledLevel(float peak)
    {
        const auto db = juce::Decibels::gainToDecibels(juce::jmax(peak, 1.0e-6f));
        return juce::jlimit(0.0f, 1.0f, juce::jmap(db, -60.0f, 18.0f, 0.0f, 1.0f));
    }

    void drawTopBar(juce::Graphics& g)
    {
        // official brand lockup, printed into the top bar
        brandPrint.draw(g, juce::Rectangle<float>(22.0f, 2.0f, 248.0f, 80.0f), 0.86f);

        g.setColour(juce::Colour(0xffd0c8b2));
        g.setFont(labelFont(17.0f, true, 0.96f));
        g.drawText("HERITAGE ANALOG EQ", juce::Rectangle<float>(300.0f, 30.0f, 220.0f, 24.0f),
                   juce::Justification::centredLeft, false);

    }

    void drawSidePanels(juce::Graphics& g)
    {
        auto column = [&](float originX, const juce::String& topLabel, bool lampOnRight,
                          const juce::String& midLabel, const juce::String& midUnit)
        {
            const auto originY = 96.0f;
            g.setColour(cText);
            g.setFont(labelFont(18.0f, true, 0.95f));
            g.drawText(topLabel, juce::Rectangle<float>(originX, originY + 12.0f, 206.0f, 20.0f),
                       juce::Justification::centred, false);

            const auto lamp = juce::Rectangle<float>(12.0f, 12.0f)
                                  .withCentre({ originX + (lampOnRight ? 174.0f : 32.0f), originY + 20.0f });
            g.setColour(cAmber.withAlpha(0.55f));
            g.fillEllipse(lamp.expanded(5.0f));
            g.setColour(cAmber.brighter(0.4f));
            g.fillEllipse(lamp);

            g.setColour(cMuted);
            g.setFont(labelFont(14.5f, true, 0.95f));
            g.drawText("dB TRIM", juce::Rectangle<float>(originX, originY + 164.0f, 206.0f, 18.0f),
                       juce::Justification::centred, false);
            g.setColour(cText);
            g.drawText("PHASE", juce::Rectangle<float>(originX, originY + 244.0f, 206.0f, 18.0f),
                       juce::Justification::centred, false);

            drawDivider(g, originX, originY + 276.0f);
            g.setColour(cText);
            g.setFont(labelFont(18.0f, true, 0.95f));
            g.drawText(midLabel, juce::Rectangle<float>(originX, originY + 290.0f, 206.0f, 20.0f),
                       juce::Justification::centred, false);
            g.setColour(cMuted);
            g.setFont(labelFont(14.5f, true, 0.95f));
            g.drawText(midUnit, juce::Rectangle<float>(originX, originY + 426.0f, 206.0f, 18.0f),
                       juce::Justification::centred, false);
            drawDivider(g, originX, originY + 460.0f);
        };

        column(36.0f, "INPUT", true, "HPF", "Hz");
        column(1294.0f, "OUTPUT", false, "MIX", "%");

        g.setColour(cText);
        g.setFont(labelFont(18.0f, true, 0.95f));
        g.drawText("FILTER IN", juce::Rectangle<float>(36.0f, 570.0f, 206.0f, 20.0f),
                   juce::Justification::centred, false);
        g.drawText("OUTPUT", juce::Rectangle<float>(1294.0f, 570.0f, 206.0f, 20.0f),
                   juce::Justification::centred, false);

        // grille badge on the left column
        const auto grille = juce::Rectangle<float>(48.0f, 664.0f, 182.0f, 214.0f);
        g.setColour(juce::Colours::black.withAlpha(0.55f));
        for (float y = grille.getY() + 6.0f; y < grille.getBottom(); y += 8.0f)
            for (float x = grille.getX() + 6.0f; x < grille.getRight(); x += 8.0f)
                g.fillEllipse(x, y, 3.2f, 3.2f);
        g.setColour(juce::Colour(0xff3e2a1a));
        g.drawRoundedRectangle(grille, 10.0f, 2.0f);
        {
            const auto centre = grille.getCentre();
            const auto r = 46.0f;
            juce::Path diamond;
            diamond.startNewSubPath(centre.x, centre.y - r);
            diamond.lineTo(centre.x + r, centre.y);
            diamond.lineTo(centre.x, centre.y + r);
            diamond.lineTo(centre.x - r, centre.y);
            diamond.closeSubPath();
            g.setColour(juce::Colour(0xffb8862f));
            g.strokePath(diamond, juce::PathStrokeType(6.0f, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
            juce::Path inner;
            inner.startNewSubPath(centre.x, centre.y - r * 0.56f);
            inner.lineTo(centre.x + r * 0.56f, centre.y);
            inner.lineTo(centre.x, centre.y + r * 0.56f);
            inner.lineTo(centre.x - r * 0.56f, centre.y);
            inner.closeSubPath();
            g.strokePath(inner, juce::PathStrokeType(6.0f));
            g.drawLine(centre.x, centre.y - r, centre.x, centre.y + r, 6.0f);
            g.drawLine(centre.x - r, centre.y, centre.x + r, centre.y, 6.0f);
        }
    }

    static void drawDivider(juce::Graphics& g, float originX, float y)
    {
        juce::ColourGradient line(juce::Colours::transparentWhite, originX + 18.0f, y,
                                  juce::Colours::transparentWhite, originX + 188.0f, y, false);
        line.addColour(0.5, juce::Colours::white.withAlpha(0.12f));
        g.setGradientFill(line);
        g.fillRect(originX + 18.0f, y, 170.0f, 1.0f);
    }

    void drawBandLegends(juce::Graphics& g)
    {
        for (const auto& band : bands)
        {
            const auto area = band.bounds.toFloat();
            g.setColour(band.spec.colour);
            g.setFont(labelFont(20.0f, true, 0.95f));
            g.drawText(band.spec.title, area.withHeight(22.0f).translated(0.0f, 14.0f),
                       juce::Justification::centred, false);

            g.setColour(cText);
            g.setFont(labelFont(14.5f, true, 0.95f));
            g.drawText(band.spec.type, area.withHeight(18.0f).translated(0.0f, 80.0f),
                       juce::Justification::centred, false);
            g.setFont(labelFont(15.0f, true, 0.95f));
            g.drawText("Hz", area.withHeight(18.0f).translated(0.0f, 214.0f),
                       juce::Justification::centred, false);
            g.setFont(labelFont(16.5f, true, 0.92f));
            g.drawText("GAIN", area.withHeight(20.0f).translated(0.0f, 262.0f),
                       juce::Justification::centred, false);
            g.setFont(labelFont(15.0f, true, 0.95f));
            g.drawText("dB", area.withHeight(18.0f).translated(0.0f, 404.0f),
                       juce::Justification::centred, false);
            g.setFont(labelFont(16.5f, true, 0.92f));
            g.drawText(band.spec.id == "high" ? "SLOPE" : "Q",
                       area.withHeight(20.0f).translated(0.0f, 432.0f),
                       juce::Justification::centred, false);

            drawCurveGlyphs(g, area.withHeight(20.0f).translated(0.0f, 554.0f), band.spec.id == "high");
        }
    }

    static void drawCurveGlyphs(juce::Graphics& g, juce::Rectangle<float> row, bool shelfSet)
    {
        const auto count = shelfSet ? 3 : 1;
        const auto slot = row.getWidth() / static_cast<float>(count + 1);
        for (int i = 0; i < count; ++i)
        {
            const auto cell = juce::Rectangle<float>(44.0f, 16.0f)
                                  .withCentre({ row.getX() + slot * (static_cast<float>(i) + 1.0f),
                                                row.getCentreY() });
            juce::Path path;
            if (! shelfSet)
            {
                path.startNewSubPath(cell.getX() + 2.0f, cell.getBottom() - 2.0f);
                path.lineTo(cell.getX() + 14.0f, cell.getBottom() - 2.0f);
                path.quadraticTo(cell.getX() + 22.0f, cell.getY() - 6.0f,
                                 cell.getX() + 30.0f, cell.getBottom() - 2.0f);
                path.lineTo(cell.getRight() - 2.0f, cell.getBottom() - 2.0f);
                g.setColour(juce::Colour(0xffc9a55c));
            }
            else if (i == 0)
            {
                path.startNewSubPath(cell.getX() + 2.0f, cell.getBottom() - 2.0f);
                path.lineTo(cell.getX() + 14.0f, cell.getBottom() - 2.0f);
                path.lineTo(cell.getX() + 20.0f, cell.getY() + 3.0f);
                path.lineTo(cell.getRight() - 2.0f, cell.getY() + 3.0f);
                g.setColour(juce::Colour(0xffc9a55c));
            }
            else if (i == 1)
            {
                path.startNewSubPath(cell.getX() + 2.0f, cell.getBottom() - 2.0f);
                path.lineTo(cell.getX() + 14.0f, cell.getBottom() - 2.0f);
                path.lineTo(cell.getX() + 20.0f, cell.getY() + 3.0f);
                path.lineTo(cell.getX() + 26.0f, cell.getY() + 3.0f);
                path.lineTo(cell.getRight() - 2.0f, cell.getBottom() - 2.0f);
                g.setColour(juce::Colour(0xffc9a55c));
            }
            else
            {
                path.startNewSubPath(cell.getX() + 2.0f, cell.getY() + 3.0f);
                path.lineTo(cell.getX() + 24.0f, cell.getY() + 3.0f);
                path.lineTo(cell.getX() + 30.0f, cell.getBottom() - 2.0f);
                path.lineTo(cell.getRight() - 2.0f, cell.getBottom() - 2.0f);
                g.setColour(juce::Colour(0xffe04a2a));
            }
            g.strokePath(path, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));
        }
    }

    void drawStripLegends(juce::Graphics& g)
    {
        const auto originX = 250.0f, originY = 696.0f;
        auto caption = [&](float x, float y, const juce::String& text)
        {
            g.setColour(cText);
            g.setFont(labelFont(15.5f, true, 0.94f));
            g.drawText(text, juce::Rectangle<float>(originX + x, originY + y, 200.0f, 18.0f),
                       juce::Justification::centredLeft, false);
        };
        auto divider = [&](float x)
        {
            g.setColour(juce::Colours::black.withAlpha(0.6f));
            g.fillRect(originX + x, originY + 8.0f, 1.0f, 115.0f);
            g.setColour(juce::Colours::white.withAlpha(0.05f));
            g.fillRect(originX + x + 1.0f, originY + 8.0f, 1.0f, 115.0f);
        };

        caption(30.0f, 10.0f, "EQ IN");
        divider(100.0f);
        caption(130.0f, 10.0f, "HPF / LPF");
        divider(334.0f);
        caption(384.0f, 6.0f, "ANALOG CHARACTER");
        caption(370.0f, 100.0f, "DRIVE");
        divider(568.0f);
        caption(610.0f, 10.0f, "AUTO GAIN");
        divider(712.0f);
        caption(778.0f, 10.0f, "OVERSAMPLE");
        divider(914.0f);

        g.setColour(juce::Colour(0xffd8b46a));
        g.setFont(labelFont(21.0f, false, 1.0f));
        g.drawText("Amanorsac", juce::Rectangle<float>(originX + 918.0f, originY + 38.0f, 112.0f, 26.0f),
                   juce::Justification::centred, false);
        g.setColour(juce::Colour(0xffb59858));
        g.setFont(labelFont(9.0f, true, 1.0f));
        g.drawText("STUDIO", juce::Rectangle<float>(originX + 918.0f, originY + 62.0f, 112.0f, 14.0f),
                   juce::Justification::centred, false);
    }

    void drawNameplate(juce::Graphics& g)
    {
        const auto plate = juce::Rectangle<float>(592.0f, 850.0f, 350.0f, 98.0f);
        g.setColour(juce::Colour(0xff141310));
        g.fillRoundedRectangle(plate, 6.0f);
        g.setColour(juce::Colour(0xff3a342c));
        g.drawRoundedRectangle(plate, 6.0f, 1.0f);

        brandPrint.draw(g, plate.withHeight(66.0f).translated(0.0f, 4.0f).reduced(14.0f, 0.0f), 0.80f);
        g.setColour(juce::Colour(0xffcfc4a8));
        g.setFont(labelFont(15.0f, true, 1.0f));
        g.drawText("CLASS A EQUALIZER", plate.withHeight(20.0f).translated(0.0f, 72.0f),
                   juce::Justification::centred, false);
    }

    juce::Image backdrop;

    std::unique_ptr<HeritageKnob> inputTrim, outputTrim, hpfKnob, mixKnob, lpfKnob, driveKnob;
    std::array<Band, 5> bands;
    std::unique_ptr<PanelLamp> eqInLamp, filterLamp, filterIndicator;
    std::unique_ptr<PanelKey> phaseLeft, phaseRight, slope18, slope24;
    std::unique_ptr<PanelKey> autoGainOff, autoGainOn, stereoKey;
    std::unique_ptr<PanelKey> oversampleOff, oversample2x, oversample4x;
    std::unique_ptr<RadioRow> characterRow;
    std::unique_ptr<HeritageVu> vuLeft, vuRight;
    std::unique_ptr<HeritageLedMeter> ledMeter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Surface)
};

// ----------------------------------------------------------------- editor

std::unique_ptr<juce::Component> HeritageEditor::createSurface(ProductHost host)
{
    return std::make_unique<Surface>(std::move(host));
}

juce::Rectangle<float> HeritageEditor::surfaceSize() { return { stageWidth, stageHeight }; }

HeritageEditor::HeritageEditor(juce::AudioProcessor& owner, ProductHost host)
    : AudioProcessorEditor(owner), surface(std::make_unique<Surface>(std::move(host)))
{
    addAndMakeVisible(*surface);
    // One activation page for the whole bundle: the first plug-in the customer
    // opens asks for the key, and every other one is already unlocked.
    gate = std::make_unique<ActivationView>(owner.getName(), [this] { if (gate != nullptr) gate->setVisible(false); });
    addAndMakeVisible(*gate);
    gate->setVisible(! licensing::LicenseClient::getInstance().isLicensed());
    setResizable(true, true);
    setResizeLimits(1024, static_cast<int>(1024.0f * stageHeight / stageWidth),
                    1920, static_cast<int>(1920.0f * stageHeight / stageWidth));
    getConstrainer()->setFixedAspectRatio(static_cast<double>(stageWidth / stageHeight));
    setSize(1280, static_cast<int>(1280.0f * stageHeight / stageWidth));
}

HeritageEditor::~HeritageEditor() = default;

void HeritageEditor::paint(juce::Graphics& g) { g.fillAll(juce::Colour(0xff0a0908)); }

void HeritageEditor::resized()
{
    if (gate != nullptr) gate->setBounds(getLocalBounds());
    const auto scale = juce::jmin(static_cast<float>(getWidth()) / stageWidth,
                                  static_cast<float>(getHeight()) / stageHeight);
    surface->setTransform(juce::AffineTransform::scale(scale));
    surface->setBounds(0, 0, static_cast<int>(stageWidth), static_cast<int>(stageHeight));
}
}
