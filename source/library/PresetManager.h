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
//   User/<Name>.spasynth                 (category "User")
//   User/<Bank>/[.../]<Name>.spasynth    (a user preset bank -- category is
//                                          the bank's folder name; anything
//                                          nested deeper still belongs to
//                                          its top-level bank)
class PresetManager : public juce::ChangeBroadcaster
{
public:
    static constexpr const char* presetExtension = ".spasynth";

    struct PresetInfo
    {
        juce::String name;
        juce::String category;
        juce::File file;
        bool isUser = false;   // true for anything under User/ (root or a
                                // bank subfolder). NOT the same as
                                // category == "User" -- a bank's category is
                                // its own folder name.
    };

    PresetManager (std::function<juce::ValueTree()> captureState,
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

    // If chosenFolder is inside the User presets folder (a bank the user
    // just picked or created via the save dialog's "New Folder"), the
    // preset is written there; otherwise it falls back to the User root.
    bool saveUserPreset (const juce::String& name, const juce::File& chosenFolder = {});
    juce::File getUserPresetFolder() const { return presetsRoot.getChildFile ("User"); }

    juce::String getCurrentName() const { return currentName; }

    // Writes showcase presets for each pack (Keys / Texture / Pulse
    // templates, each with several distinct sonic recipes -- see the
    // "Recipe table" comment in PresetManager.cpp). Fast: builds state
    // trees directly, never loads audio. Returns the number of presets
    // written; regenerates a pack's Factory folder whenever its existing
    // presets are missing the "recipe" stamp or carry an older
    // factoryRecipeVersion, and otherwise skips it.
    int generateFactoryPresets (const std::vector<Pack>& packs,
                                const juce::File& libraryRoot);

    // Bumped whenever the factory-preset recipes change; stamped into every
    // generated preset's XML root ("recipe" attribute) so generateFactoryPresets
    // knows to regenerate stale Factory folders. v1 = the original
    // one-recipe-per-archetype scheme (no stamp at all -- treated as v1).
    static constexpr int factoryRecipeVersion = 2;

    static constexpr int numKeysVariants = 6;
    static constexpr int numTextureVariants = 5;
    static constexpr int numPulseVariants = 6;

    // Deterministic 0-based variant index for a pack name, pure function of
    // the name (FNV-1a over its UTF-8 bytes, mixed with an archetype tag --
    // see PresetManager.cpp). Exposed for tests: the audibility test picks
    // pack names whose hash lands on each variant rather than needing a
    // separate test-only generation path.
    static int pulseVariantForPack (const juce::String& packName);
    static int keysVariantForPack (const juce::String& packName);
    static int textureVariantForPack (const juce::String& packName);

private:
    juce::ValueTree makeTemplateState() const;
    bool writePreset (const juce::File& file, const juce::String& name,
                      const juce::ValueTree& state, int recipeVersionStamp = 0) const;

    juce::ValueTree buildKeysState (const juce::File& smallest, const juce::File& libraryRoot,
                                    int variant) const;
    juce::ValueTree buildTextureState (const juce::File& smallest, const juce::File& middle,
                                       const juce::File& largest, const juce::File& libraryRoot,
                                       int variant) const;
    juce::ValueTree buildPulseState (const juce::File& middle, const juce::File& libraryRoot,
                                     int variant) const;

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
