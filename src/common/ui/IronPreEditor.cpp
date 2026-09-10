#include "IronPreEditor.h"

#include "AnalogUI.h"

namespace amanorsac
{
namespace
{
using namespace analog;

const ParameterDescriptor* find(const PluginSpec& spec, const juce::String& id)
{
    for (const auto& descriptor : spec.parameters)
        if (descriptor.id == id)
            return &descriptor;
    return nullptr;
}

/** Peak gain to VU deflection, where 1.0 sits on the 0 VU mark. */
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
}

// ---------------------------------------------------------------- Surface

class IronPreEditor::Surface final : public juce::Component
{
public:
    explicit Surface(PluginProcessor& owner)
        : processor(owner),
          palette(Palette::forProduct(owner.spec.id)),
          look(palette),
          topBar(palette, "Drums - Punchy & Wide"),
          footer(palette, { { "W", "Mouse wheel", "Adjusts the hovered control" },
                            { "S", "Shift + drag", "Fine adjustment" },
                            { "R", "Double-click", "Resets to default" },
                            { "N", "Click a value", "Type an exact number" },
                            { "A", "Automation", "Every control is host automatable" } })
    {
        topBar.setProductTitle(processor.spec.displayName, processor.spec.primaryRole);
        addAndMakeVisible(topBar);
        addAndMakeVisible(footer);

        const auto& spec = processor.spec;

        if (const auto* d = find(spec, "input"))
        {
            inputTrim = std::make_unique<TrimKnob>(processor.state, *d, palette, "INPUT", "dB TRIM");
            addAndMakeVisible(*inputTrim);
        }
        if (const auto* d = find(spec, "output"))
        {
            outputTrim = std::make_unique<TrimKnob>(processor.state, *d, palette, "OUTPUT", "dB TRIM");
            addAndMakeVisible(*outputTrim);
        }
        if (const auto* d = find(spec, "phase"))
        {
            phase = std::make_unique<LampButton>(processor.state, *d, palette,
                                                 juce::String::fromUTF8("\xC3\x98"), "PHASE");
            addAndMakeVisible(*phase);
        }
        if (const auto* d = find(spec, "hpf"))
        {
            hpf = std::make_unique<KnobCell>(processor.state, *d, palette, look, false);
            addAndMakeVisible(*hpf);
        }

        auto addKnob = [this, &spec](const juce::String& id) -> KnobCell*
        {
            if (const auto* d = find(spec, id))
            {
                auto cell = std::make_unique<KnobCell>(processor.state, *d, palette, look);
                auto* raw = cell.get();
                addAndMakeVisible(*raw);
                knobs.push_back(std::move(cell));
                return raw;
            }
            return nullptr;
        };

        addKnob("impedance");
        addKnob("drive");
        addKnob("saturation");
        addKnob("low_tone");
        addKnob("high_tone");

        if (const auto* d = find(spec, "transformer"))
        {
            transformer = std::make_unique<SegmentGroup>(processor.state, *d, palette, "Transformer Character", 1);
            transformer->setOptionLabels({ "A", "B", "C" });
            addAndMakeVisible(*transformer);
        }
        if (const auto* d = find(spec, "pad"))
        {
            pad = std::make_unique<SegmentGroup>(processor.state, *d, palette, "Pad", 2);
            pad->setOptionLabels({ "0 dB", "-20 dB" });
            addAndMakeVisible(*pad);
        }

        vu = std::make_unique<VuMeter>([this]
        {
            return vuDeflection(juce::jmax(processor.getOutputPeak(0), processor.getOutputPeak(1)));
        });
        addAndMakeVisible(*vu);

        meters = std::make_unique<LedBargraph>([this] { return ledDeflection(processor.getOutputPeak(0)); },
                                               [this] { return ledDeflection(processor.getOutputPeak(1)); },
                                               palette);
        addAndMakeVisible(*meters);

        // Sizing last means the first resized() sees every child, and the
        // editor's later setBounds to the same size stays a no-op safely.
        setSize(static_cast<int>(designWidth), static_cast<int>(designHeight));
    }

    ~Surface() override = default;

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        g.fillAll(juce::Colour(0xff0b0a09));
        drawWoodFrame(g, bounds, palette);

        // three chassis plates
        for (const auto& plate : { leftPlate, centrePlate, rightPlate })
        {
            fillPanel(g, plate, palette, 10.0f);
            strokePanel(g, plate.reduced(0.6f), palette, 10.0f, 1.3f, 0.7f);
            drawPanelScrews(g, plate, 15.0f, 6.5f);
        }

        // INPUT / OUTPUT column captions sit on the plate, above their trims
        // (the trim widget prints its own caption, so nothing is drawn here).

        // HPF sub-panel
        fillPanel(g, hpfPanel, palette, 8.0f, true);
        strokePanel(g, hpfPanel.reduced(0.6f), palette, 8.0f, 1.0f, 0.5f);
        drawSectionCaption(g, hpfPanel.withHeight(30.0f).translated(0.0f, 8.0f), "HPF", palette.cream, 15.0f);
        if (hpf != nullptr)
        {
            // printed frequency legend around the small knob
            const auto knob = hpf->getBounds().toFloat();
            const auto centre = juce::Point<float>(knob.getCentreX(), knob.getY() + knob.getHeight() * 0.42f);
            const auto radius = knob.getWidth() * 0.62f;
            static const std::array<const char*, 5> marks { "OFF", "40", "80", "160", "300" };
            g.setFont(labelFont(12.0f, true, 0.92f));
            g.setColour(palette.label.withAlpha(0.9f));
            for (size_t i = 0; i < marks.size(); ++i)
            {
                const auto t = static_cast<float>(i) / static_cast<float>(marks.size() - 1);
                const auto angle = juce::MathConstants<float>::pi * (1.24f + t * 1.52f);
                const auto at = centre + juce::Point<float>(std::sin(angle), -std::cos(angle)) * radius;
                g.drawText(juce::String(marks[i]),
                           juce::Rectangle<float>(44.0f, 16.0f).withCentre(at),
                           juce::Justification::centred, false);
            }
        }

        // brand grille
        fillPanel(g, emblemPanel, palette, 8.0f, true);
        strokePanel(g, emblemPanel.reduced(0.6f), palette, 8.0f, 1.0f, 0.5f);
        {
            g.saveState();
            juce::Path clip;
            clip.addRoundedRectangle(emblemPanel.reduced(6.0f), 6.0f);
            g.reduceClipRegion(clip);
            g.setColour(juce::Colours::black.withAlpha(0.55f));
            for (float y = emblemPanel.getY() + 8.0f; y < emblemPanel.getBottom(); y += 7.0f)
                for (float x = emblemPanel.getX() + 8.0f; x < emblemPanel.getRight(); x += 7.0f)
                    g.fillEllipse(x, y, 3.4f, 3.4f);
            g.restoreState();
        }
        {
            const auto side = juce::jmin(emblemPanel.getWidth(), emblemPanel.getHeight()) * 0.74f;
            drawHexEmblem(g, juce::Rectangle<float>(side, side).withCentre(emblemPanel.getCentre()),
                          palette.accent.withAlpha(0.9f));
        }
        drawPanelScrews(g, emblemPanel, 12.0f, 5.0f);

        // recessed well behind the knob rows
        fillPanel(g, knobWell, palette, 9.0f, true);
        strokePanel(g, knobWell.reduced(0.6f), palette, 9.0f, 1.0f, 0.45f);

        // switch bank frame
        fillPanel(g, switchBank, palette, 9.0f, true);
        strokePanel(g, switchBank.reduced(0.6f), palette, 9.0f, 1.0f, 0.5f);
    }

    void resized() override
    {
        auto bounds = getLocalBounds().toFloat().reduced(16.0f);

        topBar.setBounds(bounds.removeFromTop(122.0f).toNearestInt());
        bounds.removeFromTop(10.0f);
        footer.setBounds(bounds.removeFromBottom(86.0f).toNearestInt());
        bounds.removeFromBottom(10.0f);

        leftPlate = bounds.removeFromLeft(284.0f);
        bounds.removeFromLeft(10.0f);
        rightPlate = bounds.removeFromRight(280.0f);
        bounds.removeFromLeft(10.0f);
        centrePlate = bounds;

        // ---- left column
        auto left = leftPlate.reduced(14.0f, 12.0f);
        if (inputTrim) inputTrim->setBounds(left.removeFromTop(248.0f).toNearestInt());
        left.removeFromTop(6.0f);
        if (phase) phase->setBounds(left.removeFromTop(74.0f)
                                        .withSizeKeepingCentre(132.0f, 74.0f).toNearestInt());
        left.removeFromTop(8.0f);
        hpfPanel = left.removeFromTop(210.0f);
        if (hpf)
        {
            const auto knobSide = juce::jmin(hpfPanel.getWidth() * 0.52f, hpfPanel.getHeight() * 0.72f);
            hpf->setBounds(juce::Rectangle<float>(knobSide, hpfPanel.getHeight() * 0.74f)
                               .withCentre({ hpfPanel.getCentreX(), hpfPanel.getCentreY() + 22.0f })
                               .toNearestInt());
        }
        left.removeFromTop(10.0f);
        emblemPanel = left;

        // ---- right column
        auto right = rightPlate.reduced(12.0f, 12.0f);
        if (outputTrim) outputTrim->setBounds(right.removeFromTop(248.0f).toNearestInt());
        right.removeFromTop(14.0f);
        if (meters) meters->setBounds(right.toNearestInt());

        // ---- centre column
        auto centre = centrePlate.reduced(18.0f, 16.0f);
        if (vu) vu->setBounds(centre.removeFromTop(250.0f).reduced(58.0f, 0.0f).toNearestInt());
        centre.removeFromTop(14.0f);

        switchBank = centre.removeFromRight(186.0f);
        centre.removeFromRight(12.0f);
        knobWell = centre;

        auto bank = switchBank.reduced(12.0f, 14.0f);
        if (transformer) transformer->setBounds(bank.removeFromTop(190.0f).toNearestInt());
        bank.removeFromTop(14.0f);
        if (pad) pad->setBounds(bank.removeFromTop(92.0f).toNearestInt());

        auto well = knobWell.reduced(10.0f, 12.0f);
        auto topRow = well.removeFromTop(well.getHeight() * 0.5f);
        well.removeFromTop(6.0f);
        auto bottomRow = well;

        const auto place = [](juce::Rectangle<float> row, std::vector<KnobCell*> cells)
        {
            if (cells.empty()) return;
            const auto cellWidth = row.getWidth() / static_cast<float>(cells.size());
            for (size_t i = 0; i < cells.size(); ++i)
                cells[i]->setBounds(row.withX(row.getX() + cellWidth * static_cast<float>(i))
                                       .withWidth(cellWidth).reduced(6.0f, 0.0f).toNearestInt());
        };

        std::vector<KnobCell*> top, bottom;
        for (size_t i = 0; i < knobs.size(); ++i)
            (i < 3 ? top : bottom).push_back(knobs[i].get());

        place(topRow, top);
        // the shorter second row keeps the same cell width as the first
        if (! bottom.empty())
        {
            const auto cellWidth = topRow.getWidth() / 3.0f;
            const auto used = cellWidth * static_cast<float>(bottom.size());
            place(bottomRow.withX(bottomRow.getCentreX() - used * 0.5f).withWidth(used), bottom);
        }
    }

private:
    PluginProcessor& processor;
    Palette palette;
    AnalogLookAndFeel look;

    TopBar topBar;
    FooterStrip footer;

    std::unique_ptr<TrimKnob> inputTrim, outputTrim;
    std::unique_ptr<LampButton> phase;
    std::unique_ptr<KnobCell> hpf;
    std::vector<std::unique_ptr<KnobCell>> knobs;
    std::unique_ptr<SegmentGroup> transformer, pad;
    std::unique_ptr<VuMeter> vu;
    std::unique_ptr<LedBargraph> meters;

    juce::Rectangle<float> leftPlate, centrePlate, rightPlate;
    juce::Rectangle<float> hpfPanel, emblemPanel, knobWell, switchBank;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Surface)
};

// ----------------------------------------------------------------- editor

IronPreEditor::IronPreEditor(PluginProcessor& owner)
    : AudioProcessorEditor(owner), surface(std::make_unique<Surface>(owner))
{
    addAndMakeVisible(*surface);
    setResizable(true, true);
    getConstrainer()->setFixedAspectRatio(static_cast<double>(analog::designWidth)
                                          / static_cast<double>(analog::designHeight));
    setResizeLimits(960, 600, 1920, 1200);
    setSize(1280, 800);
}

IronPreEditor::~IronPreEditor() = default;

void IronPreEditor::paint(juce::Graphics& g) { g.fillAll(juce::Colour(0xff080807)); }

void IronPreEditor::resized()
{
    const auto scale = juce::jmin(static_cast<float>(getWidth()) / analog::designWidth,
                                  static_cast<float>(getHeight()) / analog::designHeight);
    surface->setTransform(juce::AffineTransform::scale(scale));
    surface->setBounds(0, 0, static_cast<int>(analog::designWidth), static_cast<int>(analog::designHeight));
}
}
