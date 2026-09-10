#include "PrismEditor.h"
#include "Theme.h"

#include <array>
#include <cmath>
#include <functional>

namespace amanorsac
{
namespace
{
constexpr auto panel = 0xff07111b;
constexpr auto panelRaised = 0xff0a1724;
constexpr auto hairline = 0xff18354a;
constexpr auto cyan = 0xff45dff5;

const ParameterDescriptor* findDescriptor(const PluginSpec& spec, const juce::String& id)
{
    for (const auto& parameter : spec.parameters)
        if (parameter.id == id)
            return &parameter;
    return nullptr;
}

juce::String bandId(int oneBasedBand, const juce::String& suffix)
{
    return "band." + juce::String(oneBasedBand).paddedLeft('0', 2) + "." + suffix;
}

class PrismControl final : public juce::Component
{
public:
    PrismControl(juce::AudioProcessorValueTreeState& state,
                 const ParameterDescriptor& parameter,
                 bool compactControl = false)
        : descriptor(parameter), compact(compactControl)
    {
        auto displayName = descriptor.name;
        if (displayName.startsWithIgnoreCase("Band ") && displayName.length() > 8)
            displayName = displayName.substring(8);
        title.setText(displayName.toUpperCase(), juce::dontSendNotification);
        title.setJustificationType(juce::Justification::centred);
        title.setFont(juce::FontOptions(compact ? 9.0f : 10.0f, juce::Font::bold));
        title.setColour(juce::Label::textColourId, juce::Colour(0xff89a2b7));
        addAndMakeVisible(title);

        if (descriptor.kind == ParameterDescriptor::Kind::boolean)
        {
            toggle.setButtonText(descriptor.name);
            toggle.setColour(juce::ToggleButton::textColourId, theme::primaryText);
            toggle.setColour(juce::ToggleButton::tickColourId, juce::Colour(cyan));
            addAndMakeVisible(toggle);
            buttonAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
                state, descriptor.id, toggle);
        }
        else if (descriptor.kind == ParameterDescriptor::Kind::choice)
        {
            choice.addItemList(descriptor.choices, 1);
            choice.setJustificationType(juce::Justification::centred);
            choice.setColour(juce::ComboBox::backgroundColourId, juce::Colour(panelRaised));
            choice.setColour(juce::ComboBox::outlineColourId, juce::Colour(hairline));
            choice.setColour(juce::ComboBox::textColourId, theme::primaryText);
            choice.setColour(juce::ComboBox::arrowColourId, juce::Colour(cyan));
            addAndMakeVisible(choice);
            choiceAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
                state, descriptor.id, choice);
        }
        else
        {
            slider.setSliderStyle(compact ? juce::Slider::LinearHorizontal
                                          : juce::Slider::RotaryHorizontalVerticalDrag);
            slider.setTextBoxStyle(compact ? juce::Slider::TextBoxRight : juce::Slider::TextBoxBelow,
                                   false, compact ? 58 : 82, 18);
            slider.setDoubleClickReturnValue(true, descriptor.defaultValue);
            slider.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(cyan));
            slider.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff17394f));
            slider.setColour(juce::Slider::trackColourId, juce::Colour(cyan));
            slider.setColour(juce::Slider::thumbColourId, juce::Colour(0xffe9fbff));
            slider.setColour(juce::Slider::textBoxTextColourId, theme::primaryText);
            slider.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
            slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            slider.setTextValueSuffix(descriptor.unit.isEmpty() ? juce::String() : " " + descriptor.unit);
            addAndMakeVisible(slider);
            sliderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                state, descriptor.id, slider);
        }
        setTitle(descriptor.name);
        setDescription(descriptor.rangeText + "; double-click restores " + descriptor.defaultText);
    }

    void paint(juce::Graphics& graphics) override
    {
        graphics.setColour(juce::Colour(hairline).withAlpha(0.72f));
        graphics.drawVerticalLine(getWidth() - 1, 8.0f, static_cast<float>(getHeight() - 8));
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(compact ? 7 : 9, 5);
        title.setBounds(area.removeFromTop(compact ? 15 : 19));
        if (toggle.isVisible())
        {
            toggle.setBounds(area.withSizeKeepingCentre(juce::jmin(110, area.getWidth()), 28));
            title.setVisible(false);
        }
        if (choice.isVisible()) choice.setBounds(area.reduced(3, compact ? 5 : 20));
        if (slider.isVisible()) slider.setBounds(area);
    }

private:
    const ParameterDescriptor descriptor;
    const bool compact;
    juce::Label title;
    juce::Slider slider;
    juce::ToggleButton toggle;
    juce::ComboBox choice;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> choiceAttachment;
};

class PrismGraph final : public juce::Component, private juce::Timer
{
public:
    explicit PrismGraph(juce::AudioProcessorValueTreeState& valueTree) : state(valueTree)
    {
        for (int band = 0; band < 6; ++band)
        {
            bands[static_cast<size_t>(band)].frequency = state.getParameter(bandId(band + 1, "frequency"));
            bands[static_cast<size_t>(band)].gain = state.getParameter(bandId(band + 1, "gain"));
            bands[static_cast<size_t>(band)].q = state.getParameter(bandId(band + 1, "q"));
            bands[static_cast<size_t>(band)].enabled = state.getParameter(bandId(band + 1, "enabled"));
        }
        setMouseCursor(juce::MouseCursor::CrosshairCursor);
        startTimerHz(30);
    }

    std::function<void(int)> onBandSelected;

    void paint(juce::Graphics& graphics) override
    {
        auto bounds = getLocalBounds().toFloat();
        juce::ColourGradient background(juce::Colour(0xff071723), bounds.getX(), bounds.getY(),
                                        juce::Colour(0xff02070d), bounds.getRight(), bounds.getBottom(), false);
        graphics.setGradientFill(background);
        graphics.fillRoundedRectangle(bounds, 8.0f);

        const std::array<float, 10> frequencies { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
        graphics.setFont(juce::FontOptions(9.0f));
        for (const auto frequency : frequencies)
        {
            const auto x = frequencyToX(frequency);
            graphics.setColour(juce::Colour(hairline).withAlpha(frequency == 1000 ? 0.68f : 0.42f));
            graphics.drawVerticalLine(static_cast<int>(x), 18.0f, bounds.getBottom() - 19.0f);
            graphics.setColour(juce::Colour(0xff66859a));
            const auto text = frequency >= 1000 ? juce::String(frequency / 1000, frequency == 1000 ? 0 : 0) + "k"
                                                : juce::String(static_cast<int>(frequency));
            graphics.drawText(text, static_cast<int>(x) - 20, getHeight() - 18, 40, 12,
                              juce::Justification::centred);
        }
        for (int db = -24; db <= 24; db += 6)
        {
            const auto y = gainToY(static_cast<float>(db));
            graphics.setColour(juce::Colour(hairline).withAlpha(db == 0 ? 0.8f : 0.34f));
            graphics.drawHorizontalLine(static_cast<int>(y), 34.0f, bounds.getRight() - 34.0f);
            graphics.setColour(juce::Colour(0xff66859a));
            graphics.drawText((db > 0 ? "+" : "") + juce::String(db), 6, static_cast<int>(y) - 6, 25, 12,
                              juce::Justification::centredRight);
        }

        drawSpectrum(graphics);
        drawEqCurve(graphics);
        drawNodes(graphics);

        graphics.setColour(juce::Colour(hairline));
        graphics.drawRoundedRectangle(bounds.reduced(0.5f), 8.0f, 1.0f);
        graphics.setColour(juce::Colour(cyan).withAlpha(0.8f));
        graphics.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        graphics.drawText("LIVE SPECTRUM  /  DRAG NODES TO SHAPE", 42, 8, 250, 18,
                          juce::Justification::centredLeft);
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        activeBand = nearestBand(event.position);
        if (activeBand >= 0)
        {
            selectedBand = activeBand;
            bands[static_cast<size_t>(activeBand)].frequency->beginChangeGesture();
            bands[static_cast<size_t>(activeBand)].gain->beginChangeGesture();
            if (onBandSelected) onBandSelected(selectedBand + 1);
            repaint();
        }
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (activeBand < 0) return;
        auto& band = bands[static_cast<size_t>(activeBand)];
        const auto xNorm = juce::jlimit(0.0f, 1.0f, (event.position.x - 34.0f) / juce::jmax(1.0f, getWidth() - 68.0f));
        const auto yNorm = juce::jlimit(0.0f, 1.0f, (event.position.y - 26.0f) / juce::jmax(1.0f, getHeight() - 52.0f));
        const auto frequency = 20.0f * std::pow(2000.0f, xNorm);
        const auto gain = 30.0f - yNorm * 60.0f;
        band.frequency->setValueNotifyingHost(band.frequency->convertTo0to1(frequency));
        band.gain->setValueNotifyingHost(band.gain->convertTo0to1(gain));
        repaint();
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        if (activeBand < 0) return;
        bands[static_cast<size_t>(activeBand)].frequency->endChangeGesture();
        bands[static_cast<size_t>(activeBand)].gain->endChangeGesture();
        activeBand = -1;
    }

    void mouseDoubleClick(const juce::MouseEvent& event) override
    {
        const auto index = nearestBand(event.position);
        if (index < 0) return;
        auto* gain = bands[static_cast<size_t>(index)].gain;
        gain->beginChangeGesture();
        gain->setValueNotifyingHost(gain->convertTo0to1(0.0f));
        gain->endChangeGesture();
        repaint();
    }

private:
    struct BandParameters
    {
        juce::RangedAudioParameter* frequency = nullptr;
        juce::RangedAudioParameter* gain = nullptr;
        juce::RangedAudioParameter* q = nullptr;
        juce::RangedAudioParameter* enabled = nullptr;
    };

    juce::AudioProcessorValueTreeState& state;
    std::array<BandParameters, 6> bands;
    int selectedBand = 0;
    int activeBand = -1;

    static float actual(const juce::RangedAudioParameter* parameter)
    {
        return parameter == nullptr ? 0.0f : parameter->convertFrom0to1(parameter->getValue());
    }

    float frequencyToX(float frequency) const
    {
        const auto normal = std::log(juce::jlimit(20.0f, 40000.0f, frequency) / 20.0f) / std::log(2000.0f);
        return 34.0f + normal * (getWidth() - 68.0f);
    }

    float gainToY(float gain) const
    {
        return 26.0f + (0.5f - juce::jlimit(-30.0f, 30.0f, gain) / 60.0f) * (getHeight() - 52.0f);
    }

    juce::Point<float> nodePosition(int index) const
    {
        const auto& band = bands[static_cast<size_t>(index)];
        return { frequencyToX(actual(band.frequency)), gainToY(actual(band.gain)) };
    }

    int nearestBand(juce::Point<float> position) const
    {
        int result = -1;
        auto distance = 30.0f;
        for (int index = 0; index < 6; ++index)
        {
            const auto next = position.getDistanceFrom(nodePosition(index));
            if (next < distance) { distance = next; result = index; }
        }
        return result;
    }

    void drawSpectrum(juce::Graphics& graphics)
    {
        const auto phase = static_cast<float>(juce::Time::getMillisecondCounterHiRes() * 0.0012);
        for (int layer = 0; layer < 4; ++layer)
        {
            juce::Path path;
            path.startNewSubPath(34.0f, static_cast<float>(getHeight() - 26));
            for (int x = 34; x < getWidth() - 34; x += 3)
            {
                const auto n = static_cast<float>(x - 34) / juce::jmax(1.0f, getWidth() - 68.0f);
                const auto envelope = 22.0f + 118.0f * std::sin(juce::MathConstants<float>::pi * n);
                const auto texture = 0.34f + 0.22f * std::sin(n * 63.0f + phase + layer)
                                           + 0.12f * std::sin(n * 149.0f - phase * 0.7f + layer);
                const auto y = getHeight() - 27.0f - envelope * texture - layer * 5.0f;
                path.lineTo(static_cast<float>(x), y);
            }
            path.lineTo(static_cast<float>(getWidth() - 34), static_cast<float>(getHeight() - 26));
            path.closeSubPath();
            juce::ColourGradient fill(juce::Colour(0xff29e1c2).withAlpha(0.03f + layer * 0.018f),
                                      0.0f, static_cast<float>(getHeight()),
                                      juce::Colour(0xff4678ff).withAlpha(0.10f),
                                      static_cast<float>(getWidth()), getHeight() * 0.45f, false);
            graphics.setGradientFill(fill);
            graphics.fillPath(path);
            graphics.setColour(juce::Colour(0xff44ddca).withAlpha(0.10f + layer * 0.025f));
            graphics.strokePath(path, juce::PathStrokeType(0.7f));
        }
    }

    void drawEqCurve(juce::Graphics& graphics)
    {
        juce::Path curve;
        for (int x = 34; x < getWidth() - 34; ++x)
        {
            const auto xNorm = static_cast<float>(x - 34) / juce::jmax(1.0f, getWidth() - 68.0f);
            auto combinedGain = 0.0f;
            for (const auto& band : bands)
            {
                if (actual(band.enabled) < 0.5f) continue;
                const auto centre = std::log(juce::jmax(20.0f, actual(band.frequency)) / 20.0f) / std::log(2000.0f);
                const auto q = juce::jmax(0.05f, actual(band.q));
                const auto width = 0.115f / std::sqrt(q);
                const auto d = (xNorm - centre) / juce::jmax(0.012f, width);
                combinedGain += actual(band.gain) * std::exp(-0.5f * d * d);
            }
            const auto y = gainToY(juce::jlimit(-30.0f, 30.0f, combinedGain));
            if (x == 34) curve.startNewSubPath(static_cast<float>(x), y);
            else curve.lineTo(static_cast<float>(x), y);
        }
        graphics.setColour(juce::Colour(cyan).withAlpha(0.18f));
        graphics.strokePath(curve, juce::PathStrokeType(7.0f, juce::PathStrokeType::curved));
        graphics.setColour(juce::Colour(0xffbaf8ff));
        graphics.strokePath(curve, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved));
    }

    void drawNodes(juce::Graphics& graphics)
    {
        const std::array<juce::Colour, 6> colours {
            juce::Colour(0xff42e7d1), juce::Colour(0xff75ee64), juce::Colour(0xffffcd3c),
            juce::Colour(0xff4cd8ff), juce::Colour(0xffbf60ff), juce::Colour(0xffff5e91)
        };
        for (int index = 0; index < 6; ++index)
        {
            const auto point = nodePosition(index);
            const auto colour = colours[static_cast<size_t>(index)];
            const auto enabled = actual(bands[static_cast<size_t>(index)].enabled) >= 0.5f;
            graphics.setColour(colour.withAlpha(enabled ? 0.13f : 0.035f));
            graphics.fillEllipse(point.x - 24.0f, point.y - 24.0f, 48.0f, 48.0f);
            graphics.setColour(colour.withAlpha(enabled ? 0.8f : 0.25f));
            graphics.drawEllipse(point.x - 15.0f, point.y - 15.0f, 30.0f, 30.0f,
                                 selectedBand == index ? 2.4f : 1.0f);
            graphics.setColour(enabled ? juce::Colour(0xfff4fdff) : juce::Colour(0xff52636e));
            graphics.fillEllipse(point.x - 6.0f, point.y - 6.0f, 12.0f, 12.0f);
            graphics.setColour(juce::Colour(0xff021017));
            graphics.setFont(juce::FontOptions(8.0f, juce::Font::bold));
            graphics.drawText(juce::String(index + 1), static_cast<int>(point.x) - 7,
                              static_cast<int>(point.y) - 6, 14, 12, juce::Justification::centred);
        }
    }

    void timerCallback() override { repaint(); }
};
}

class PrismEditor::Surface final : public juce::Component
{
public:
    explicit Surface(PluginProcessor& owner) : processor(owner), graph(owner.state)
    {
        addAndMakeVisible(graph);
        graph.onBandSelected = [this](int band) { selectBand(band); };

        for (const auto& id : { "input_gain", "auto_gain", "analyzer", "processing_quality", "oversampling", "output_gain" })
            if (const auto* descriptor = findDescriptor(processor.spec, id))
            {
                auto control = std::make_unique<PrismControl>(processor.state, *descriptor, true);
                addAndMakeVisible(*control);
                globalControls.push_back(std::move(control));
            }

        bandTitle.setFont(juce::FontOptions(18.0f, juce::Font::bold));
        bandTitle.setColour(juce::Label::textColourId, theme::primaryText);
        bandTitle.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(bandTitle);

        bandHint.setText("Selected band", juce::dontSendNotification);
        bandHint.setFont(juce::FontOptions(9.0f));
        bandHint.setColour(juce::Label::textColourId, juce::Colour(0xff6d8799));
        bandHint.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(bandHint);
        selectBand(1);
    }

    void paint(juce::Graphics& graphics) override
    {
        graphics.fillAll(juce::Colour(0xff02070c));
        auto top = getLocalBounds().removeFromTop(76).toFloat();
        juce::ColourGradient header(juce::Colour(0xff06121d), 0.0f, 0.0f,
                                    juce::Colour(0xff02070c), static_cast<float>(getWidth()), 0.0f, false);
        graphics.setGradientFill(header);
        graphics.fillRect(top);
        graphics.setColour(juce::Colour(hairline));
        graphics.drawHorizontalLine(75, 0.0f, static_cast<float>(getWidth()));

        graphics.setColour(juce::Colour(cyan));
        for (int index = 0; index < 5; ++index)
        {
            const auto height = 12.0f + index * 5.0f;
            graphics.fillRoundedRectangle(26.0f + index * 7.0f, 38.0f - height * 0.5f, 4.0f, height, 2.0f);
        }
        graphics.setColour(theme::primaryText);
        graphics.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        graphics.drawText("AMANORSAC", 72, 22, 130, 18, juce::Justification::centredLeft);
        graphics.setColour(juce::Colour(0xff7590a3));
        graphics.setFont(juce::FontOptions(8.0f, juce::Font::bold));
        graphics.drawText("STUDIO", 73, 40, 80, 12, juce::Justification::centredLeft);

        graphics.setColour(theme::primaryText);
        graphics.setFont(juce::FontOptions(27.0f));
        graphics.drawText("P R I S M   E Q", 0, 17, getWidth(), 31, juce::Justification::centred);
        graphics.setColour(juce::Colour(cyan).withAlpha(0.76f));
        graphics.setFont(juce::FontOptions(8.0f, juce::Font::bold));
        graphics.drawText("ADAPTIVE DIGITAL EQUALIZER", 0, 49, getWidth(), 12, juce::Justification::centred);

        const auto controlsTop = 84;
        graphics.setColour(juce::Colour(panel));
        graphics.fillRoundedRectangle(20.0f, static_cast<float>(controlsTop), getWidth() - 40.0f, 64.0f, 7.0f);
        graphics.setColour(juce::Colour(hairline));
        graphics.drawRoundedRectangle(20.5f, static_cast<float>(controlsTop) + 0.5f,
                                      getWidth() - 41.0f, 63.0f, 7.0f, 1.0f);

        auto dock = juce::Rectangle<float>(20.0f, static_cast<float>(getHeight() - 222),
                                           getWidth() - 40.0f, 194.0f);
        juce::ColourGradient dockFill(juce::Colour(panelRaised), dock.getX(), dock.getY(),
                                      juce::Colour(panel), dock.getRight(), dock.getBottom(), false);
        graphics.setGradientFill(dockFill);
        graphics.fillRoundedRectangle(dock, 8.0f);
        graphics.setColour(juce::Colour(hairline));
        graphics.drawRoundedRectangle(dock.reduced(0.5f), 8.0f, 1.0f);
        graphics.setColour(juce::Colour(cyan).withAlpha(0.65f));
        graphics.fillRoundedRectangle(20.0f, dock.getY(), 4.0f, dock.getHeight(), 2.0f);

        graphics.setColour(juce::Colour(0xff577183));
        graphics.setFont(juce::FontOptions(9.0f));
        graphics.drawText("Drag a node  /  double-click resets gain  /  Shift-drag any knob for fine control",
                          24, getHeight() - 24, getWidth() - 48, 14, juce::Justification::centredRight);
    }

    void resized() override
    {
        const auto globalX = 28;
        const auto globalY = 89;
        const auto globalW = (getWidth() - 56) / juce::jmax(1, static_cast<int>(globalControls.size()));
        for (size_t index = 0; index < globalControls.size(); ++index)
            globalControls[index]->setBounds(globalX + static_cast<int>(index) * globalW, globalY, globalW, 54);

        graph.setBounds(20, 158, getWidth() - 40, getHeight() - 394);

        const auto dockY = getHeight() - 222;
        bandHint.setBounds(39, dockY + 29, 128, 16);
        bandTitle.setBounds(38, dockY + 43, 128, 32);
        auto controlsArea = juce::Rectangle<int>(174, dockY + 10, getWidth() - 202, 174);
        const auto controlWidth = controlsArea.getWidth() / juce::jmax(1, static_cast<int>(bandControls.size()));
        for (size_t index = 0; index < bandControls.size(); ++index)
            bandControls[index]->setBounds(controlsArea.getX() + static_cast<int>(index) * controlWidth,
                                           controlsArea.getY(), controlWidth, controlsArea.getHeight());
    }

private:
    PluginProcessor& processor;
    PrismGraph graph;
    std::vector<std::unique_ptr<PrismControl>> globalControls;
    std::vector<std::unique_ptr<PrismControl>> bandControls;
    juce::Label bandTitle;
    juce::Label bandHint;

    void selectBand(int oneBasedBand)
    {
        bandTitle.setText("BAND " + juce::String(oneBasedBand).paddedLeft('0', 2), juce::dontSendNotification);
        for (auto& control : bandControls) removeChildComponent(control.get());
        bandControls.clear();
        for (const auto& suffix : { "type", "frequency", "gain", "q", "dynamic_mode", "dynamic_range", "threshold", "stereo_mode" })
            if (const auto* descriptor = findDescriptor(processor.spec, bandId(oneBasedBand, suffix)))
            {
                auto control = std::make_unique<PrismControl>(processor.state, *descriptor);
                addAndMakeVisible(*control);
                bandControls.push_back(std::move(control));
            }
        resized();
        repaint();
    }
};

PrismEditor::PrismEditor(PluginProcessor& owner) : AudioProcessorEditor(owner)
{
    surface = std::make_unique<Surface>(owner);
    addAndMakeVisible(*surface);
    setResizable(true, true);
    setResizeLimits(980, 640, 1800, 1125);
    setSize(1280, 800);
}

PrismEditor::~PrismEditor() = default;

void PrismEditor::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colour(0xff02070c));
}

void PrismEditor::resized()
{
    surface->setBounds(getLocalBounds());
}
}
