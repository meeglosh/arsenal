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
    // v2 = the SFX-follower recipe table. v3 = every Pulse/Keys/Texture
    // variant keeps the pack's own WAV audible in OSC A, drops the
    // arpeggiator everywhere, and switches variant selection from a
    // per-pack hash to round-robin-by-sorted-index (see variantForIndex
    // below) so alphabetically adjacent packs never land on the same
    // recipe by chance.
    static constexpr int factoryRecipeVersion = 3;

    static constexpr int numKeysVariants = 6;
    static constexpr int numTextureVariants = 5;
    static constexpr int numPulseVariants = 6;

    // Deterministic 0-based variant index for the pack's position in the
    // alphabetically-sorted (case-insensitive) pack list, offset per
    // archetype so the three presets generated for one pack don't line up
    // in a fixed pattern across archetypes, and so adjacent packs (index
    // N, N+1) always land on different Pulse/Keys/Texture recipes.
    // NOTE: inserting a new pack shifts every later pack's index and thus
    // its variant assignment -- acceptable, since preset *names* (and so
    // user favorites) are unaffected, only which recipe a pack gets.
    // Exposed for tests.
    static int pulseVariantForIndex (int sortedIndex) { return (sortedIndex + 4) % numPulseVariants; }
    static int keysVariantForIndex (int sortedIndex) { return sortedIndex % numKeysVariants; }
    static int textureVariantForIndex (int sortedIndex) { return (sortedIndex + 2) % numTextureVariants; }

private:
    juce::ValueTree makeTemplateState() const;
    bool writePreset (const juce::File& file, const juce::String& name,
                      const juce::ValueTree& state, int recipeVersionStamp = 0) const;

    juce::ValueTree buildKeysState (const juce::File& smallest, const juce::File& libraryRoot,
                                    int variant) const;
    juce::ValueTree buildTextureState (const juce::File& smallest, const juce::File& middle,
                                       const juce::File& largest, const juce::File& libraryRoot,
                                       int variant) const;
    juce::ValueTree buildPulseState (const juce::File& smallest, const juce::File& middle,
                                     const juce::File& largest, const juce::File& libraryRoot,
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
