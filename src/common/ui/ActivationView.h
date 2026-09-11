#pragma once

#include "common/licensing/LicenseClient.h"
#include "AnalogUI.h"
#include "BinaryData.h"

#include <juce_gui_extra/juce_gui_extra.h>

namespace amanorsac
{
/** The one activation page, shown by whichever plug-in the customer opened
    first. One key unlocks the whole bundle on this machine, so this screen
    appears once and never again on that computer.

    It is deliberately the same in every product: same wording, same layout,
    same behaviour, so support only ever has one screen to talk about.
*/
class ActivationView final : public juce::Component,
                             private juce::ChangeListener
{
public:
    ActivationView(juce::String product, std::function<void()> onLicensed)
        : productName(std::move(product)), licensedCallback(std::move(onLicensed))
    {
        auto& client = licensing::LicenseClient::getInstance();

        entry.setFont(analog::labelFont(22.0f, false, 1.0f));
        entry.setJustification(juce::Justification::centred);
        entry.setTextToShowWhenEmpty("XXXX-XXXX-XXXX-XXXX", juce::Colour(0xff6d6758));
        entry.setInputRestrictions(19, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-");
        entry.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff0d0c0b));
        entry.setColour(juce::TextEditor::textColourId, juce::Colour(0xffecdfc2));
        entry.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff2a2723));
        entry.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colour(0xffd3b26a));
        entry.onReturnKey = [this] { submit(); };
        entry.onTextChange = [this] { if (message.isNotEmpty()) { message.clear(); repaint(); } };
        addAndMakeVisible(entry);

        activate.setButtonText("Activate");
        activate.onClick = [this] { submit(); };
        style(activate);
        addAndMakeVisible(activate);

        buy.setButtonText("Get a licence");
        buy.onClick = [] { juce::URL("https://amanorsac.studio").launchInDefaultBrowser(); };
        style(buy);
        addAndMakeVisible(buy);

        client.addChangeListener(this);
        client.start();
        if (client.isLicensed() && licensedCallback) licensedCallback();
    }

    ~ActivationView() override
    {
        licensing::LicenseClient::getInstance().removeChangeListener(this);
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff1d1b19), bounds.getCentreX(), 0.0f,
                                               juce::Colour(0xff0b0a09), bounds.getCentreX(),
                                               bounds.getBottom(), false));
        g.fillAll();

        auto panel = juce::Rectangle<float>(560.0f, 430.0f).withCentre(bounds.getCentre());
        g.setColour(juce::Colour(0xff16140f));
        g.fillRoundedRectangle(panel, 12.0f);
        g.setColour(juce::Colour(0xff2e2a23));
        g.drawRoundedRectangle(panel.reduced(0.5f), 12.0f, 1.2f);

        if (logo.isValid())
            g.drawImage(logo, panel.withHeight(74.0f).translated(0.0f, 26.0f).reduced(150.0f, 0.0f),
                        juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize);

        g.setColour(juce::Colour(0xffe6dcc4));
        g.setFont(analog::labelFont(19.0f, true, 1.0f));
        g.drawText(productName.toUpperCase(), panel.withHeight(24.0f).translated(0.0f, 112.0f),
                   juce::Justification::centred, false);

        g.setColour(juce::Colour(0xff9a9483));
        g.setFont(analog::labelFont(14.5f, false, 1.0f));
        g.drawFittedText("Enter your licence key once. It unlocks every plug-in in the bundle "
                         "on this computer.",
                         panel.withHeight(44.0f).translated(0.0f, 142.0f).reduced(58.0f, 0.0f).toNearestInt(),
                         juce::Justification::centredTop, 2);

        if (message.isNotEmpty())
        {
            g.setColour(failed ? juce::Colour(0xffe0603f) : juce::Colour(0xff7fd48a));
            g.setFont(analog::labelFont(14.0f, true, 1.0f));
            g.drawFittedText(message,
                             panel.withHeight(52.0f).translated(0.0f, 300.0f).reduced(40.0f, 0.0f).toNearestInt(),
                             juce::Justification::centredTop, 3);
        }

        g.setColour(juce::Colour(0xff6d6758));
        g.setFont(analog::labelFont(12.5f, false, 1.0f));
        g.drawText("This computer: " + licensing::LicenseClient::getInstance().deviceLabel(),
                   panel.withHeight(18.0f).translated(0.0f, panel.getHeight() - 34.0f),
                   juce::Justification::centred, false);
    }

    void resized() override
    {
        auto panel = juce::Rectangle<int>(560, 430).withCentre(getLocalBounds().getCentre());
        entry.setBounds(panel.getX() + 96, panel.getY() + 206, panel.getWidth() - 192, 48);
        activate.setBounds(panel.getX() + 96, panel.getY() + 262, panel.getWidth() - 192, 42);
        buy.setBounds(panel.getX() + 176, panel.getY() + 360, panel.getWidth() - 352, 32);
    }

private:
    static void style(juce::TextButton& button)
    {
        button.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff26231f));
        button.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe6dcc4));
    }

    void submit()
    {
        if (busy) return;
        busy = true;
        message = "Checking with the licence server...";
        failed = false;
        activate.setEnabled(false);
        repaint();

        juce::Component::SafePointer<ActivationView> safe(this);
        licensing::LicenseClient::getInstance().activate(entry.getText(),
            [safe](licensing::LicenseClient::Result result)
            {
                if (safe == nullptr) return;
                safe->busy = false;
                safe->activate.setEnabled(true);
                safe->failed = ! result.ok;
                if (result.ok)
                {
                    safe->message = "Activated.";
                    // The flag is already true: show the plug-in now rather
                    // than making anyone reopen it.
                    if (safe->licensedCallback) safe->licensedCallback();
                }
                else
                {
                    safe->message = result.message;
                    if (! result.devicesInUse.isEmpty())
                        safe->message << "\nIn use on: " << result.devicesInUse.joinIntoString(", ");
                }
                safe->repaint();
            });
    }

    void changeListenerCallback(juce::ChangeBroadcaster*) override
    {
        // Another plug-in in the bundle may have been activated meanwhile.
        if (licensing::LicenseClient::getInstance().isLicensed() && licensedCallback) licensedCallback();
    }

    static juce::Image loadLogo()
    {
        int size = 0;
        if (const auto* data = AmanorsacBinaryData::getNamedResource("AmanorsacLogo_png", size))
            return juce::ImageFileFormat::loadFrom(data, static_cast<size_t>(size));
        return {};
    }

    juce::String productName;
    std::function<void()> licensedCallback;
    juce::Image logo { loadLogo() };
    juce::TextEditor entry;
    juce::TextButton activate, buy;
    juce::String message;
    bool failed = false, busy = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ActivationView)
};
}
