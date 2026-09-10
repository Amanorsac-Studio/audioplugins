#include "RackEditor.h"

#include "common/ui/AnalogChassis.h"
#include "common/ui/AnalogPageEditor.h"
#include "common/ui/HeritageEditor.h"

#include <array>
#include <cmath>

namespace amanorsac
{
namespace
{
using namespace analog;
using namespace hw;

constexpr float stageWidth = 1536.0f;
constexpr float stageHeight = 1024.0f;

const juce::Colour cRackText { 0xffd9d3c2 };
const juce::Colour cRackMuted { 0xff9a9483 };
const juce::Colour cRackDark { 0xff2a2825 };
const juce::Colour cRackTrim { 0xffc9382c };
const juce::Colour cRackGold { 0xffd3b26a };

/** Each module keeps the accent it wears as a plugin, so a rack slot is
    recognisable at a glance. */
juce::Colour moduleAccent(const juce::String& id)
{
    if (id == "A01") return juce::Colour(0xfff0a030);
    if (id == "A02") return juce::Colour(0xffe2571f);
    if (id == "A03") return juce::Colour(0xffe0a12f);
    if (id == "A04") return juce::Colour(0xffd97b2a);
    if (id == "A05") return juce::Colour(0xffe8944a);
    if (id == "A06") return juce::Colour(0xff3fbf5a);
    if (id == "A07") return juce::Colour(0xff46b6d8);
    if (id == "A08") return juce::Colour(0xff8f6cf0);
    if (id == "A09") return juce::Colour(0xffd3b26a);
    return juce::Colour(0xffc558d8);
}

float vuFromPeak(float peak)
{
    const auto db = juce::Decibels::gainToDecibels(juce::jmax(peak, 1.0e-5f));
    return juce::jlimit(0.0f, 1.15f, juce::jmap(db, -26.0f, 3.0f, 0.0f, 1.0f));
}

float ledFromPeak(float peak)
{
    const auto db = juce::Decibels::gainToDecibels(juce::jmax(peak, 1.0e-6f));
    return juce::jlimit(0.0f, 1.0f, juce::jmap(db, -60.0f, 18.0f, 0.0f, 1.0f));
}

juce::String shortLabel(const juce::String& raw)
{
    auto text = raw.trim();
    if (text.startsWithIgnoreCase("iron_")) text = text.substring(5);
    if (text.equalsIgnoreCase("medium")) text = "MED";
    if (text.equalsIgnoreCase("standard")) text = "STD";
    return text.replaceCharacter('_', ' ').toUpperCase();
}

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
    else if (std::abs(value) >= 10.0f || value == 0.0f) text = juce::String(juce::roundToInt(value));
    else text = juce::String(value, 1).trimCharactersAtEnd("0").trimCharactersAtEnd(".");
    if (signedScale && value > 0.0f) text = "+" + text;
    return text;
}

float niceValue(float v)
{
    if (v == 0.0f) return 0.0f;
    const auto magnitude = std::pow(10.0f, std::floor(std::log10(std::abs(v))));
    const auto n = std::abs(v) / magnitude;
    const float rounded = n < 1.25f ? 1.0f : n < 1.75f ? 1.5f : n < 2.5f ? 2.0f : n < 4.0f ? 3.0f
                        : n < 6.0f ? 5.0f : n < 8.5f ? 7.0f : 10.0f;
    return (v < 0.0f ? -1.0f : 1.0f) * rounded * magnitude;
}
}

/** The selected module's controls and printed labels. Laid out at its natural
    size and then scaled as a whole to fit the rack's panel bay, so a module
    with 21 controls and one with 9 both sit correctly on the same chassis. */
class ModulePanel final : public juce::Component
{
public:
    struct Label
    {
        juce::Rectangle<float> area;
        juce::String text;
        float size = 15.0f;
        bool bold = true;
        juce::Colour colour { cRackText };
        juce::Justification justification { juce::Justification::centred };
        bool display = false;
    };

    void clearContent() { labels.clear(); rules.clear(); removeAllChildren(); }
    void addLabel(Label label) { labels.push_back(std::move(label)); }
    void addRule(juce::Line<float> line) { rules.push_back(line); }

    void paint(juce::Graphics& g) override
    {
        for (const auto& label : labels)
        {
            g.setColour(label.colour);
            g.setFont(label.display ? displayFont(label.size) : labelFont(label.size, label.bold, 1.0f));
            g.drawText(label.text, label.area, label.justification, false);
        }
        for (const auto& rule : rules)
        {
            g.setColour(juce::Colours::black.withAlpha(0.55f));
            g.drawLine(rule, 1.0f);
            g.setColour(juce::Colours::white.withAlpha(0.05f));
            g.drawLine({ rule.getStart().translated(0.0f, 1.0f), rule.getEnd().translated(0.0f, 1.0f) }, 1.0f);
        }
    }

private:
    std::vector<Label> labels;
    std::vector<juce::Line<float>> rules;
};

// ---------------------------------------------------------------- Surface

/** The rack's own controls live in the same tree as the modules', so its host
    simply passes ids through. */
inline ProductHost rackHost(RackProcessor& rack)
{
    ProductHost host;
    host.spec = &rack.spec;
    host.state = &rack.state;
    host.undoManager = &rack.undoManager;
    host.presetManager = rack.presetManager.get();
    host.inputPeak = [&rack](int channel) { return rack.getInputPeak(channel); };
    host.outputPeak = [&rack](int channel) { return rack.getOutputPeak(channel); };
    host.gainReduction = [&rack] { return rack.getGainReductionDb(); };
    host.sampleRate = [&rack] { return rack.getSampleRate(); };
    host.latency = [&rack] { return rack.getLatencySamples(); };
    return host;
}

class RackEditor::Surface final : public AnalogChassis
{
public:
    explicit Surface(RackProcessor& owner) : AnalogChassis(rackHost(owner)), processor(owner)
    {
        rebuild();
        setSize(static_cast<int>(stageWidth), static_cast<int>(stageHeight));
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff0a0908));
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff262322), 0.0f, 0.0f,
                                               juce::Colour(0xff141211), 0.0f, stageHeight, false));
        g.fillRect(juce::Rectangle<float>(0.0f, 0.0f, stageWidth, stageHeight));

        drawTopBar(g);
        drawBays(g);

        for (const auto& text : texts)
        {
            g.setColour(text.colour);
            g.setFont(text.display ? displayFont(text.size) : labelFont(text.size, text.bold, 1.0f));
            g.drawText(text.text, text.area, text.justification, false);
        }
        for (const auto& line : dividers)
        {
            g.setColour(juce::Colours::black.withAlpha(0.55f));
            g.drawLine(line, 1.0f);
            g.setColour(juce::Colours::white.withAlpha(0.05f));
            g.drawLine({ line.getStart().translated(0.0f, 1.0f), line.getEnd().translated(0.0f, 1.0f) }, 1.0f);
        }
        drawChain(g);
    }

    void resized() override { layoutTopBar(560, 20, 44, static_cast<int>(stageWidth) - 60); }

private:
    struct Text
    {
        juce::Rectangle<float> area;
        juce::String text;
        float size = 16.0f;
        bool bold = true;
        juce::Colour colour { cRackText };
        juce::Justification justification { juce::Justification::centred };
        bool display = false;
    };

    struct Card
    {
        juce::Rectangle<float> bounds;
        int slot = 0;
        juce::String name;
        juce::Colour accent;
        RackProcessor::Lane lane = RackProcessor::Lane::a;
    };

    static constexpr float chainLeft = 268.0f;
    static constexpr float chainWidth = 996.0f;
    static constexpr float chainTop = 118.0f;
    static constexpr float cardHeight = 84.0f;
    static constexpr float footerTop = 824.0f;

    /** The chain grows a second rail when the rack is split; the module panel
        takes the rest of the bay either way. */
    float chainBottom() const
    {
        return chainTop + cardHeight + (processor.isSplit() ? cardHeight + 22.0f : 0.0f) + 8.0f;
    }
    float panelTop() const { return chainBottom() + 24.0f; }
    float panelHeight() const { return footerTop - 24.0f - panelTop(); }

    // ------------------------------------------------------------ painting

    void drawTopBar(juce::Graphics& g)
    {
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff151312), 0.0f, 0.0f,
                                               juce::Colour(0xff0f0e0d), 0.0f, 84.0f, false));
        g.fillRect(juce::Rectangle<float>(0.0f, 0.0f, stageWidth, 84.0f));
        g.setColour(juce::Colour(0xff0a0908));
        g.drawLine(0.0f, 83.0f, stageWidth, 83.0f, 2.0f);

        brandPrint.draw(g, { 16.0f, 12.0f, 190.0f, 60.0f }, 0.86f);
        g.setColour(juce::Colour(0xffd0c8b2));
        g.setFont(labelFont(21.0f, true, 1.0f));
        g.drawText("ANALOG MIX RACK", juce::Rectangle<float>(228.0f, 18.0f, 320.0f, 26.0f),
                   juce::Justification::centredLeft, false);
        g.setColour(cRackGold);
        g.setFont(labelFont(12.5f, true, 1.0f));
        g.drawText("MODULAR ANALOG PROCESSOR RACK",
                   juce::Rectangle<float>(228.0f, 46.0f, 340.0f, 18.0f), juce::Justification::centredLeft, false);
    }

    void drawBays(juce::Graphics& g)
    {
        auto bay = [&](juce::Rectangle<float> area)
        {
            g.setColour(juce::Colour(0xff1c1a18));
            g.fillRoundedRectangle(area, 9.0f);
            g.setColour(juce::Colour(0xff0b0a09));
            g.drawRoundedRectangle(area.reduced(0.5f), 9.0f, 1.2f);
            g.setColour(juce::Colours::white.withAlpha(0.04f));
            g.drawLine(area.getX() + 9.0f, area.getY() + 1.0f, area.getRight() - 9.0f, area.getY() + 1.0f, 1.0f);
        };
        bay({ 20.0f, 96.0f, 232.0f, 900.0f });
        bay({ 1284.0f, 96.0f, 232.0f, 900.0f });
        bay({ chainLeft, panelTop() - 12.0f, chainWidth, panelHeight() + 24.0f });
        bay({ chainLeft, footerTop, chainWidth, 172.0f });
    }

    void drawChain(juce::Graphics& g)
    {
        const auto split = processor.isSplit();

        // lane rails
        for (int lane = 0; lane < (split ? 2 : 1); ++lane)
        {
            const auto y = chainTop + static_cast<float>(lane) * (cardHeight + 22.0f);
            const auto rail = juce::Rectangle<float>(chainLeft, y - 6.0f, chainWidth, cardHeight + 12.0f);
            g.setColour(juce::Colour(0xff161413));
            g.fillRoundedRectangle(rail, 8.0f);
            g.setColour(juce::Colour(0xff0b0a09));
            g.drawRoundedRectangle(rail.reduced(0.5f), 8.0f, 1.0f);
            if (split)
            {
                g.setColour(cRackGold.withAlpha(0.8f));
                g.setFont(labelFont(13.0f, true, 1.0f));
                g.drawText(lane == 0 ? "A" : "B",
                           juce::Rectangle<float>(rail.getX() - 26.0f, rail.getCentreY() - 10.0f, 22.0f, 20.0f),
                           juce::Justification::centredRight, false);
            }
        }

        for (const auto& card : cards)
        {
            if (dragging && card.slot == dragSlot) continue;
            drawCard(g, card, card.bounds);
        }

        // where a dragged module would land
        if (dragging && dropIndex >= 0)
        {
            const auto y = chainTop + static_cast<float>(dropLane) * (cardHeight + 22.0f);
            g.setColour(cRackGold);
            g.fillRoundedRectangle(dropX - 2.0f, y - 2.0f, 4.0f, cardHeight + 4.0f, 2.0f);
        }
        if (dragging)
        {
            const auto slot = dragSlot;
            for (const auto& card : cards)
                if (card.slot == slot)
                    drawCard(g, card, card.bounds.withPosition(dragPosition), true);
        }
    }

    void drawCard(juce::Graphics& g, const Card& card, juce::Rectangle<float> bounds, bool floating = false)
    {
        const auto on = processor.isActive(card.slot);
        const auto selected = card.slot == selection;

        if (floating)
        {
            g.setColour(juce::Colours::black.withAlpha(0.45f));
            g.fillRoundedRectangle(bounds.translated(3.0f, 5.0f), 7.0f);
        }
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff26231f), 0.0f, bounds.getY(),
                                               juce::Colour(0xff141312), 0.0f, bounds.getBottom(), false));
        g.fillRoundedRectangle(bounds, 7.0f);
        if (on)
        {
            g.setColour(card.accent.withAlpha(0.12f));
            g.fillRoundedRectangle(bounds, 7.0f);
        }
        g.setColour(selected ? card.accent : juce::Colour(0xff0b0a09));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 7.0f, selected ? 2.0f : 1.0f);
        g.setColour(juce::Colours::white.withAlpha(0.05f));
        g.drawLine(bounds.getX() + 7.0f, bounds.getY() + 1.0f, bounds.getRight() - 7.0f, bounds.getY() + 1.0f, 1.0f);

        // grip, so it reads as something you can pick up
        g.setColour(cRackMuted.withAlpha(0.32f));
        for (int i = 0; i < 3; ++i)
            g.fillRect(bounds.getX() + 7.0f, bounds.getY() + 10.0f + static_cast<float>(i) * 4.0f, 8.0f, 1.5f);

        g.setColour(on ? juce::Colour(0xffe6dcc4) : cRackMuted.withAlpha(0.7f));
        g.setFont(labelFont(bounds.getWidth() > 120.0f ? 13.5f : 11.5f, true, 1.0f));
        g.drawFittedText(card.name, bounds.withTrimmedTop(24.0f).withTrimmedBottom(24.0f)
                                          .withTrimmedLeft(6.0f).withTrimmedRight(6.0f).toNearestInt(),
                         juce::Justification::centred, 2, 0.85f);

        const auto meter = juce::Rectangle<float>(bounds.getX() + 8.0f, bounds.getBottom() - 13.0f,
                                                  bounds.getWidth() - 16.0f, 5.0f);
        g.setColour(juce::Colour(0xff0d0c0b));
        g.fillRoundedRectangle(meter, 2.5f);
        const auto level = juce::jlimit(0.0f, 1.0f, ledFromPeak(processor.getSlotPeak(card.slot)));
        if (level > 0.001f)
        {
            g.setColour(card.accent.withAlpha(0.9f));
            g.fillRoundedRectangle(meter.withWidth(meter.getWidth() * level), 2.5f);
        }
    }

    // ------------------------------------------------------------ interaction

    const Card* cardAt(juce::Point<float> position) const
    {
        for (const auto& card : cards)
            if (card.bounds.contains(position)) return &card;
        return nullptr;
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        if (const auto* card = cardAt(event.position))
        {
            dragSlot = card->slot;
            dragGrab = event.position - card->bounds.getPosition();
            dragOrigin = card->bounds.getPosition();
            dragging = false;
            if (card->slot != selection) { selection = card->slot; rebuild(); }
            repaint();
        }
        else dragSlot = -1;
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (dragSlot < 0) return;
        if (! dragging && event.getDistanceFromDragStart() < 5) return;
        dragging = true;
        dragPosition = event.position - dragGrab;
        updateDropTarget(event.position);
        repaint();
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        if (dragging && dragSlot >= 0 && dropIndex >= 0)
        {
            const auto slot = dragSlot;
            const auto lane = dropLane == 1 ? RackProcessor::Lane::b : RackProcessor::Lane::a;
            const auto index = dropIndex;
            dragging = false;
            dragSlot = -1;
            dropIndex = -1;
            processor.moveSlot(slot, lane, index);
            rebuild();
        }
        dragging = false;
        dragSlot = -1;
        dropIndex = -1;
        repaint();
    }

    /** Which lane, and which gap in it, the pointer is over. */
    void updateDropTarget(juce::Point<float> position)
    {
        const auto split = processor.isSplit();
        dropLane = 0;
        if (split && position.y > chainTop + cardHeight + 11.0f) dropLane = 1;

        std::vector<const Card*> lane;
        for (const auto& card : cards)
            if (static_cast<int>(card.lane) == dropLane && card.slot != dragSlot) lane.push_back(&card);

        dropIndex = static_cast<int>(lane.size());
        dropX = chainLeft + 6.0f;
        for (size_t i = 0; i < lane.size(); ++i)
            if (position.x < lane[i]->bounds.getCentreX())
            {
                dropIndex = static_cast<int>(i);
                dropX = lane[i]->bounds.getX() - 5.0f;
                return;
            }
        if (! lane.empty()) dropX = lane.back()->bounds.getRight() + 5.0f;
    }

    /** Rebuilds when the arrangement changed behind the editor's back, which is
        what a preset load, an undo or host automation does. */
    juce::String arrangementSignature() const
    {
        juce::String signature(processor.isSplit() ? "S" : "-");
        for (int slot = 0; slot < RackProcessor::moduleCount; ++slot)
            signature << static_cast<int>(processor.laneOf(slot)) << ":" << processor.positionOf(slot) << ",";
        return signature;
    }

    void refreshExtras() override
    {
        if (! dragging)
        {
            const auto signature = arrangementSignature();
            if (signature != shownArrangement) { shownArrangement = signature; rebuild(); }
        }
        repaint();
    }

    // ------------------------------------------------------------ building

    void caption(float x, float y, float w, const juce::String& text, float size = 15.0f,
                 juce::Colour colour = cRackText, bool bold = true)
    {
        texts.push_back({ { x, y, w, size + 8.0f }, text, size, bold, colour });
    }

    std::vector<HeritageKnob::Legend> autoLegends(const ParameterDescriptor& descriptor, bool compact)
    {
        const auto* parameter = processor.state.getParameter(descriptor.id);
        const auto signedScale = descriptor.minimum < 0.0f;
        auto fromT = [&](float t)
        {
            return parameter != nullptr ? parameter->convertFrom0to1(t)
                                        : juce::jmap(t, descriptor.minimum, descriptor.maximum);
        };
        auto toT = [&](float v)
        {
            return parameter != nullptr ? parameter->convertTo0to1(v)
                                        : juce::jmap(v, descriptor.minimum, descriptor.maximum, 0.0f, 1.0f);
        };

        std::vector<HeritageKnob::Legend> legends;
        juce::StringArray used;
        const auto marks = compact ? 3 : 5;
        for (int i = 0; i < marks; ++i)
        {
            auto t = static_cast<float>(i) / static_cast<float>(marks - 1);
            auto value = fromT(t);
            if (i > 0 && i < marks - 1)
            {
                value = signedScale && i == marks / 2 ? 0.0f : niceValue(value);
                t = juce::jlimit(0.05f, 0.95f, toT(value));
            }
            if (i == marks / 2 && ! signedScale) continue;
            const auto text = legendText(value, descriptor.unit, signedScale);
            if (used.contains(text)) continue;
            used.add(text);
            legends.push_back({ text, -135.0f + 270.0f * t, 6.0f });
        }
        return legends;
    }

    void placeKnob(const ParameterDescriptor& descriptor, float cx, float cy, float size,
                   juce::Colour cap, juce::Colour arc, const juce::String& title, bool toPanel = true)
    {
        std::unique_ptr<HeritageKnob> made;
        addKnob(made, descriptor.id, size, cap, arc, size >= 60.0f ? 11 : 9, size >= 60.0f ? 2 : 1,
                autoLegends(descriptor, size < 52.0f), size >= 60.0f ? 15.0f : 14.0f);
        if (made == nullptr) return;
        const auto box = static_cast<int>(size) + (size >= 70.0f ? 96 : 76);
        made->setBounds(static_cast<int>(cx) - box / 2, static_cast<int>(cy) - box / 2, box, box);
        if (toPanel) panel.addAndMakeVisible(*made);
        knobs.push_back(std::move(made));

        auto label = [&](float x, float y, float w, const juce::String& text, float fontSize, juce::Colour colour)
        {
            if (toPanel) panel.addLabel({ { x, y, w, fontSize + 8.0f }, text, fontSize, true, colour });
            else caption(x, y, w, text, fontSize, colour);
        };
        label(cx - 110.0f, cy - size * 0.5f - 56.0f, 220.0f, title, size >= 60.0f ? 17.0f : 15.0f, cRackText);
        if (descriptor.unit.isNotEmpty())
            label(cx - 60.0f, cy + size * 0.5f + 24.0f, 120.0f, descriptor.unit, 12.5f, cRackMuted);
    }

    void rebuild()
    {
        attachments.clear();
        keys.clear();
        lamps.clear();
        rows.clear();
        knobs.clear();
        keyStore.clear();
        lampStore.clear();
        meters.clear();
        vus.clear();
        cards.clear();
        texts.clear();
        dividers.clear();
        removeAllChildren();
        faceplate.reset();
        panel.clearContent();
        buildTopBar();

        buildChrome();
        buildChain();
        buildModulePanel();
        juce::ignoreUnused(&Surface::buildGenericPanel);
        shownArrangement = arrangementSignature();
        resized();
    }

    void buildChrome()
    {
        // ---- left bay: rack input trim and input meter
        if (const auto* input = find(processor.spec, "input"))
        {
            std::unique_ptr<HeritageKnob> made;
            addKnob(made, input->id, 70.0f, cRackTrim, {}, 17, 2,
                    evenly({ "-24", "-18", "-12", "-6", "0", "+6", "+12", "+18", "+24" }, 9.0f, true), 15.0f);
            if (made != nullptr) { made->setBounds(53, 128, 166, 166); knobs.push_back(std::move(made)); }
        }
        caption(26.0f, 106.0f, 220.0f, "INPUT", 17.0f);
        caption(76.0f, 296.0f, 120.0f, "dB TRIM", 13.0f, cRackMuted);

        auto inputMeter = std::make_unique<HeritageLedMeter>(
            [this] { return ledFromPeak(processor.getInputPeak(0)); },
            [this] { return ledFromPeak(processor.getInputPeak(1)); });
        inputMeter->setAccent(cRackGold);
        inputMeter->setBounds(46, 352, 180, 620);
        addAndMakeVisible(*inputMeter);
        meters.push_back(std::move(inputMeter));

        // ---- right bay: output trim, global mix, output meter
        if (const auto* output = find(processor.spec, "output"))
        {
            std::unique_ptr<HeritageKnob> made;
            addKnob(made, output->id, 70.0f, cRackTrim, {}, 17, 2,
                    evenly({ "-24", "-18", "-12", "-6", "0", "+6", "+12", "+18", "+24" }, 9.0f, true), 15.0f);
            if (made != nullptr) { made->setBounds(1317, 128, 166, 166); knobs.push_back(std::move(made)); }
        }
        caption(1290.0f, 106.0f, 220.0f, "OUTPUT", 17.0f);
        caption(1340.0f, 296.0f, 120.0f, "dB TRIM", 13.0f, cRackMuted);

        if (const auto* mix = find(processor.spec, "mix"))
        {
            auto legends = evenly({ "0", "25", "50", "75", "100" }, 4.0f);
            legends.push_back({ "DRY", -152.0f, 14.0f });
            legends.push_back({ "WET", 152.0f, 14.0f });
            std::unique_ptr<HeritageKnob> made;
            addKnob(made, mix->id, 56.0f, cRackDark, {}, 9, 2, legends, 14.0f);
            if (made != nullptr) { made->setBounds(1334, 352, 132, 132); knobs.push_back(std::move(made)); }
            caption(1290.0f, 332.0f, 220.0f, "MIX", 16.0f);
        }

        auto outputMeter = std::make_unique<HeritageLedMeter>(
            [this] { return ledFromPeak(processor.getOutputPeak(0)); },
            [this] { return ledFromPeak(processor.getOutputPeak(1)); });
        outputMeter->setAccent(cRackGold);
        outputMeter->setBounds(1310, 512, 180, 460);
        addAndMakeVisible(*outputMeter);
        meters.push_back(std::move(outputMeter));

        buildFooter();
    }

    /** Routing controls: the split, what each lane contributes, and the rack
        bypass, with the chain's gain reduction beside them. */
    void buildFooter()
    {
        const auto split = processor.isSplit();

        {
            std::unique_ptr<PanelKey> key;
            addKey(key, "split", "SPLIT", cRackGold);
            key->setBounds(300, 856, 128, 42);
            keyStore.push_back(std::move(key));
        }
        caption(290.0f, 830.0f, 148.0f, "ROUTING", 13.0f, cRackMuted);
        caption(290.0f, 906.0f, 148.0f, split ? "TWO LANES" : "ONE CHAIN", 12.5f,
                split ? cRackGold : cRackMuted);

        auto laneKnob = [&](const char* id, float cx, const juce::String& title)
        {
            const auto* descriptor = find(processor.spec, id);
            if (descriptor == nullptr) return;
            std::unique_ptr<HeritageKnob> made;
            addKnob(made, descriptor->id, 44.0f, cRackDark, split ? cRackGold : juce::Colour(0xff3a3631),
                    9, 1, evenly({ "-24", "-12", "0", "+12", "+24" }, 4.0f, true), 12.0f);
            if (made == nullptr) return;
            made->setBounds(static_cast<int>(cx) - 60, 862, 120, 120);
            made->setEnabled(split);
            knobs.push_back(std::move(made));
            caption(cx - 80.0f, 836.0f, 160.0f, title, 13.0f, split ? cRackText : cRackMuted);
        };
        laneKnob("lane_a_level", 512.0f, "LANE A");
        laneKnob("lane_b_level", 640.0f, "LANE B");

        {
            std::unique_ptr<PanelKey> key;
            addKey(key, "lane_b_phase", juce::String::fromUTF8("\xC3\x98") + " B", cRackGold);
            key->setBounds(700, 862, 84, 38);
            key->setEnabled(split);
            keyStore.push_back(std::move(key));
        }

        auto reduction = std::make_unique<HeritageVu>(
            [this] { return juce::jlimit(0.0f, 1.0f, -processor.getGainReductionDb() / 20.0f); });
        reduction->setGainReductionScale(true);
        reduction->setBounds(830, 842, 300, 124);
        addAndMakeVisible(*reduction);
        vus.push_back(std::move(reduction));

        if (find(processor.spec, "bypass") != nullptr)
        {
            std::unique_ptr<PanelKey> key;
            addKey(key, "bypass", "BYPASS", juce::Colour(0xfff0a030));
            key->setBounds(1146, 882, 110, 44);
            keyStore.push_back(std::move(key));
        }
    }

    /** The chain itself: one rail, or two when the rack is split. */
    void buildChain()
    {
        const auto split = processor.isSplit();
        caption(chainLeft, 90.0f, 300.0f, "SIGNAL CHAIN", 12.5f, cRackMuted);
        texts.back().justification = juce::Justification::centredLeft;
        caption(chainLeft + chainWidth - 320.0f, 90.0f, 320.0f, "DRAG A MODULE TO REORDER", 12.5f, cRackMuted);
        texts.back().justification = juce::Justification::centredRight;

        for (int lane = 0; lane < (split ? 2 : 1); ++lane)
        {
            const auto order = processor.laneOrder(lane == 1 ? RackProcessor::Lane::b : RackProcessor::Lane::a);
            if (order.empty()) continue;

            const auto count = static_cast<float>(order.size());
            constexpr float gap = 10.0f;
            const auto width = juce::jmin(178.0f, (chainWidth - 16.0f - gap * (count - 1.0f)) / count);
            const auto total = width * count + gap * (count - 1.0f);
            const auto startX = chainLeft + (chainWidth - total) * 0.5f;
            const auto y = chainTop + static_cast<float>(lane) * (cardHeight + 22.0f);

            for (size_t i = 0; i < order.size(); ++i)
            {
                const auto slot = order[i];
                Card card;
                card.slot = slot;
                card.name = processor.moduleSpec(slot).displayName;
                card.accent = moduleAccent(processor.moduleSpec(slot).id);
                card.lane = lane == 1 ? RackProcessor::Lane::b : RackProcessor::Lane::a;
                card.bounds = { startX + (width + gap) * static_cast<float>(i), y, width, cardHeight };

                std::unique_ptr<PanelLamp> lamp;
                addLamp(lamp, "slot." + processor.moduleSpec(slot).id + ".enabled", card.accent, true);
                if (lamp != nullptr)
                {
                    lamp->setLampBounds(static_cast<int>(card.bounds.getRight()) - 26,
                                        static_cast<int>(card.bounds.getY()) + 9, 15, 15);
                    lampStore.push_back(std::move(lamp));
                }

                std::unique_ptr<PanelKey> solo;
                addKey(solo, "slot." + processor.moduleSpec(slot).id + ".solo", "S", juce::Colour(0xffe8b04a));
                if (solo != nullptr)
                {
                    solo->setBounds(static_cast<int>(card.bounds.getCentreX()) - 13,
                                    static_cast<int>(card.bounds.getBottom()) - 32, 26, 17);
                    keyStore.push_back(std::move(solo));
                }
                cards.push_back(std::move(card));
            }
        }
    }

    /** Puts the selected module's real faceplate in the bay: the same editor
        the plugin opens, reading the rack's copy of that module's parameters.
        Nothing here is a second layout of the product. */
    void buildModulePanel()
    {
        const auto& module = processor.moduleSpec(selection);
        const auto prefix = RackProcessor::modulePrefix(selection);

        ProductHost moduleHost;
        moduleHost.spec = &module;
        moduleHost.state = &processor.state;
        moduleHost.undoManager = &processor.undoManager;
        moduleHost.presetManager = processor.presetManager.get();
        moduleHost.qualify = [prefix](const juce::String& id) { return prefix + id; };
        moduleHost.inputPeak = [this](int channel) { return processor.getInputPeak(channel); };
        moduleHost.outputPeak = [this, slot = selection](int channel)
        {
            juce::ignoreUnused(channel);
            return processor.getSlotPeak(slot);
        };
        moduleHost.gainReduction = [this] { return processor.getGainReductionDb(); };
        moduleHost.sampleRate = [this] { return processor.getSampleRate(); };
        moduleHost.latency = [this] { return processor.getLatencySamples(); };
        moduleHost.ownsTopBar = false;   // the rack already has one

        const auto stage = module.id == "A01" ? HeritageEditor::surfaceSize()
                                              : AnalogPageEditor::surfaceSize(module.id);
        faceplate = module.id == "A01" ? HeritageEditor::createSurface(std::move(moduleHost))
                                       : AnalogPageEditor::createSurface(std::move(moduleHost));
        if (faceplate == nullptr) return;

        addAndMakeVisible(*faceplate);
        faceplate->setBounds(0, 0, static_cast<int>(stage.getWidth()), static_cast<int>(stage.getHeight()));
        const auto scale = juce::jmin(chainWidth / stage.getWidth(), panelHeight() / stage.getHeight());
        faceplate->setTransform(juce::AffineTransform::scale(scale)
                                    .translated(chainLeft + (chainWidth - stage.getWidth() * scale) * 0.5f,
                                                panelTop() + (panelHeight() - stage.getHeight() * scale) * 0.5f));
    }

    /** Kept for reference: the generic contract layout the rack used before the
        real faceplates were embedded. */
    void buildGenericPanel()
    {
        const auto& module = processor.moduleSpec(selection);
        const auto prefix = RackProcessor::modulePrefix(selection);
        const auto accent = moduleAccent(module.id);

        std::vector<const ParameterDescriptor*> continuous, switches, choices;
        for (const auto& descriptor : module.parameters)
        {
            const auto* full = find(processor.spec, prefix + descriptor.id);
            if (full == nullptr) continue;
            if (descriptor.kind == ParameterDescriptor::Kind::boolean) switches.push_back(full);
            else if (descriptor.kind == ParameterDescriptor::Kind::choice) choices.push_back(full);
            else continuous.push_back(full);
        }

        const auto count = static_cast<int>(continuous.size());
        const auto columns = count <= 6 ? juce::jmax(1, count) : count <= 12 ? 6 : 7;
        const auto knobRows = (count + columns - 1) / juce::jmax(1, columns);
        constexpr float rowHeight = 158.0f, cellWidth = 148.0f;
        const auto contentWidth = juce::jmax(chainWidth, cellWidth * static_cast<float>(columns));
        const auto centreX = contentWidth * 0.5f;

        panel.addLabel({ { 0.0f, 4.0f, contentWidth, 36.0f }, module.displayName.toUpperCase(), 27.0f, true,
                         juce::Colour(0xffe6dcc4), juce::Justification::centred, true });
        panel.addLabel({ { 0.0f, 46.0f, contentWidth, 20.0f }, module.primaryRole.toUpperCase(), 13.5f, true,
                         accent });
        panel.addRule({ 0.0f, 78.0f, contentWidth, 78.0f });

        auto y = 152.0f;
        for (int i = 0; i < count; ++i)
        {
            const auto row = i / columns;
            const auto column = i % columns;
            const auto inRow = juce::jmin(columns, count - row * columns);
            const auto startX = centreX - cellWidth * static_cast<float>(inRow) * 0.5f;
            const auto cx = startX + cellWidth * (static_cast<float>(column) + 0.5f);
            placeKnob(*continuous[static_cast<size_t>(i)], cx, y + static_cast<float>(row) * rowHeight,
                      54.0f, cRackDark, accent, controlName(module, continuous[static_cast<size_t>(i)]->name));
        }

        auto optionY = y + static_cast<float>(juce::jmax(0, knobRows)) * rowHeight - 52.0f;
        if (! switches.empty())
        {
            panel.addRule({ 40.0f, optionY - 20.0f, contentWidth - 40.0f, optionY - 20.0f });
            constexpr float width = 116.0f, gap = 10.0f;
            const auto perRow = juce::jmax(1, static_cast<int>((contentWidth - 60.0f) / (width + gap)));
            for (size_t i = 0; i < switches.size(); ++i)
            {
                const auto row = static_cast<int>(i) / perRow;
                const auto column = static_cast<int>(i) % perRow;
                const auto inRow = juce::jmin(perRow, static_cast<int>(switches.size()) - row * perRow);
                const auto total = (width + gap) * static_cast<float>(inRow) - gap;
                const auto x = centreX - total * 0.5f + (width + gap) * static_cast<float>(column);
                std::unique_ptr<PanelKey> key;
                addKey(key, switches[i]->id, controlName(module, switches[i]->name), accent);
                panel.addAndMakeVisible(*key);
                key->setBounds(static_cast<int>(x), static_cast<int>(optionY + static_cast<float>(row) * 48.0f),
                               static_cast<int>(width), 38);
                keyStore.push_back(std::move(key));
            }
            optionY += 48.0f * static_cast<float>((switches.size() + static_cast<size_t>(perRow) - 1)
                                                  / static_cast<size_t>(perRow)) + 14.0f;
        }

        for (const auto* descriptor : choices)
        {
            const auto choiceCount = juce::jmax(1, descriptor->choices.size());
            const auto width = juce::jmin(104.0f, 620.0f / static_cast<float>(choiceCount));
            constexpr float gap = 8.0f;
            const auto total = (width + gap) * static_cast<float>(choiceCount) - gap;
            auto x = centreX - total * 0.5f + 110.0f;
            panel.addLabel({ { 40.0f, optionY + 8.0f, 300.0f, 22.0f },
                             controlName(module, descriptor->name), 14.0f, true, cRackText,
                             juce::Justification::centredLeft });
            for (int i = 0; i < choiceCount; ++i)
            {
                std::unique_ptr<PanelKey> key;
                addChoiceKey(key, descriptor->id, i, shortLabel(descriptor->choices[i]));
                panel.addAndMakeVisible(*key);
                key->setBounds(static_cast<int>(x), static_cast<int>(optionY), static_cast<int>(width), 34);
                keyStore.push_back(std::move(key));
                x += width + gap;
            }
            optionY += 44.0f;
        }

        const auto available = panelHeight();
        const auto contentHeight = juce::jmax(available * 0.5f, optionY + 16.0f);
        panel.setBounds(0, 0, static_cast<int>(contentWidth), static_cast<int>(contentHeight));
        const auto scale = juce::jmin(1.0f, chainWidth / contentWidth, available / contentHeight);
        panel.setTransform(juce::AffineTransform::scale(scale)
                               .translated(chainLeft + (chainWidth - contentWidth * scale) * 0.5f,
                                           panelTop() + (available - contentHeight * scale) * 0.5f));
    }

    /** The rack prefixes every parameter name with its product; the panel
        already says which module this is. */
    static juce::String controlName(const PluginSpec& module, const juce::String& name)
    {
        auto text = name;
        if (text.startsWith(module.displayName)) text = text.substring(module.displayName.length()).trim();
        return text.toUpperCase();
    }

    /** As the chassis does: the mark at twelve o clock is kept only when it is
        the true centre of a bipolar scale. */
    static std::vector<HeritageKnob::Legend> evenly(const juce::StringArray& items, float radiusOffset,
                                                    bool keepCentre = false)
    {
        std::vector<HeritageKnob::Legend> legends;
        const auto centre = items.size() / 2;
        for (int i = 0; i < items.size(); ++i)
        {
            if (! keepCentre && items.size() % 2 == 1 && i == centre) continue;
            legends.push_back({ items[i],
                                -135.0f + 270.0f * static_cast<float>(i)
                                    / static_cast<float>(juce::jmax(1, items.size() - 1)),
                                radiusOffset });
        }
        return legends;
    }

    int selection = 0;
    juce::String shownArrangement;

    int dragSlot = -1;
    bool dragging = false;
    juce::Point<float> dragGrab, dragOrigin, dragPosition;
    int dropLane = 0, dropIndex = -1;
    float dropX = 0.0f;

    std::vector<Card> cards;
    std::vector<Text> texts;
    std::vector<juce::Line<float>> dividers;
    std::vector<std::unique_ptr<HeritageKnob>> knobs;
    std::vector<std::unique_ptr<PanelKey>> keyStore;
    std::vector<std::unique_ptr<PanelLamp>> lampStore;
    std::vector<std::unique_ptr<HeritageLedMeter>> meters;
    std::vector<std::unique_ptr<HeritageVu>> vus;
    ModulePanel panel;
    std::unique_ptr<juce::Component> faceplate;
    RackProcessor& processor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Surface)
};

// ----------------------------------------------------------------- editor

RackEditor::RackEditor(RackProcessor& owner)
    : AudioProcessorEditor(owner), surface(std::make_unique<Surface>(owner))
{
    addAndMakeVisible(*surface);
    setResizable(true, true);
    setResizeLimits(1024, static_cast<int>(1024.0f * stageHeight / stageWidth),
                    1920, static_cast<int>(1920.0f * stageHeight / stageWidth));
    getConstrainer()->setFixedAspectRatio(static_cast<double>(stageWidth / stageHeight));
    setSize(1280, static_cast<int>(1280.0f * stageHeight / stageWidth));
}

RackEditor::~RackEditor() = default;

void RackEditor::paint(juce::Graphics& g) { g.fillAll(juce::Colour(0xff0a0908)); }

void RackEditor::resized()
{
    const auto scale = juce::jmin(static_cast<float>(getWidth()) / stageWidth,
                                  static_cast<float>(getHeight()) / stageHeight);
    surface->setTransform(juce::AffineTransform::scale(scale));
    surface->setBounds(0, 0, static_cast<int>(stageWidth), static_cast<int>(stageHeight));
}
}
