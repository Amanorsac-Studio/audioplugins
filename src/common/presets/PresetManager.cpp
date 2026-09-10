#include "PresetManager.h"

#include "BinaryData.h"

#include <algorithm>

namespace amanorsac::presets
{
namespace
{
const juce::Identifier presetType { "AmanorsacPreset" };
const juce::Identifier tagsType { "Tags" };
const juce::Identifier parametersType { "Parameters" };
const juce::Identifier parameterType { "Param" };

juce::String productVersion()
{
   #ifdef JucePlugin_VersionString
    return JucePlugin_VersionString;
   #else
    return "dev";
   #endif
}
}

const juce::StringArray& PresetManager::categoryOrder()
{
    // Source first, because that is how someone reaches for a preset, then the
    // places a whole mix passes through, then the odd ones.
    static const juce::StringArray order { "Vocals", "Drums", "Bass", "Guitars", "Keys",
                                           "Strings & Horns", "Mix Bus", "Master", "Creative",
                                           "Utility", userCategory };
    return order;
}

juce::StringArray PresetManager::categories() const
{
    juce::StringArray used;
    for (const auto& item : items)
    {
        if (item.file == juce::File() && item.parameters.getNumChildren() == 0 && item.builtIn) continue;
        const auto category = item.category.isNotEmpty() ? item.category
                            : item.builtIn ? juce::String("Utility") : juce::String(userCategory);
        used.addIfNotAlreadyThere(category);
    }

    juce::StringArray ordered;
    for (const auto& known : categoryOrder())
        if (used.contains(known)) ordered.add(known);
    for (const auto& other : used)
        if (! ordered.contains(other)) ordered.add(other);
    return ordered;
}

const juce::StringArray& PresetManager::tagKeys()
{
    static const juce::StringArray keys { "Source", "Intent", "Genre", "Intensity", "Character" };
    return keys;
}

PresetManager::PresetManager(juce::AudioProcessorValueTreeState& tree, const PluginSpec& pluginSpec)
    : state(tree), spec(pluginSpec)
{
    for (const auto& descriptor : spec.parameters)
        state.addParameterListener(descriptor.id, this);
    rescan();
}

PresetManager::~PresetManager()
{
    for (const auto& descriptor : spec.parameters)
        state.removeParameterListener(descriptor.id, this);
}

juce::File PresetManager::userDirectory(const juce::String& pluginId)
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
               .getChildFile("Amanorsac Studio")
               .getChildFile("Presets")
               .getChildFile(pluginId);
}

// ------------------------------------------------------------------ listing

/** The bank that ships inside the product. It is authored as JSON next to the
    contracts and embedded at build time, so a customer has usable starting
    points the moment they install, with nothing to copy into place. */
void PresetManager::addFactoryBank()
{
    int size = 0;
    const auto resource = spec.id + "_factory_json";
    const auto* data = AmanorsacBinaryData::getNamedResource(resource.toRawUTF8(), size);
    if (data == nullptr || size <= 0) return;

    const auto bank = juce::JSON::parse(juce::String::fromUTF8(data, size));
    const auto* presets = bank.getProperty("presets", {}).getArray();
    if (presets == nullptr) return;

    for (const auto& entry : *presets)
    {
        auto* object = entry.getDynamicObject();
        if (object == nullptr) continue;

        PresetInfo info;
        info.name = object->getProperty("name").toString();
        if (info.name.isEmpty()) continue;
        info.builtIn = true;
        info.author = bank.getProperty("author", "Amanorsac Studio").toString();
        info.category = object->getProperty("category").toString();

        if (const auto* tags = object->getProperty("tags").getDynamicObject())
            for (const auto& key : tagKeys())
                if (tags->hasProperty(key)) info.tags.set(key, tags->getProperty(key).toString());

        juce::ValueTree parameters(parametersType);
        if (const auto* values = object->getProperty("values").getDynamicObject())
            for (const auto& property : values->getProperties())
            {
                const auto id = property.name.toString();
                const auto* descriptor = findDescriptor(id);
                if (descriptor == nullptr) continue;

                juce::ValueTree item(parameterType);
                item.setProperty("id", id, nullptr);
                item.setProperty("value", resolveValue(*descriptor, property.value), nullptr);
                parameters.appendChild(item, nullptr);
            }
        info.parameters = parameters;
        items.push_back(std::move(info));
    }
}

bool PresetManager::isFactory(int index) const
{
    if (index <= 0 || index >= static_cast<int>(items.size())) return false;
    return items[static_cast<size_t>(index)].builtIn;
}

void PresetManager::rescan()
{
    const auto keepName = currentName();

    items.clear();
    PresetInfo builtIn;
    builtIn.name = "Default";
    builtIn.builtIn = true;
    items.push_back(builtIn);
    addFactoryBank();

    auto directory = userDirectory();
    directory.createDirectory();
    auto files = directory.findChildFiles(juce::File::findFiles, false, "*" + juce::String(fileExtension));
    std::sort(files.begin(), files.end(), [](const juce::File& a, const juce::File& b)
    {
        return a.getFileNameWithoutExtension().compareIgnoreCase(b.getFileNameWithoutExtension()) < 0;
    });

    for (const auto& file : files)
    {
        PresetInfo info;
        info.name = file.getFileNameWithoutExtension();
        info.file = file;
        info.category = userCategory;
        if (auto xml = juce::XmlDocument::parse(file))
        {
            const auto tree = juce::ValueTree::fromXml(*xml);
            if (tree.hasType(presetType))
            {
                info.name = tree.getProperty("name", info.name).toString();
                info.author = tree.getProperty("author").toString();
                const auto tags = tree.getChildWithName(tagsType);
                for (const auto& key : tagKeys())
                    if (tags.hasProperty(key))
                        info.tags.set(key, tags.getProperty(key).toString());
            }
        }
        items.push_back(info);
    }

    current = 0;
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i].name == keepName) { current = static_cast<int>(i); break; }

    sendChangeMessage();
}

juce::String PresetManager::currentName() const
{
    if (items.empty()) return "Default";
    return items[static_cast<size_t>(juce::jlimit(0, static_cast<int>(items.size()) - 1, current))].name;
}

bool PresetManager::currentIsUserPreset() const
{
    return current > 0 && current < static_cast<int>(items.size()) && ! items[static_cast<size_t>(current)].builtIn;
}

const ParameterDescriptor* PresetManager::findDescriptor(const juce::String& id) const
{
    for (const auto& descriptor : spec.parameters)
        if (descriptor.id == id) return &descriptor;
    return nullptr;
}

/** A bank may name an enum by its label ("tube") or a switch by true/false,
    which reads far better than an index in a file a human maintains. */
float PresetManager::resolveValue(const ParameterDescriptor& descriptor, const juce::var& value)
{
    if (value.isBool()) return static_cast<bool>(value) ? 1.0f : 0.0f;
    if (value.isString())
    {
        const auto text = value.toString().trim();
        if (descriptor.kind == ParameterDescriptor::Kind::choice)
            for (int i = 0; i < descriptor.choices.size(); ++i)
                if (descriptor.choices[i].equalsIgnoreCase(text)) return static_cast<float>(i);
        if (text.equalsIgnoreCase("on") || text.equalsIgnoreCase("true")) return 1.0f;
        if (text.equalsIgnoreCase("off") || text.equalsIgnoreCase("false")) return 0.0f;
        return text.getFloatValue();
    }
    return juce::jlimit(descriptor.minimum, descriptor.maximum, static_cast<float>(static_cast<double>(value)));
}

// ------------------------------------------------------------------ loading

void PresetManager::setParameterFromHost(const juce::String& id, float value)
{
    if (auto* parameter = state.getParameter(id))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
        parameter->endChangeGesture();
    }
}

juce::ValueTree PresetManager::captureParameterValues() const
{
    juce::ValueTree parameters(parametersType);
    for (const auto& descriptor : spec.parameters)
        if (const auto* raw = state.getRawParameterValue(descriptor.id))
        {
            juce::ValueTree entry(parameterType);
            entry.setProperty("id", descriptor.id, nullptr);
            entry.setProperty("value", raw->load(), nullptr);
            parameters.appendChild(entry, nullptr);
        }
    return parameters;
}

void PresetManager::applyParameterValues(const juce::ValueTree& parameters, const juce::String& transactionName)
{
    // Every parameter is written, defaults first, so a preset that predates a
    // newer parameter still produces a fully defined state. The whole load is
    // one undo step.
    const juce::ScopedValueSetter<bool> guard(applying, true);
    if (state.undoManager != nullptr)
        state.undoManager->beginNewTransaction(transactionName);

    for (const auto& descriptor : spec.parameters)
        setParameterFromHost(descriptor.id, descriptor.defaultValue);

    for (const auto& entry : parameters)
        if (entry.hasType(parameterType))
            setParameterFromHost(entry.getProperty("id").toString(),
                                 static_cast<float>(static_cast<double>(entry.getProperty("value"))));
}

void PresetManager::loadDefault()
{
    applyParameterValues(juce::ValueTree(parametersType), "Load Default preset");
    current = 0;
    modified.store(false);
    sendChangeMessage();
}

bool PresetManager::load(int index)
{
    if (index < 0 || index >= static_cast<int>(items.size())) return false;
    const auto& info = items[static_cast<size_t>(index)];
    if (info.builtIn)
    {
        applyParameterValues(info.parameters.isValid() ? info.parameters : juce::ValueTree(parametersType),
                             "Load preset " + info.name);
        current = index;
        modified.store(false);
        sendChangeMessage();
        return true;
    }

    auto xml = juce::XmlDocument::parse(info.file);
    if (xml == nullptr) return false;
    if (! applyPresetTree(juce::ValueTree::fromXml(*xml))) return false;

    current = index;
    modified.store(false);
    sendChangeMessage();
    return true;
}

bool PresetManager::loadNext()
{
    if (items.empty()) return false;
    return load((current + 1) % static_cast<int>(items.size()));
}

bool PresetManager::loadPrevious()
{
    if (items.empty()) return false;
    return load((current - 1 + static_cast<int>(items.size())) % static_cast<int>(items.size()));
}

// ------------------------------------------------------------------ saving

juce::String PresetManager::sanitiseFileName(const juce::String& name)
{
    return juce::File::createLegalFileName(name.trim()).substring(0, 80);
}

bool PresetManager::save(const juce::String& name, const juce::String& author,
                         const juce::StringPairArray& tags, juce::String* error)
{
    const auto clean = name.trim();
    if (clean.isEmpty() || clean.equalsIgnoreCase("Default"))
    {
        if (error != nullptr) *error = "Choose a name for the preset.";
        return false;
    }

    auto directory = userDirectory();
    directory.createDirectory();
    const auto file = directory.getChildFile(sanitiseFileName(clean) + fileExtension);

    const auto tree = toPresetTree(clean, author, tags);
    auto xml = tree.createXml();
    if (xml == nullptr || ! xml->writeTo(file))
    {
        if (error != nullptr) *error = "Could not write " + file.getFullPathName();
        return false;
    }

    rescan();
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i].file == file) current = static_cast<int>(i);
    modified.store(false);
    sendChangeMessage();
    return true;
}

bool PresetManager::remove(int index, juce::String* error)
{
    if (index <= 0 || index >= static_cast<int>(items.size()))
    {
        if (error != nullptr) *error = "The Default preset cannot be deleted.";
        return false;
    }
    const auto file = items[static_cast<size_t>(index)].file;
    if (! file.deleteFile())
    {
        if (error != nullptr) *error = "Could not delete " + file.getFullPathName();
        return false;
    }
    const auto wasCurrent = index == current;
    rescan();
    if (wasCurrent) { current = 0; modified.store(true); }
    sendChangeMessage();
    return true;
}

bool PresetManager::rename(int index, const juce::String& newName, juce::String* error)
{
    const auto clean = newName.trim();
    if (index <= 0 || index >= static_cast<int>(items.size()))
    {
        if (error != nullptr) *error = "The Default preset cannot be renamed.";
        return false;
    }
    if (clean.isEmpty())
    {
        if (error != nullptr) *error = "Choose a name for the preset.";
        return false;
    }

    const auto oldFile = items[static_cast<size_t>(index)].file;
    const auto newFile = oldFile.getSiblingFile(sanitiseFileName(clean) + fileExtension);
    if (newFile != oldFile && newFile.existsAsFile())
    {
        if (error != nullptr) *error = "A preset called " + clean + " already exists.";
        return false;
    }

    auto xml = juce::XmlDocument::parse(oldFile);
    if (xml == nullptr)
    {
        if (error != nullptr) *error = "Could not read " + oldFile.getFullPathName();
        return false;
    }
    auto tree = juce::ValueTree::fromXml(*xml);
    tree.setProperty("name", clean, nullptr);
    auto rewritten = tree.createXml();
    if (rewritten == nullptr || ! rewritten->writeTo(newFile))
    {
        if (error != nullptr) *error = "Could not write " + newFile.getFullPathName();
        return false;
    }
    if (newFile != oldFile) oldFile.deleteFile();

    const auto wasCurrent = index == current;
    rescan();
    if (wasCurrent)
        for (size_t i = 0; i < items.size(); ++i)
            if (items[i].file == newFile) current = static_cast<int>(i);
    sendChangeMessage();
    return true;
}

// ------------------------------------------------------------------ A/B

void PresetManager::captureSlot(int index)
{
    slots[static_cast<size_t>(juce::jlimit(0, 1, index))] = captureParameterValues();
}

bool PresetManager::recallSlot(int index)
{
    const auto which = static_cast<size_t>(juce::jlimit(0, 1, index));
    if (! slots[which].isValid()) return false;

    const auto before = captureParameterValues();
    applyParameterValues(slots[which], index == 0 ? "Recall A" : "Recall B");
    slot = static_cast<int>(which);
    if (! before.isEquivalentTo(slots[which])) modified.store(true);
    sendChangeMessage();
    return true;
}

void PresetManager::toggleAB()
{
    captureSlot(slot);
    const auto other = 1 - slot;
    if (! slots[static_cast<size_t>(other)].isValid())
        slots[static_cast<size_t>(other)] = slots[static_cast<size_t>(slot)].createCopy();
    recallSlot(other);
}

bool PresetManager::slotHasData(int index) const
{
    return slots[static_cast<size_t>(juce::jlimit(0, 1, index))].isValid();
}

// ------------------------------------------------------------------ clipboard

void PresetManager::copyToClipboard() const
{
    if (auto xml = toPresetTree(currentName(), {}, {}).createXml())
        juce::SystemClipboard::copyTextToClipboard(xml->toString());
}

bool PresetManager::pasteFromClipboard(juce::String* error)
{
    auto xml = juce::XmlDocument::parse(juce::SystemClipboard::getTextFromClipboard());
    if (xml == nullptr)
    {
        if (error != nullptr) *error = "The clipboard does not hold Amanorsac settings.";
        return false;
    }
    if (! applyPresetTree(juce::ValueTree::fromXml(*xml), error)) return false;
    modified.store(true);
    sendChangeMessage();
    return true;
}

// ------------------------------------------------------------------ format

juce::ValueTree PresetManager::toPresetTree(const juce::String& name, const juce::String& author,
                                            const juce::StringPairArray& tags) const
{
    juce::ValueTree tree(presetType);
    tree.setProperty("schema", schemaVersion, nullptr);
    tree.setProperty("plugin", spec.id, nullptr);
    tree.setProperty("product", spec.displayName, nullptr);
    tree.setProperty("name", name, nullptr);
    tree.setProperty("author", author, nullptr);
    tree.setProperty("version", productVersion(), nullptr);
    tree.setProperty("created", juce::Time::getCurrentTime().toISO8601(true), nullptr);

    juce::ValueTree tagTree(tagsType);
    for (const auto& key : tagKeys())
        tagTree.setProperty(key, tags.getValue(key, {}), nullptr);
    tree.appendChild(tagTree, nullptr);
    tree.appendChild(captureParameterValues(), nullptr);
    return tree;
}

bool PresetManager::applyPresetTree(const juce::ValueTree& tree, juce::String* error)
{
    if (! tree.hasType(presetType))
    {
        if (error != nullptr) *error = "That is not an Amanorsac preset.";
        return false;
    }
    const auto plugin = tree.getProperty("plugin").toString();
    if (plugin != spec.id)
    {
        if (error != nullptr)
            *error = "That preset belongs to " + tree.getProperty("product", plugin).toString()
                   + ", not " + spec.displayName + ".";
        return false;
    }
    if (static_cast<int>(tree.getProperty("schema", 1)) > schemaVersion)
    {
        if (error != nullptr) *error = "That preset was made by a newer version of " + spec.displayName + ".";
        return false;
    }

    applyParameterValues(tree.getChildWithName(parametersType),
                         "Load preset " + tree.getProperty("name").toString());
    return true;
}

// ------------------------------------------------------------------ dirty tracking

void PresetManager::parameterChanged(const juce::String&, float)
{
    // May arrive from the audio thread under host automation: only flip the
    // flag and post; ChangeBroadcaster delivers on the message thread.
    if (applying) return;
    if (! modified.exchange(true))
        sendChangeMessage();
}
}
