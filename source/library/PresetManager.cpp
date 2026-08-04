#include "PresetManager.h"
#include "../params/ParameterRegistry.h"

namespace spa::library
{

namespace
{
    constexpr const char* presetTag = "SPASynthPreset";

    // Sets a raw (real-world) parameter value inside a captured state tree.
    void writeParam (juce::ValueTree& state, const juce::String& paramID, float realValue)
    {
        for (auto child : state)
        {
            if (child.hasType ("PARAM") && child.getProperty ("id").toString() == paramID)
            {
                child.setProperty ("value", (double) realValue, nullptr);
                return;
            }
        }

        juce::ValueTree p ("PARAM");
        p.setProperty ("id", paramID, nullptr);
        p.setProperty ("value", (double) realValue, nullptr);
        state.appendChild (p, nullptr);
    }

    void writeSamplePath (juce::ValueTree& state, int slot, const juce::String& portablePath)
    {
        auto samples = state.getOrCreateChildWithName ("SAMPLES", nullptr);
        samples.setProperty ("slot" + juce::String (slot), portablePath, nullptr);
    }

    float routeDestValue (const juce::String& destParamID)
    {
        return (float) (params::modDestIndex (destParamID) + 1);  // choice 0 = None
    }
}

PresetManager::PresetManager (std::function<juce::ValueTree()> capture,
                              std::function<void (const juce::ValueTree&)> apply,
                              juce::File root)
    : captureState (std::move (capture)),
      applyState (std::move (apply)),
      presetsRoot (std::move (root))
{
    defaultState = captureState().createCopy();
    rescan();
}

void PresetManager::rescan()
{
    presets.clear();

    const auto addFrom = [this] (const juce::File& folder, const juce::String& category)
    {
        for (const auto& f : folder.findChildFiles (juce::File::findFiles, false,
                                                    "*" + juce::String (presetExtension)))
            presets.push_back ({ f.getFileNameWithoutExtension(), category, f });
    };

    const auto factory = presetsRoot.getChildFile ("Factory");
    for (const auto& categoryDir : factory.findChildFiles (juce::File::findDirectories, false))
        addFrom (categoryDir, categoryDir.getFileName());

    addFrom (presetsRoot.getChildFile ("User"), "User");

    std::sort (presets.begin(), presets.end(),
               [] (const PresetInfo& a, const PresetInfo& b)
               {
                   const auto c = a.category.compareIgnoreCase (b.category);
                   return c != 0 ? c < 0 : a.name.compareIgnoreCase (b.name) < 0;
               });

    sendChangeMessage();
}

juce::StringArray PresetManager::getCategories() const
{
    juce::StringArray categories;
    for (const auto& p : presets)
        categories.addIfNotAlreadyThere (p.category);
    return categories;
}

bool PresetManager::loadPreset (int index)
{
    if (index < 0 || index >= (int) presets.size())
        return false;

    return loadPresetFile (presets[(size_t) index].file);
}

void PresetManager::resetToDefault()
{
    applyState (defaultState);

    currentName = "Init";
    currentIndex = -1;

    sendChangeMessage();
}

bool PresetManager::loadPresetFile (const juce::File& file)
{
    const auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName (presetTag) || xml->getFirstChildElement() == nullptr)
        return false;

    const auto state = juce::ValueTree::fromXml (*xml->getFirstChildElement());
    if (! state.isValid())
        return false;

    applyState (state);

    currentName = xml->getStringAttribute ("name", file.getFileNameWithoutExtension());
    currentIndex = -1;
    for (size_t i = 0; i < presets.size(); ++i)
        if (presets[i].file == file)
            currentIndex = (int) i;

    sendChangeMessage();
    return true;
}

void PresetManager::loadNext()
{
    if (! presets.empty())
        loadPreset ((currentIndex + 1) % (int) presets.size());
}

void PresetManager::loadPrevious()
{
    if (! presets.empty())
        loadPreset (currentIndex <= 0 ? (int) presets.size() - 1 : currentIndex - 1);
}

bool PresetManager::writePreset (const juce::File& file, const juce::String& name,
                                 const juce::ValueTree& state) const
{
    juce::XmlElement root (presetTag);
    root.setAttribute ("name", name);
    root.setAttribute ("version", 1);
    root.addChildElement (state.createXml().release());

    file.getParentDirectory().createDirectory();
    return root.writeTo (file);
}

bool PresetManager::saveUserPreset (const juce::String& name)
{
    const auto file = getUserPresetFolder()
                          .getChildFile (juce::File::createLegalFileName (name)
                                         + presetExtension);

    if (! writePreset (file, name, captureState()))
        return false;

    currentName = name;
    rescan();
    for (size_t i = 0; i < presets.size(); ++i)
        if (presets[i].file == file)
            currentIndex = (int) i;

    return true;
}

juce::ValueTree PresetManager::makeTemplateState() const
{
    auto state = defaultState.createCopy();

    // House defaults for all factory presets: gentle chaos and a hint of air.
    writeParam (state, params::id::chaos::mix, 0.7f);
    return state;
}

int PresetManager::generateFactoryPresets (const std::vector<Pack>& packs,
                                           const juce::File& libraryRoot)
{
    namespace id = params::id;
    int written = 0;

    for (const auto& pack : packs)
    {
        if (pack.wavs.isEmpty())
            continue;

        const auto categoryDir = presetsRoot.getChildFile ("Factory")
                                            .getChildFile (pack.name);
        if (categoryDir.isDirectory()
            && ! categoryDir.findChildFiles (juce::File::findFiles, false,
                                             "*" + juce::String (presetExtension)).isEmpty())
            continue;  // already generated

        const auto smallest = pack.wavs.getFirst();
        const auto largest = pack.wavs.getLast();
        const auto middle = pack.wavs[pack.wavs.size() / 2];

        // --- "Keys": the SFX as a playable, keytracked instrument ------------
        {
            auto state = makeTemplateState();
            writeParam (state, id::oscSlot (0, id::osc::mode), (float) (int) params::OscMode::sample);
            writeSamplePath (state, 0, toPortable (smallest, libraryRoot));
            writeParam (state, id::ampRelease, 0.35f);
            writeParam (state, id::fx::reverbEnable, 1.0f);
            writeParam (state, id::fx::reverbMix, 0.25f);

            const auto name = pack.name + " Keys";
            written += writePreset (categoryDir.getChildFile (
                juce::File::createLegalFileName (name) + presetExtension), name, state) ? 1 : 0;
        }

        // --- "Texture": granular cloud, slow LFO scrubbing the position ------
        {
            auto state = makeTemplateState();
            writeParam (state, id::oscSlot (0, id::osc::mode), (float) (int) params::OscMode::granular);
            writeSamplePath (state, 0, toPortable (largest, libraryRoot));
            writeParam (state, id::oscSlot (0, id::osc::keytrack), 0.0f);
            writeParam (state, id::oscSlot (0, id::osc::grainSize), 180.0f);
            writeParam (state, id::oscSlot (0, id::osc::grainDensity), 25.0f);
            writeParam (state, id::oscSlot (0, id::osc::grainSpray), 0.3f);
            writeParam (state, id::ampAttack, 0.8f);
            writeParam (state, id::ampRelease, 1.2f);
            writeParam (state, id::lfoParam (0, id::lfo::rate), 0.07f);
            writeParam (state, id::routeParam (0, id::route::source), (float) (int) params::ModSource::lfo1);
            writeParam (state, id::routeParam (0, id::route::dest),
                        routeDestValue (id::oscSlot (0, id::osc::grainPos)));
            writeParam (state, id::routeParam (0, id::route::depth), 0.35f);
            writeParam (state, id::chaos::positionAmount, 0.35f);
            writeParam (state, id::fx::reverbEnable, 1.0f);
            writeParam (state, id::fx::reverbSize, 0.7f);
            writeParam (state, id::fx::reverbMix, 0.45f);
            writeParam (state, id::fx::chorusEnable, 1.0f);

            const auto name = pack.name + " Texture";
            written += writePreset (categoryDir.getChildFile (
                juce::File::createLegalFileName (name) + presetExtension), name, state) ? 1 : 0;
        }

        // --- "Pulse": SFX-as-modulator — the sample's own amplitude opens the
        // filter on a wavetable voice ------------------------------------------
        {
            auto state = makeTemplateState();
            writeParam (state, id::oscSlot (0, id::osc::position), 0.66f);  // saw-ish
            writeParam (state, id::oscSlot (1, id::osc::enable), 1.0f);
            writeParam (state, id::oscSlot (1, id::osc::mode), (float) (int) params::OscMode::sample);
            writeSamplePath (state, 1, toPortable (middle, libraryRoot));
            writeParam (state, id::oscSlot (1, id::osc::keytrack), 0.0f);
            writeParam (state, id::oscSlot (1, id::osc::level), -30.0f);
            writeParam (state, id::filter1Type, (float) (int) params::FilterType::lp24);
            writeParam (state, id::filter1Cutoff, 350.0f);
            writeParam (state, id::routeParam (0, id::route::source),
                        (float) (params::sfxFollowerBase + 2));  // SFX B Amp
            writeParam (state, id::routeParam (0, id::route::dest),
                        routeDestValue (id::filter1Cutoff));
            writeParam (state, id::routeParam (0, id::route::depth), 0.7f);
            writeParam (state, id::fx::delayEnable, 1.0f);
            writeParam (state, id::fx::delayMix, 0.25f);

            const auto name = pack.name + " Pulse";
            written += writePreset (categoryDir.getChildFile (
                juce::File::createLegalFileName (name) + presetExtension), name, state) ? 1 : 0;
        }
    }

    if (written > 0)
        rescan();

    return written;
}

} // namespace spa::library
