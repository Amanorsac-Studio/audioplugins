#pragma once

#include "common/state/PluginSpec.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <vector>

namespace amanorsac::presets
{
struct PresetInfo
{
    juce::String name;
    juce::File file;              // empty for anything that ships inside the product
    bool builtIn = false;         // Default, or one of the factory bank
    juce::String author;
    juce::String category;        // Vocals, Drums, Bass ... which menu group it sits in
    juce::StringPairArray tags;   // Source, Intent, Genre, Intensity, Character
    juce::ValueTree parameters;   // the settings themselves, for a built-in preset
};

/** Preset, A/B and clipboard service shared by every product.

    Presets are XML files in the platform user-data folder, never the install
    location, following the build specification: plugin ID, schema version,
    parameter map, tags and author. Loading applies values through the
    parameter contract (setValueNotifyingHost), so the host, automation and
    undo all see a preset change exactly as they would see a knob move. Nothing
    here touches DSP internals.

    No factory presets are shipped yet: the specification defers authoring
    until the parameter contracts are frozen. The only built-in entry is
    Default, which is every parameter at its contract default.
*/
class PresetManager final : public juce::ChangeBroadcaster,
                            private juce::AudioProcessorValueTreeState::Listener
{
public:
    static constexpr int schemaVersion = 1;
    static constexpr const char* fileExtension = ".amanorsacpreset";
    static const juce::StringArray& tagKeys();

    PresetManager(juce::AudioProcessorValueTreeState&, const PluginSpec&);
    ~PresetManager() override;

    static juce::File userDirectory(const juce::String& pluginId);
    juce::File userDirectory() const { return userDirectory(spec.id); }

    /** Re-reads the factory bank and the user folder. Keeps the current
        selection by name. */
    void rescan();

    /** True when this entry ships with the product and cannot be overwritten. */
    [[nodiscard]] bool isFactory(int index) const;

    /** Menu groups in the order they should be offered, factory bank first and
        anything the customer saved last. */
    [[nodiscard]] juce::StringArray categories() const;
    static const juce::StringArray& categoryOrder();
    /** The group a user preset falls into when it names none. */
    static constexpr const char* userCategory = "My Presets";

    const std::vector<PresetInfo>& presets() const noexcept { return items; }
    int currentIndex() const noexcept { return current; }
    juce::String currentName() const;
    bool currentIsUserPreset() const;
    /** True once any parameter moved since the last load or save. */
    bool isModified() const noexcept { return modified.load(); }

    bool load(int index);
    bool loadNext();
    bool loadPrevious();
    void loadDefault();

    bool save(const juce::String& name, const juce::String& author,
              const juce::StringPairArray& tags, juce::String* error = nullptr);
    bool remove(int index, juce::String* error = nullptr);
    bool rename(int index, const juce::String& newName, juce::String* error = nullptr);

    // ---- A/B snapshot service
    void captureSlot(int slot);
    bool recallSlot(int slot);
    void toggleAB();
    int activeSlot() const noexcept { return slot; }
    bool slotHasData(int index) const;

    // ---- clipboard
    void copyToClipboard() const;
    bool pasteFromClipboard(juce::String* error = nullptr);

    // ---- serialisation
    juce::ValueTree toPresetTree(const juce::String& name, const juce::String& author,
                                 const juce::StringPairArray& tags) const;
    bool applyPresetTree(const juce::ValueTree&, juce::String* error = nullptr);

private:
    void parameterChanged(const juce::String& parameterId, float newValue) override;
    juce::ValueTree captureParameterValues() const;
    void applyParameterValues(const juce::ValueTree& parameters, const juce::String& transactionName);
    void setParameterFromHost(const juce::String& id, float value);
    void addFactoryBank();
    [[nodiscard]] const ParameterDescriptor* findDescriptor(const juce::String& id) const;
    static float resolveValue(const ParameterDescriptor&, const juce::var&);
    static juce::String sanitiseFileName(const juce::String&);

    juce::AudioProcessorValueTreeState& state;
    const PluginSpec& spec;
    std::vector<PresetInfo> items;
    int current = 0;
    std::atomic<bool> modified { false };
    bool applying = false;
    std::array<juce::ValueTree, 2> slots;
    int slot = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetManager)
};
}
