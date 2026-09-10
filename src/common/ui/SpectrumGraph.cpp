#include "SpectrumGraph.h"
#include "Theme.h"

namespace amanorsac
{
SpectrumGraph::SpectrumGraph(juce::AudioProcessorValueTreeState& parameterState, juce::String bandPrefix)
    : state(parameterState),
      prefix(std::move(bandPrefix)),
      verticalParameter(state.getParameter(prefix + ".gain") != nullptr ? prefix + ".gain" : prefix + ".range")
{
    setWantsKeyboardFocus(true);
    setTitle("Equalizer graph");
    setDescription("Drag frequency and gain; wheel adjusts Q; Shift enables fine adjustment");
}

juce::String SpectrumGraph::bandId(const juce::String& suffix) const
{
    return prefix + "." + suffix;
}

float SpectrumGraph::parameter(const juce::String& id, float fallback) const
{
    if (const auto* value = state.getRawParameterValue(id))
        return value->load(std::memory_order_relaxed);
    return fallback;
}

void SpectrumGraph::setParameter(const juce::String& id, float value)
{
    if (auto* target = state.getParameter(id))
        target->setValueNotifyingHost(target->convertTo0to1(value));
}

void SpectrumGraph::resetParameter(const juce::String& id)
{
    if (auto* target = state.getParameter(id))
        target->setValueNotifyingHost(target->getDefaultValue());
}

juce::Point<float> SpectrumGraph::nodePosition() const
{
    const auto bounds = getLocalBounds().toFloat().reduced(24.0f);
    const auto* frequency = state.getParameter(bandId("frequency"));
    const auto* vertical = state.getParameter(verticalParameter);
    const auto xNormal = frequency != nullptr ? frequency->getValue() : 0.5f;
    const auto yNormal = vertical != nullptr ? vertical->getValue() : 0.5f;
    const auto x = bounds.getX() + xNormal * bounds.getWidth();
    const auto y = bounds.getBottom() - yNormal * bounds.getHeight();
    return { x, y };
}

void SpectrumGraph::setFromPosition(juce::Point<float> point)
{
    const auto bounds = getLocalBounds().toFloat().reduced(24.0f);
    const auto x = juce::jlimit(0.0f, 1.0f, (point.x - bounds.getX()) / bounds.getWidth());
    const auto y = juce::jlimit(0.0f, 1.0f, (point.y - bounds.getY()) / bounds.getHeight());
    if (auto* frequency = state.getParameter(bandId("frequency")))
        frequency->setValueNotifyingHost(x);
    if (auto* vertical = state.getParameter(verticalParameter))
        vertical->setValueNotifyingHost(1.0f - y);
    repaint();
}

void SpectrumGraph::paint(juce::Graphics& graphics)
{
    auto bounds = getLocalBounds().toFloat();
    juce::ColourGradient background(theme::digitalPanel.brighter(0.08f), bounds.getCentreX(), bounds.getY(),
                                    theme::digitalBackground, bounds.getCentreX(), bounds.getBottom(), false);
    graphics.setGradientFill(background);
    graphics.fillRoundedRectangle(bounds, 10.0f);
    graphics.setColour(theme::digitalBorder.withAlpha(0.55f));
    for (int index = 1; index < 10; ++index)
    {
        const auto x = bounds.getX() + bounds.getWidth() * index / 10.0f;
        graphics.drawVerticalLine(static_cast<int>(x), bounds.getY(), bounds.getBottom());
    }
    for (int index = 1; index < 6; ++index)
    {
        const auto y = bounds.getY() + bounds.getHeight() * index / 6.0f;
        graphics.drawHorizontalLine(static_cast<int>(y), bounds.getX(), bounds.getRight());
    }

    juce::Path spectrum;
    spectrum.startNewSubPath(bounds.getX(), bounds.getBottom() - 22.0f);
    for (float x = 0.0f; x <= bounds.getWidth(); x += 4.0f)
    {
        const auto normalized = x / bounds.getWidth();
        const auto ripple = std::sin(normalized * 42.0f) * 12.0f + std::sin(normalized * 93.0f) * 5.0f;
        const auto envelope = std::sin(normalized * juce::MathConstants<float>::pi) * 72.0f;
        spectrum.lineTo(bounds.getX() + x, bounds.getBottom() - 20.0f - envelope - ripple);
    }
    graphics.setColour(theme::electricBlue.withAlpha(0.22f));
    graphics.strokePath(spectrum, juce::PathStrokeType(2.0f));

    const auto node = nodePosition();
    for (int ring = 4; ring >= 1; --ring)
    {
        graphics.setColour(theme::electricBlue.withAlpha(0.06f * static_cast<float>(5 - ring)));
        graphics.fillEllipse(juce::Rectangle<float>(static_cast<float>(ring * 22), static_cast<float>(ring * 22)).withCentre(node));
    }
    graphics.setColour(theme::electricBlue);
    graphics.fillEllipse(juce::Rectangle<float>(22.0f, 22.0f).withCentre(node));
    graphics.setColour(theme::primaryText);
    graphics.fillEllipse(juce::Rectangle<float>(8.0f, 8.0f).withCentre(node));
}

void SpectrumGraph::mouseDown(const juce::MouseEvent& event)
{
    grabKeyboardFocus();
    if (event.mods.isPopupMenu())
    {
        showContextMenu();
        return;
    }
    dragging = event.position.getDistanceFrom(nodePosition()) <= 34.0f;
}

void SpectrumGraph::mouseDrag(const juce::MouseEvent& event)
{
    if (dragging && event.getDistanceFromDragStart() >= 2)
        setFromPosition(event.position);
}

void SpectrumGraph::mouseDoubleClick(const juce::MouseEvent& event)
{
    setFromPosition(event.position);
}

void SpectrumGraph::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    const auto scale = event.mods.isShiftDown() ? 0.08f : 0.45f;
    const auto qId = bandId("q");
    setParameter(qId, juce::jlimit(0.05f, 50.0f, parameter(qId, 1.0f) + wheel.deltaY * scale));
    repaint();
}

bool SpectrumGraph::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        setParameter(bandId("enabled"), 0.0f);
        repaint();
        return true;
    }
    return false;
}

void SpectrumGraph::showContextMenu()
{
    juce::PopupMenu menu;
    juce::PopupMenu types;
    types.addItem(101, "Bell", true, true);
    types.addItem(102, "Low Shelf");
    types.addItem(103, "High Shelf");
    types.addItem(104, "Low Cut");
    types.addItem(105, "High Cut");
    types.addItem(106, "Notch");
    types.addItem(107, "Band Pass");
    types.addItem(108, "Tilt");
    menu.addSubMenu("Band Type", types);

    juce::PopupMenu modes;
    modes.addItem(201, "Stereo", true, true);
    modes.addItem(202, "Left");
    modes.addItem(203, "Right");
    modes.addItem(204, "Mid");
    modes.addItem(205, "Side");
    menu.addSubMenu("Processing Mode", modes);

    juce::PopupMenu dynamics;
    dynamics.addItem(301, "Static", true, true);
    dynamics.addItem(302, "Dynamic Down");
    dynamics.addItem(303, "Dynamic Up");
    menu.addSubMenu("Dynamic Mode", dynamics);
    menu.addSeparator();
    menu.addItem(401, "Solo");
    menu.addItem(402, "Bypass");
    menu.addItem(403, "Delta");
    menu.addItem(404, "Reset");
    menu.addItem(405, "Duplicate", false);
    menu.addItem(406, "Copy");
    menu.addItem(407, "Paste", false);
    menu.addItem(408, "Delete");

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [this](int result)
    {
        if (result >= 101 && result <= 108) setParameter(bandId("type"), static_cast<float>(result - 101));
        else if (result >= 201 && result <= 205) setParameter(bandId("stereo_mode"), static_cast<float>(result - 201));
        else if (result >= 301 && result <= 303) setParameter(bandId("dynamic_mode"), static_cast<float>(result - 301));
        else if (result == 402 || result == 408) setParameter(bandId("enabled"), 0.0f);
        else if (result == 403)
        {
            const auto deltaId = bandId("delta");
            setParameter(deltaId, parameter(deltaId, 0.0f) > 0.5f ? 0.0f : 1.0f);
        }
        else if (result == 404)
        {
            resetParameter(bandId("frequency"));
            resetParameter(verticalParameter);
            resetParameter(bandId("q"));
            resetParameter(bandId("enabled"));
        }
        repaint();
    });
}
}
