#pragma once

#include "Library.h"
#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>

namespace spa::library
{

// Preset save/load and the factory-preset generator. Presets are
// human-readable XML wrapping the same state tree the host chunk uses;
// sample paths inside are portable ("$LIB$/...").
//
// Directory layout under the presets root:
//   Factory/<Category>/<Name>.spasynth   (generated from library packs)
//   User/<Name>.spasynth
class PresetManager : public juce::ChangeBroadcaster
{
public:
    static constexpr const char* presetExtension = ".spasynth";

    struct PresetInfo
    {
        juce::String name;
        juce::String category;
        juce::File file;
    };

    PresetManager (juce::AudioProcessorValueTreeState& apvts,
                   std::function<juce::ValueTree()> captureState,
                   std::function<void (const juce::ValueTree&)> applyState,
                   juce::File presetsRoot);

    void rescan();
    const std::vector<PresetInfo>& getPresets() const { return presets; }
    juce::StringArray getCategories() const;

    bool loadPreset (int index);
    bool loadPresetFile (const juce::File&);
    void loadNext();
    void loadPrevious();

    // Restores every parameter to its ParameterRegistry default (the
    // pristine state captured at construction, before any preset/session
    // load) — "start afresh" after randomizing or a long tweak session.
    void resetToDefault();

    bool saveUserPreset (const juce::String& name);
    juce::File getUserPresetFolder() const { return presetsRoot.getChildFile ("User"); }

    juce::String getCurrentName() const { return currentName; }

    // Writes showcase presets for each pack (Keys / Texture / Pulse
    // templates). Fast: builds state trees directly, never loads audio.
    // Returns the number of presets written; skips packs that already have
    // a factory folder.
    int generateFactoryPresets (const std::vector<Pack>& packs,
                                const juce::File& libraryRoot);

private:
    juce::ValueTree makeTemplateState() const;
    bool writePreset (const juce::File& file, const juce::String& name,
                      const juce::ValueTree& state) const;

    juce::AudioProcessorValueTreeState& apvts;
    std::function<juce::ValueTree()> captureState;
    std::function<void (const juce::ValueTree&)> applyState;
    juce::File presetsRoot;

    juce::ValueTree defaultState;   // pristine, captured at construction
    std::vector<PresetInfo> presets;
    int currentIndex = -1;
    juce::String currentName { "Init" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetManager)
};

} // namespace spa::library
