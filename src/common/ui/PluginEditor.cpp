#include "PluginEditor.h"
#include "Theme.h"
#include "BinaryData.h"

#include <map>

namespace amanorsac
{
namespace
{
constexpr int controlsPerPage = 8;

juce::String pageGroupFor(const juce::String& parameterId)
{
    const auto firstDot = parameterId.indexOfChar('.');
    if (firstDot < 0)
        return "Global";

    const auto family = parameterId.substring(0, firstDot);
    if (family != "band" && family != "tap" && family != "zone" && family != "xover")
        return "Global";

    const auto secondDot = parameterId.indexOfChar(firstDot + 1, '.');
    if (secondDot < 0)
        return "Global";

    return family.toUpperCase() + " " + parameterId.substring(firstDot + 1, secondDot);
}
}

PluginEditor::PluginEditor(PluginProcessor& owner)
    : AudioProcessorEditor(owner), processor(owner)
{
    int imageSize = 0;
    const auto resourceName = processor.spec.id + "_png";
    if (const auto* imageData = AmanorsacBinaryData::getNamedResource(resourceName.toRawUTF8(), imageSize))
        designImage = juce::ImageFileFormat::loadFrom(imageData, static_cast<size_t>(imageSize));

    inspectorTitle.setText("LIVE PARAMETER INSPECTOR", juce::dontSendNotification);
    inspectorTitle.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    inspectorTitle.setColour(juce::Label::textColourId,
                             processor.spec.isAnalog() ? theme::brass : theme::electricBlue);
    inspectorTitle.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(inspectorTitle);

    inspectorHint.setText("Live, automatable controls - saved with your session.", juce::dontSendNotification);
    inspectorHint.setFont(juce::FontOptions(11.0f));
    inspectorHint.setColour(juce::Label::textColourId, theme::secondaryText);
    inspectorHint.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(inspectorHint);

    pageSelector.setJustificationType(juce::Justification::centred);
    pageSelector.setColour(juce::ComboBox::backgroundColourId, theme::digitalPanel);
    pageSelector.setColour(juce::ComboBox::outlineColourId,
                           processor.spec.isAnalog() ? theme::brass.withAlpha(0.6f) : theme::digitalBorder);
    addAndMakeVisible(pageSelector);
    addAndMakeVisible(previousPage);
    addAndMakeVisible(nextPage);
    addAndMakeVisible(controlsContent);

    const auto accent = processor.spec.isAnalog() ? theme::brass : theme::electricBlue;
    for (auto* button : { &previousPage, &nextPage })
    {
        button->setColour(juce::TextButton::buttonColourId, theme::digitalPanel);
        button->setColour(juce::TextButton::buttonOnColourId, accent.darker(0.35f));
        button->setColour(juce::TextButton::textColourOffId, accent);
    }

    buildPages();
    pageSelector.onChange = [this]
    {
        currentPage = juce::jlimit(0, juce::jmax(0, static_cast<int>(pages.size()) - 1),
                                   pageSelector.getSelectedItemIndex());
        rebuildPage();
        resized();
    };
    previousPage.onClick = [this] { changePage(-1); };
    nextPage.onClick = [this] { changePage(1); };
    rebuildPage();

    setResizable(true, true);
    setResizeLimits(1080, 500, 2000, 1100);
    setSize(1320, 600);
}

void PluginEditor::buildPages()
{
    juce::StringArray groupOrder;
    std::map<juce::String, std::vector<size_t>> grouped;

    for (size_t index = 0; index < processor.spec.parameters.size(); ++index)
    {
        const auto group = pageGroupFor(processor.spec.parameters[index].id);
        if (grouped.find(group) == grouped.end())
            groupOrder.add(group);
        grouped[group].push_back(index);
    }

    for (const auto& group : groupOrder)
    {
        const auto& indices = grouped[group];
        const auto pageCount = juce::jmax(1, static_cast<int>((indices.size() + controlsPerPage - 1)
                                                              / controlsPerPage));
        for (int page = 0; page < pageCount; ++page)
        {
            ControlPage controlPage;
            controlPage.name = group.toUpperCase();
            if (pageCount > 1)
                controlPage.name += "  " + juce::String(page + 1) + "/" + juce::String(pageCount);
            const auto begin = static_cast<size_t>(page * controlsPerPage);
            const auto end = juce::jmin(indices.size(), begin + static_cast<size_t>(controlsPerPage));
            controlPage.parameterIndices.assign(indices.begin() + static_cast<std::ptrdiff_t>(begin),
                                                indices.begin() + static_cast<std::ptrdiff_t>(end));
            pages.push_back(std::move(controlPage));
        }
    }

    for (int index = 0; index < static_cast<int>(pages.size()); ++index)
        pageSelector.addItem(pages[static_cast<size_t>(index)].name, index + 1);
    if (!pages.empty())
        pageSelector.setSelectedItemIndex(0, juce::dontSendNotification);
}

void PluginEditor::rebuildPage()
{
    controlsContent.removeAllChildren();
    controls.clear();
    if (pages.empty())
        return;

    const auto& page = pages[static_cast<size_t>(currentPage)];
    for (const auto parameterIndex : page.parameterIndices)
    {
        auto control = std::make_unique<ParameterControl>(processor.state,
                                                          processor.spec.parameters[parameterIndex],
                                                          processor.spec.isAnalog());
        controlsContent.addAndMakeVisible(*control);
        controls.push_back(std::move(control));
    }
    previousPage.setEnabled(currentPage > 0);
    nextPage.setEnabled(currentPage + 1 < static_cast<int>(pages.size()));
}

void PluginEditor::changePage(int delta)
{
    if (pages.empty())
        return;
    currentPage = juce::jlimit(0, static_cast<int>(pages.size()) - 1, currentPage + delta);
    pageSelector.setSelectedItemIndex(currentPage, juce::sendNotificationSync);
}

int PluginEditor::inspectorWidth() const noexcept
{
    return juce::jlimit(320, 430, getWidth() / 3);
}

juce::Rectangle<int> PluginEditor::designBounds() const noexcept
{
    return getLocalBounds().withTrimmedRight(inspectorWidth());
}

void PluginEditor::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colours::black);
    const auto design = designBounds();
    if (designImage.isValid())
        graphics.drawImageWithin(designImage, design.getX(), design.getY(), design.getWidth(), design.getHeight(),
                                 juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize);
    else
    {
        graphics.setColour(processor.spec.isAnalog() ? theme::analogMetal : theme::digitalPanel);
        graphics.fillRect(design);
        graphics.setColour(theme::primaryText);
        graphics.setFont(juce::FontOptions(30.0f, juce::Font::bold));
        graphics.drawText(processor.spec.displayName, design.reduced(40), juce::Justification::centred);
    }

    const auto inspector = getLocalBounds().withTrimmedLeft(design.getWidth()).toFloat();
    juce::ColourGradient panel(theme::digitalPanel.brighter(0.05f), inspector.getX(), inspector.getY(),
                               theme::digitalBackground, inspector.getRight(), inspector.getBottom(), false);
    graphics.setGradientFill(panel);
    graphics.fillRect(inspector);
    graphics.setColour((processor.spec.isAnalog() ? theme::brass : theme::electricBlue).withAlpha(0.55f));
    graphics.drawVerticalLine(static_cast<int>(inspector.getX()), inspector.getY(), inspector.getBottom());
    graphics.setColour(theme::secondaryText.withAlpha(0.5f));
    graphics.setFont(juce::FontOptions(10.0f));
    graphics.drawText(processor.spec.id + "  /  " + juce::String(pages.size()) + " CONTROL PAGES",
                      inspector.toNearestInt().removeFromBottom(24).reduced(12, 0),
                      juce::Justification::centredRight);
}

void PluginEditor::resized()
{
    auto inspector = getLocalBounds().removeFromRight(inspectorWidth()).reduced(12);
    inspectorTitle.setBounds(inspector.removeFromTop(28));
    inspectorHint.setBounds(inspector.removeFromTop(32));
    inspector.removeFromTop(5);

    auto navigation = inspector.removeFromTop(34);
    previousPage.setBounds(navigation.removeFromLeft(34));
    navigation.removeFromLeft(6);
    nextPage.setBounds(navigation.removeFromRight(34));
    navigation.removeFromRight(6);
    pageSelector.setBounds(navigation);
    inspector.removeFromTop(10);
    inspector.removeFromBottom(24);
    controlsContent.setBounds(inspector);

    const auto columns = 2;
    const auto rows = juce::jmax(1, (static_cast<int>(controls.size()) + columns - 1) / columns);
    const auto cellWidth = controlsContent.getWidth() / columns;
    const auto cellHeight = controlsContent.getHeight() / rows;
    for (size_t index = 0; index < controls.size(); ++index)
    {
        const auto column = static_cast<int>(index) % columns;
        const auto row = static_cast<int>(index) / columns;
        controls[index]->setBounds(column * cellWidth + 3, row * cellHeight + 3,
                                   cellWidth - 6, cellHeight - 6);
    }
}
}
