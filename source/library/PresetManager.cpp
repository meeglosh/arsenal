#include "PresetManager.h"
#include "../params/ParameterRegistry.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace spa::library
{

namespace
{
    constexpr const char* presetTag = "SPASynthPreset";

    // Header-only duration probe (opens a reader but never decodes samples --
    // cheap enough to call per file during factory-preset generation, which
    // otherwise never touches audio). Returns 0.0 if the file can't be read.
    double sampleDurationSeconds (const juce::File& wav)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (wav));
        if (reader == nullptr || reader->sampleRate <= 0.0)
            return 0.0;
        return (double) reader->lengthInSamples / reader->sampleRate;
    }

    // Real SFX library files can run many seconds to minutes long, and a
    // held factory-preset note must stay audible for as long as it's held.
    // SamplePlayer's default loop (loopStart=0, loopEnd=1) loops the WHOLE
    // file, so on a long file the note just rides that one file's own
    // natural decay -- which, for a percussive/impact recording, decays into
    // true digital silence well before a 3s hold ends (found via the real-
    // library audibility test: dozens of Keys/Pulse presets went silent this
    // way). Looping a short window at the very start of the file (where a
    // recorded hit/impact's audible transient almost always lives) instead
    // keeps the note re-triggering that transient for as long as it's held,
    // however long the underlying file actually is. Files already shorter
    // than the cap are left looping whole (their natural loop period is
    // already short enough).
    // 1.2s (rather than something closer to a single hit's real duration)
    // gives a long file's loop window enough width to very likely cross a
    // genuinely louder moment somewhere inside it, not just whatever level
    // happens to sit at the chosen start offset -- found empirically
    // against the quietest real ambience file in the library, where a
    // narrower window sometimes landed on a comparatively quiet stretch.
    constexpr double loopCapSeconds = 1.2;

    // SamplePlayer's own end-of-file cutoff (position >= len - 1.0) is
    // checked AFTER the loop-wrap check but uses a strictly smaller
    // threshold than a loopEnd sitting exactly at len -- so a loopEndNorm of
    // exactly 1.0 (the default, "loop the whole file") makes that cutoff
    // fire on every single note, before the wrap ever triggers, and the
    // sample never actually loops at all (found via the real-library
    // audibility test on several very short one-shot files, where even
    // whole-file-length loop periods well under the cap above still went
    // silent). Capping just short of 1.0 keeps the loop wrap point strictly
    // inside the file, so it always wins that race.
    constexpr float loopEndSafetyMargin = 0.999f;

    // loopEnd is expressed as an offset from startNorm (not from the file's
    // absolute 0) so a nonzero loop start (see writeSafeSampleLoop below)
    // still gets the same short, guaranteed-audible loop window ahead of
    // it, rather than the cap being measured from a point behind the loop's
    // own start (which could put loopEnd before loopStart entirely).
    float safeLoopEndNorm (const juce::File& wav, float startNorm)
    {
        const auto duration = sampleDurationSeconds (wav);
        if (duration <= 0.0)
            return loopEndSafetyMargin;
        const auto frac = (float) juce::jmin (1.0, loopCapSeconds / duration);
        return juce::jmin (startNorm + frac, loopEndSafetyMargin);
    }

    // Granular's default grain position (0.0, the very start of the file)
    // sits inside typical leading room-tone/silence on a lot of real field
    // recordings. A small inward bias keeps grains reading from where a
    // real recording is actually speaking up, on files of any length,
    // without materially changing the granular character.
    constexpr float safeGrainPosBase = 0.12f;

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

    // Applies the safe short loop window above to a sample-mode oscillator
    // slot. A short one-shot file (<= loopCapSeconds, the overwhelmingly
    // common real-library case -- impacts, footsteps, foley hits) starts
    // and loops from the true file start (0.0), where a percussive hit's
    // transient almost always is. A long file (an ambience/drone that's
    // actually going through the fractional loop window above) starts and
    // loops from a small inward offset instead, to skip a typical early
    // fade-in/room-tone stretch real field recordings often open with --
    // found via the real-library audibility test on one very long, slow-
    // opening ambience file that stayed silent even with the short loop
    // window, because that window itself sat right in the opening silence.
    // sampleStart and loopStart are set to the SAME offset so even the
    // very first pass (before the loop ever wraps) starts past it.
    void writeSafeSampleLoop (juce::ValueTree& state, int slot, const juce::File& wav)
    {
        const auto duration = sampleDurationSeconds (wav);
        const auto startNorm = duration > loopCapSeconds ? safeGrainPosBase : 0.0f;
        writeParam (state, params::id::oscSlot (slot, params::id::osc::sampleStart), startNorm);
        writeParam (state, params::id::oscSlot (slot, params::id::osc::loopStart), startNorm);
        writeParam (state, params::id::oscSlot (slot, params::id::osc::loopEnd),
                    safeLoopEndNorm (wav, startNorm));
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

    const auto addFrom = [this] (const juce::File& folder, const juce::String& category,
                                 bool isUser, bool recursive)
    {
        for (const auto& f : folder.findChildFiles (juce::File::findFiles, recursive,
                                                    "*" + juce::String (presetExtension)))
            presets.push_back ({ f.getFileNameWithoutExtension(), category, f, isUser });
    };

    const auto factory = presetsRoot.getChildFile ("Factory");
    for (const auto& categoryDir : factory.findChildFiles (juce::File::findDirectories, false))
        addFrom (categoryDir, categoryDir.getFileName(), false, false);

    const auto userRoot = presetsRoot.getChildFile ("User");
    addFrom (userRoot, "User", true, false);

    // Bank subfolders: each immediate subfolder of User/ is its own bank
    // (category = folder name), scanned recursively so nested folders
    // inside a bank still count as that same bank.
    for (const auto& bankDir : userRoot.findChildFiles (juce::File::findDirectories, false))
        addFrom (bankDir, bankDir.getFileName(), true, true);

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
                                 const juce::ValueTree& state, int recipeVersionStamp) const
{
    juce::XmlElement root (presetTag);
    root.setAttribute ("name", name);
    root.setAttribute ("version", 1);
    if (recipeVersionStamp > 0)
        root.setAttribute ("recipe", recipeVersionStamp);
    root.addChildElement (state.createXml().release());

    file.getParentDirectory().createDirectory();
    return root.writeTo (file);
}

bool PresetManager::saveUserPreset (const juce::String& name, const juce::File& chosenFolder)
{
    // Honor a bank subfolder chosen in the save dialog (including one just
    // created via "New Folder"), but only if it's actually inside the User
    // presets tree -- anywhere else falls back to the User root so the
    // preset browser can always find what was saved.
    auto targetFolder = getUserPresetFolder();
    if (chosenFolder != juce::File()
        && (chosenFolder == targetFolder || chosenFolder.isAChildOf (targetFolder)))
        targetFolder = chosenFolder;

    const auto file = targetFolder.getChildFile (juce::File::createLegalFileName (name)
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

// =============================================================================
// Recipe table (v4, factoryRecipeVersion). One line per variant: engine /
// envelope character / key mod routes / FX. The variant used for a given
// pack is round-robin by the pack's position in the alphabetically-sorted
// (case-insensitive) pack list, offset per archetype -- see
// {pulse,keys,texture}VariantForIndex() in PresetManager.h and the sort in
// generateFactoryPresets(). All routes reference only that pack's
// smallest/middle/largest WAV (whichever the archetype uses); oscillator
// levels stay <=0dB; the limiter is left at registry defaults. No variant,
// anywhere in this table, enables the arpeggiator. Every sample-mode OSC
// slot writes the safe short loop window (writeSafeSampleLoop) and every
// granular OSC slot starts its grain position at safeGrainPosBase, rather
// than the raw defaults (whole-file loop / position 0) -- both of which
// went silent against real, long SFX library files whose own content
// decays into true silence, or opens with room tone, well before a held
// note's 3s test window ends. See factoryPresetsRealLibraryAudibleTest.
//
// KEYS (numKeysVariants = 6), sample = smallest WAV unless noted:
//   0  sample, keytracked                    | plain ADSR release 0.35s      | (none)                                   | reverb (light)
//   1  sample + detuned wavetable layer       | quick attack, long sustain    | (none, static detune -12st/+8ct)         | reverb (light)
//   2  sample + a quieter (-10dB) pluck layer  | fast pluck-style ADSR         | (none)                                   | (dry)
//      underneath for transient bite
//   3  sample, high-passed                    | plain ADSR                    | (none)                                   | HP filter + chorus
//   4  sample + pitch-drop on note-on          | plain ADSR                    | env2 (decaying pulse) -> osc A fine      | reverb (larger)
//   5  sample, unison voice mode               | plain ADSR, longer release    | (none)                                   | reverb (light)
//
// TEXTURE (numTextureVariants = 5), sample = largest WAV unless noted:
//   0  granular, one slot                      | slow attack/release           | LFO1 -> grain position                   | reverb + chorus
//   1  granular + chaos-driven grain position   | slow attack/release           | chaos -> grain position (elevated)       | reverb (large)
//      (was "+ latched arp chord" pre-v3 -- arp removed)
//   2  two granular slots (largest + middle),   | slow attack/release           | (none, static detune)                    | reverb (light)
//      detuned against each other
//   3  granular through a swept band-pass       | slow attack/release           | LFO1 -> filter1 cutoff                   | band-pass filter + fold distortion
//   4  granular, middle WAV, tiny grains          | quick attack, short release   | (none, high grain density = the "glitch")| delay
//      (glitchy)
//
// PULSE (numPulseVariants = 6). Mike's v1.0.15/16 feedback: Pulse presets
// must be a genuine MIX, not sample-only -- the pack's own WAV AUDIBLY in
// OSC A (sample or granular mode, 0dB, keytracked unless noted as a drone)
// PLUS a real synth oscillator (wavetable/analog/fm/pluck) in OSC B at
// >= -12dB, with the sample's own SFX-follower (or an ENV/LFO on the
// audible layer) driving that synth layer in a DIFFERENT way per variant --
// level, pitch, the shared filter, wavetable position, FM amount, pluck
// damping. "SFX A" = the follower for OSC A's own sample (sfxFollowerBase +
// 0 amp / +1 pitch):
//   0  sample (middle WAV, kt) + wavetable        | fast attack, short    | SFX A amp -> osc B LEVEL                 | tremolo (rhythmic gate) + delay
//      layer (osc B, -8dB)                         release                | (sample dynamics gate the synth)         |
//   1  sample (middle WAV, kt), fast decay,        | fast decay, low       | env2 -> filter1 cutoff (percussive open);| crush distortion (low drive)
//      nonzero sustain floor + FM layer             sustain floor          | SFX A pitch -> osc B FINE                |
//      (osc B, -10dB)                                                      | (sample's pitch contour plays the synth) |
//   2  granular (largest WAV, NOT keytracked --    | slow attack/release,  | LFO1 -> filter1 cutoff; chaos -> amp;    | delay (ping-pong)
//      the drone exception) + a sustained analog     sustain 1              | SFX A amp -> osc B LEVEL (pad gated by   |
//      pad (osc B, -9dB, -12st)                                             | the sample's own dynamics)               |
//   3  sample (middle WAV, kt) + a quieter (-8dB)   | plain ADSR            | SFX A amp -> osc B WAVETABLE POSITION    | chorus
//      sub wavetable layer -12st (osc B)                                    | (sample dynamics scan the wavetable)     |
//   4  sample (middle WAV, kt) + an FM layer        | plain ADSR            | LFO1 -> filter1 cutoff (band-pass sweep);| fold distortion
//      (osc B, -10dB)                                                       | env2 -> osc B FM AMOUNT (metallic bite)  |
//   5  sample (smallest WAV, kt), bright            | short/percussive env, | SFX A amp -> osc A pan; SFX A amp ->     | reverb
//      (high-passed) + a plucked layer               nonzero sustain floor  | osc B PLUCK DAMP (excitation brightness) |
//      (osc B, -9dB)                                                        |                                           |
// =============================================================================

juce::ValueTree PresetManager::buildKeysState (const juce::File& smallest, const juce::File& libraryRoot,
                                               int variant) const
{
    namespace id = params::id;
    auto state = makeTemplateState();

    // Common to every variant: the primary sample, keytracked. A safe short
    // loop window (see writeSafeSampleLoop) keeps it audible for the whole
    // hold even on a long real SFX file whose own natural decay would
    // otherwise ride out into true silence well before the note is released.
    writeParam (state, id::oscSlot (0, id::osc::mode), (float) (int) params::OscMode::sample);
    writeSamplePath (state, 0, toPortable (smallest, libraryRoot));
    writeParam (state, id::oscSlot (0, id::osc::keytrack), 1.0f);
    writeParam (state, id::oscSlot (0, id::osc::level), 0.0f);   // full headroom, not the
                                                                  // registry's -6dB default --
                                                                  // real SFX files run quiet
                                                                  // enough already
    writeSafeSampleLoop (state, 0, smallest);

    switch (variant)
    {
        case 0:   // sample, keytracked (original recipe)
        default:
            writeParam (state, id::ampRelease, 0.35f);
            writeParam (state, id::fx::reverbEnable, 1.0f);
            writeParam (state, id::fx::reverbMix, 0.4f);
            break;

        case 1:   // sample + detuned wavetable layer underneath
            writeParam (state, id::ampAttack, 0.01f);
            writeParam (state, id::ampRelease, 0.5f);
            writeParam (state, id::oscSlot (1, id::osc::enable), 1.0f);
            writeParam (state, id::oscSlot (1, id::osc::mode), (float) (int) params::OscMode::wavetable);
            writeParam (state, id::oscSlot (1, id::osc::coarse), -12.0f);
            writeParam (state, id::oscSlot (1, id::osc::fine), 8.0f);
            writeParam (state, id::oscSlot (1, id::osc::level), -12.0f);
            writeParam (state, id::fx::reverbEnable, 1.0f);
            writeParam (state, id::fx::reverbMix, 0.35f);
            break;

        case 2:   // sample (kept audible in OSC A per the common preamble
                  // above) + a quieter pluck layer underneath for transient bite
            writeParam (state, id::oscSlot (1, id::osc::enable), 1.0f);
            writeParam (state, id::oscSlot (1, id::osc::mode), (float) (int) params::OscMode::pluck);
            writeParam (state, id::oscSlot (1, id::osc::pluckDamp), 0.4f);
            writeParam (state, id::oscSlot (1, id::osc::level), -10.0f);
            writeParam (state, id::ampAttack, 0.002f);
            writeParam (state, id::ampDecay, 0.25f);
            writeParam (state, id::ampSustain, 0.5f);
            writeParam (state, id::ampRelease, 0.3f);
            break;

        case 3:   // sample, high-passed, + chorus
            writeParam (state, id::ampRelease, 0.4f);
            writeParam (state, id::filter1Type, (float) (int) params::FilterType::hp12);
            writeParam (state, id::filter1Cutoff, 400.0f);
            writeParam (state, id::fx::chorusEnable, 1.0f);
            writeParam (state, id::fx::chorusRate, 0.6f);
            writeParam (state, id::fx::chorusDepth, 0.4f);
            break;

        case 4:   // sample + a decaying env2 pitch-drop on note-on
            writeParam (state, id::ampRelease, 0.4f);
            writeParam (state, id::envParam (2, "attack"), 0.001f);
            writeParam (state, id::envParam (2, "decay"), 0.6f);
            writeParam (state, id::envParam (2, "sustain"), 0.0f);
            writeParam (state, id::envParam (2, "release"), 0.1f);
            writeParam (state, id::routeParam (0, id::route::source), (float) (int) params::ModSource::env2);
            writeParam (state, id::routeParam (0, id::route::dest),
                        routeDestValue (id::oscSlot (0, id::osc::fine)));
            writeParam (state, id::routeParam (0, id::route::depth), 0.5f);
            writeParam (state, id::fx::reverbEnable, 1.0f);
            writeParam (state, id::fx::reverbSize, 0.6f);
            writeParam (state, id::fx::reverbMix, 0.5f);
            break;

        case 5:   // sample, unison voice mode
            writeParam (state, id::ampRelease, 0.45f);
            writeParam (state, id::voiceMode, (float) (int) params::VoiceMode::unison);
            writeParam (state, id::unisonVoices, 4.0f);
            writeParam (state, id::unisonDetune, 18.0f);
            writeParam (state, id::fx::reverbEnable, 1.0f);
            writeParam (state, id::fx::reverbMix, 0.3f);
            break;
    }

    return state;
}

juce::ValueTree PresetManager::buildTextureState (const juce::File& smallest, const juce::File& middle,
                                                   const juce::File& largest, const juce::File& libraryRoot,
                                                   int variant) const
{
    namespace id = params::id;
    auto state = makeTemplateState();
    juce::ignoreUnused (smallest);   // kept in the signature to match
                                      // buildKeysState/buildPulseState; no
                                      // current Texture variant uses it (see
                                      // variant 4's comment)

    // Common to every variant: granular on slot A, no keytracking (SFX
    // texture, not a pitched instrument), slow-ish amp envelope by default.
    // Grain position starts a little inward (safeGrainPosBase) rather than
    // at the very front of the file -- real field recordings routinely have
    // a beat of room tone/near-silence right at 0.0, which a static
    // position-0 grain cloud would otherwise read from indefinitely.
    // A generous base spray (each grain's own start jitters randomly around
    // the position, independent of any position drift below) is the real
    // safety net against a static grain position landing in one of the
    // long, quiet stretches real ambience/texture recordings often have --
    // found via the real-library audibility test, where a few long, quiet
    // files stayed silent at a fixed position even with chaos drifting it
    // only modestly. Variants may still raise it further for character.
    writeParam (state, id::oscSlot (0, id::osc::mode), (float) (int) params::OscMode::granular);
    writeParam (state, id::oscSlot (0, id::osc::keytrack), 0.0f);
    writeParam (state, id::oscSlot (0, id::osc::level), 0.0f);   // full headroom, not the
                                                                  // registry's -6dB default
    writeParam (state, id::oscSlot (0, id::osc::grainPos), safeGrainPosBase);
    writeParam (state, id::oscSlot (0, id::osc::grainSpray), 0.8f);
    writeParam (state, id::chaos::positionAmount, 0.4f);
    writeParam (state, id::ampAttack, 0.8f);
    writeParam (state, id::ampRelease, 1.2f);

    switch (variant)
    {
        case 0:   // granular cloud, slow LFO scrubbing the position (original recipe)
        default:
            writeSamplePath (state, 0, toPortable (largest, libraryRoot));
            writeParam (state, id::oscSlot (0, id::osc::grainSize), 180.0f);
            writeParam (state, id::oscSlot (0, id::osc::grainDensity), 25.0f);
            writeParam (state, id::lfoParam (0, id::lfo::rate), 0.07f);
            writeParam (state, id::routeParam (0, id::route::source), (float) (int) params::ModSource::lfo1);
            writeParam (state, id::routeParam (0, id::route::dest),
                        routeDestValue (id::oscSlot (0, id::osc::grainPos)));
            writeParam (state, id::routeParam (0, id::route::depth), 0.35f);
            writeParam (state, id::chaos::positionAmount, 0.35f);
            writeParam (state, id::fx::reverbEnable, 1.0f);
            writeParam (state, id::fx::reverbSize, 0.7f);
            writeParam (state, id::fx::reverbMix, 0.6f);
            writeParam (state, id::fx::chorusEnable, 1.0f);
            break;

        case 1:   // granular, elevated chaos-driven grain position (no arp)
            writeSamplePath (state, 0, toPortable (largest, libraryRoot));
            writeParam (state, id::oscSlot (0, id::osc::grainSize), 150.0f);
            writeParam (state, id::oscSlot (0, id::osc::grainDensity), 20.0f);
            writeParam (state, id::chaos::positionAmount, 0.4f);
            writeParam (state, id::fx::reverbEnable, 1.0f);
            writeParam (state, id::fx::reverbSize, 0.85f);
            writeParam (state, id::fx::reverbMix, 0.6f);
            break;

        case 2:   // two granular slots (largest + middle), detuned against each other
            writeSamplePath (state, 0, toPortable (largest, libraryRoot));
            writeParam (state, id::oscSlot (0, id::osc::grainSize), 160.0f);
            writeParam (state, id::oscSlot (0, id::osc::grainDensity), 22.0f);
            writeParam (state, id::oscSlot (0, id::osc::fine), -15.0f);
            writeParam (state, id::oscSlot (1, id::osc::enable), 1.0f);
            writeParam (state, id::oscSlot (1, id::osc::mode), (float) (int) params::OscMode::granular);
            writeSamplePath (state, 1, toPortable (middle, libraryRoot));
            writeParam (state, id::oscSlot (1, id::osc::keytrack), 0.0f);
            writeParam (state, id::oscSlot (1, id::osc::grainSize), 160.0f);
            writeParam (state, id::oscSlot (1, id::osc::grainDensity), 22.0f);
            writeParam (state, id::oscSlot (1, id::osc::grainPos), safeGrainPosBase);
            writeParam (state, id::oscSlot (1, id::osc::fine), 15.0f);
            writeParam (state, id::oscSlot (1, id::osc::level), -8.0f);
            writeParam (state, id::fx::reverbEnable, 1.0f);
            writeParam (state, id::fx::reverbMix, 0.35f);
            break;

        case 3:   // granular through a swept band-pass + fold distortion
            writeSamplePath (state, 0, toPortable (largest, libraryRoot));
            writeParam (state, id::oscSlot (0, id::osc::grainSize), 140.0f);
            writeParam (state, id::oscSlot (0, id::osc::grainDensity), 18.0f);
            writeParam (state, id::filter1Type, (float) (int) params::FilterType::bp12);
            writeParam (state, id::filter1Cutoff, 900.0f);
            writeParam (state, id::filter1Resonance, 0.4f);
            writeParam (state, id::lfoParam (0, id::lfo::rate), 0.15f);
            writeParam (state, id::routeParam (0, id::route::source), (float) (int) params::ModSource::lfo1);
            writeParam (state, id::routeParam (0, id::route::dest), routeDestValue (id::filter1Cutoff));
            writeParam (state, id::routeParam (0, id::route::depth), 0.6f);
            writeParam (state, id::fx::distEnable, 1.0f);
            writeParam (state, id::fx::distType, 2.0f);   // Fold
            writeParam (state, id::fx::distDrive, 0.4f);
            break;

        case 4:   // middle WAV, tiny grains (glitchy) + delay -- middle
                  // rather than the smallest file (found via the real-
                  // library audibility test: a pack's single smallest file
                  // is sometimes a real outlier, tens of dB quieter than
                  // everything else in the pack, e.g. a barely-audible
                  // one-off recording; the middle file is far more likely
                  // to be representative of the pack's actual level)
            writeSamplePath (state, 0, toPortable (middle, libraryRoot));
            writeParam (state, id::ampAttack, 0.02f);
            writeParam (state, id::ampRelease, 0.4f);
            writeParam (state, id::oscSlot (0, id::osc::grainSize), 15.0f);
            writeParam (state, id::oscSlot (0, id::osc::grainDensity), 80.0f);
            writeParam (state, id::oscSlot (0, id::osc::grainSpray), 0.5f);
            writeParam (state, id::fx::delayEnable, 1.0f);
            writeParam (state, id::fx::delayMix, 0.35f);
            writeParam (state, id::fx::delayFeedback, 0.3f);
            break;
    }

    return state;
}

juce::ValueTree PresetManager::buildPulseState (const juce::File& smallest, const juce::File& middle,
                                                const juce::File& largest, const juce::File& libraryRoot,
                                                int variant) const
{
    namespace id = params::id;
    auto state = makeTemplateState();

    // Common to every variant: the pack's own WAV, audible in OSC A at 0dB
    // (never a hidden/inaudible modulator layer), with the safe short loop
    // window so it stays audible against a long real file for the whole
    // hold. SFX A amp/pitch is that same audible sample's own follower
    // (sfxFollowerBase + 0 / +1). EVERY variant also enables a genuine
    // SYNTH oscillator (wavetable/analog/fm/pluck) in slot B or C, audible
    // at >= -12dB, and routes the sample's own follower/ENV/LFO onto that
    // synth layer in a different way per variant -- see the table above.
    const auto sfxAAmp = (float) (params::sfxFollowerBase + 0);
    const auto sfxAPitch = (float) (params::sfxFollowerBase + 1);

    switch (variant)
    {
        case 0:   // Gated Pulse: sample + a wavetable layer whose LEVEL is
                  // gated by the sample's own amp follower, tremolo + delay
        default:
            writeParam (state, id::oscSlot (0, id::osc::mode), (float) (int) params::OscMode::sample);
            writeSamplePath (state, 0, toPortable (middle, libraryRoot));
            writeParam (state, id::oscSlot (0, id::osc::keytrack), 1.0f);
            writeParam (state, id::oscSlot (0, id::osc::level), 0.0f);
            writeSafeSampleLoop (state, 0, middle);
            writeParam (state, id::ampAttack, 0.005f);
            writeParam (state, id::ampRelease, 0.2f);
            writeParam (state, id::oscSlot (1, id::osc::enable), 1.0f);
            writeParam (state, id::oscSlot (1, id::osc::mode), (float) (int) params::OscMode::wavetable);
            writeParam (state, id::oscSlot (1, id::osc::level), -8.0f);
            writeParam (state, id::routeParam (0, id::route::source), sfxAAmp);
            writeParam (state, id::routeParam (0, id::route::dest),
                        routeDestValue (id::oscSlot (1, id::osc::level)));
            writeParam (state, id::routeParam (0, id::route::depth), 0.7f);
            writeParam (state, id::fx::tremEnable, 1.0f);
            writeParam (state, id::fx::tremRate, 6.0f);
            writeParam (state, id::fx::tremDepth, 0.75f);
            writeParam (state, id::fx::delayEnable, 1.0f);
            writeParam (state, id::fx::delayMix, 0.25f);
            break;

        case 1:   // Percussive Pitch Play: sample, fast decay (nonzero sustain
                  // floor so it never rides fully to silence while held),
                  // env2 -> filter cutoff, + an FM layer whose PITCH follows
                  // the sample's own pitch contour, crush
            writeParam (state, id::oscSlot (0, id::osc::mode), (float) (int) params::OscMode::sample);
            writeSamplePath (state, 0, toPortable (middle, libraryRoot));
            writeParam (state, id::oscSlot (0, id::osc::keytrack), 1.0f);
            writeParam (state, id::oscSlot (0, id::osc::level), 0.0f);
            writeSafeSampleLoop (state, 0, middle);
            writeParam (state, id::ampAttack, 0.001f);
            writeParam (state, id::ampDecay, 0.15f);
            writeParam (state, id::ampSustain, 0.08f);
            writeParam (state, id::ampRelease, 0.05f);
            writeParam (state, id::filter1Type, (float) (int) params::FilterType::lp24);
            writeParam (state, id::filter1Cutoff, 300.0f);
            writeParam (state, id::envParam (2, "attack"), 0.001f);
            writeParam (state, id::envParam (2, "decay"), 0.2f);
            writeParam (state, id::envParam (2, "sustain"), 0.0f);
            writeParam (state, id::envParam (2, "release"), 0.05f);
            writeParam (state, id::routeParam (0, id::route::source), (float) (int) params::ModSource::env2);
            writeParam (state, id::routeParam (0, id::route::dest), routeDestValue (id::filter1Cutoff));
            writeParam (state, id::routeParam (0, id::route::depth), 0.8f);
            writeParam (state, id::oscSlot (1, id::osc::enable), 1.0f);
            writeParam (state, id::oscSlot (1, id::osc::mode), (float) (int) params::OscMode::fm);
            writeParam (state, id::oscSlot (1, id::osc::level), -10.0f);
            writeParam (state, id::routeParam (1, id::route::source), sfxAPitch);
            writeParam (state, id::routeParam (1, id::route::dest),
                        routeDestValue (id::oscSlot (1, id::osc::fine)));
            writeParam (state, id::routeParam (1, id::route::depth), 0.8f);
            writeParam (state, id::fx::distEnable, 1.0f);
            writeParam (state, id::fx::distType, 3.0f);   // Crush
            writeParam (state, id::fx::distDrive, 0.15f);
            break;

        case 2:   // Drone Bed: granular (largest WAV, NOT keytracked -- the
                  // drone exception) + a sustained analog pad whose LEVEL is
                  // gated by the sample's own amp follower (so the pad only
                  // breathes in while the source is actually speaking up),
                  // chaos -> amp, slow LFO -> LP24 cutoff, ping-pong delay
            writeParam (state, id::oscSlot (0, id::osc::mode), (float) (int) params::OscMode::granular);
            writeSamplePath (state, 0, toPortable (largest, libraryRoot));
            writeParam (state, id::oscSlot (0, id::osc::keytrack), 0.0f);
            writeParam (state, id::oscSlot (0, id::osc::level), 0.0f);
            writeParam (state, id::oscSlot (0, id::osc::grainSize), 220.0f);
            writeParam (state, id::oscSlot (0, id::osc::grainDensity), 15.0f);
            writeParam (state, id::oscSlot (0, id::osc::grainSpray), 0.2f);
            writeParam (state, id::oscSlot (0, id::osc::grainPos), safeGrainPosBase);
            writeParam (state, id::ampAttack, 1.5f);
            writeParam (state, id::ampSustain, 1.0f);
            writeParam (state, id::ampRelease, 2.0f);
            writeParam (state, id::filter1Type, (float) (int) params::FilterType::lp24);
            writeParam (state, id::filter1Cutoff, 700.0f);
            writeParam (state, id::chaos::ampAmount, 0.4f);
            writeParam (state, id::lfoParam (0, id::lfo::rate), 0.08f);
            writeParam (state, id::routeParam (0, id::route::source), (float) (int) params::ModSource::lfo1);
            writeParam (state, id::routeParam (0, id::route::dest), routeDestValue (id::filter1Cutoff));
            writeParam (state, id::routeParam (0, id::route::depth), 0.5f);
            writeParam (state, id::oscSlot (1, id::osc::enable), 1.0f);
            writeParam (state, id::oscSlot (1, id::osc::mode), (float) (int) params::OscMode::analog);
            writeParam (state, id::oscSlot (1, id::osc::analogShape), 4.0f);   // Sine
            writeParam (state, id::oscSlot (1, id::osc::coarse), -12.0f);
            writeParam (state, id::oscSlot (1, id::osc::level), -9.0f);
            writeParam (state, id::routeParam (1, id::route::source), sfxAAmp);
            writeParam (state, id::routeParam (1, id::route::dest),
                        routeDestValue (id::oscSlot (1, id::osc::level)));
            writeParam (state, id::routeParam (1, id::route::depth), 0.6f);
            writeParam (state, id::fx::delayEnable, 1.0f);
            writeParam (state, id::fx::delayPingPong, 1.0f);
            writeParam (state, id::fx::delayFeedback, 0.5f);
            writeParam (state, id::fx::delayMix, 0.35f);
            break;

        case 3:   // Wavetable Scan: sample + a quieter sub wavetable layer an
                  // octave down whose WAVETABLE POSITION is scanned by the
                  // sample's own amp follower, chorus
            writeParam (state, id::oscSlot (0, id::osc::mode), (float) (int) params::OscMode::sample);
            writeSamplePath (state, 0, toPortable (middle, libraryRoot));
            writeParam (state, id::oscSlot (0, id::osc::keytrack), 1.0f);
            writeParam (state, id::oscSlot (0, id::osc::level), 0.0f);
            writeSafeSampleLoop (state, 0, middle);
            writeParam (state, id::oscSlot (1, id::osc::enable), 1.0f);
            writeParam (state, id::oscSlot (1, id::osc::mode), (float) (int) params::OscMode::wavetable);
            writeParam (state, id::oscSlot (1, id::osc::coarse), -12.0f);
            writeParam (state, id::oscSlot (1, id::osc::level), -8.0f);
            writeParam (state, id::routeParam (0, id::route::source), sfxAAmp);
            writeParam (state, id::routeParam (0, id::route::dest),
                        routeDestValue (id::oscSlot (1, id::osc::position)));
            writeParam (state, id::routeParam (0, id::route::depth), 0.8f);
            writeParam (state, id::fx::chorusEnable, 1.0f);
            writeParam (state, id::fx::chorusDepth, 0.35f);
            break;

        case 4:   // Filtered FM Pulse: sample, band-pass swept by LFO,
                  // resonance, fold, + an FM layer whose FM AMOUNT is driven
                  // by env2 (a decaying pulse per note-on) for metallic bite
            writeParam (state, id::oscSlot (0, id::osc::mode), (float) (int) params::OscMode::sample);
            writeSamplePath (state, 0, toPortable (middle, libraryRoot));
            writeParam (state, id::oscSlot (0, id::osc::keytrack), 1.0f);
            writeParam (state, id::oscSlot (0, id::osc::level), 0.0f);
            writeSafeSampleLoop (state, 0, middle);
            writeParam (state, id::filter1Type, (float) (int) params::FilterType::bp12);
            writeParam (state, id::filter1Cutoff, 800.0f);
            writeParam (state, id::filter1Resonance, 0.6f);
            writeParam (state, id::lfoParam (0, id::lfo::rate), 0.2f);
            writeParam (state, id::routeParam (0, id::route::source), (float) (int) params::ModSource::lfo1);
            writeParam (state, id::routeParam (0, id::route::dest), routeDestValue (id::filter1Cutoff));
            writeParam (state, id::routeParam (0, id::route::depth), 0.7f);
            writeParam (state, id::oscSlot (1, id::osc::enable), 1.0f);
            writeParam (state, id::oscSlot (1, id::osc::mode), (float) (int) params::OscMode::fm);
            writeParam (state, id::oscSlot (1, id::osc::level), -10.0f);
            writeParam (state, id::envParam (2, "attack"), 0.002f);
            writeParam (state, id::envParam (2, "decay"), 0.4f);
            writeParam (state, id::envParam (2, "sustain"), 0.2f);
            writeParam (state, id::envParam (2, "release"), 0.3f);
            writeParam (state, id::routeParam (1, id::route::source), (float) (int) params::ModSource::env2);
            writeParam (state, id::routeParam (1, id::route::dest),
                        routeDestValue (id::oscSlot (1, id::osc::fmIndex)));
            writeParam (state, id::routeParam (1, id::route::depth), 0.7f);
            writeParam (state, id::fx::distEnable, 1.0f);
            writeParam (state, id::fx::distType, 2.0f);   // Fold
            writeParam (state, id::fx::distDrive, 0.3f);
            break;

        case 5:   // Bright Pluck Layer: sample (smallest WAV), high-passed,
                  // short env, own follower panning it, + a plucked layer
                  // whose DAMPING (excitation brightness) is driven by the
                  // sample's own amp follower, reverb
            writeParam (state, id::oscSlot (0, id::osc::mode), (float) (int) params::OscMode::sample);
            writeSamplePath (state, 0, toPortable (smallest, libraryRoot));
            writeParam (state, id::oscSlot (0, id::osc::keytrack), 1.0f);
            writeParam (state, id::oscSlot (0, id::osc::level), 0.0f);
            writeSafeSampleLoop (state, 0, smallest);
            writeParam (state, id::filter1Type, (float) (int) params::FilterType::hp12);
            writeParam (state, id::filter1Cutoff, 500.0f);
            writeParam (state, id::ampAttack, 0.001f);
            writeParam (state, id::ampDecay, 0.12f);
            writeParam (state, id::ampSustain, 0.06f);
            writeParam (state, id::ampRelease, 0.08f);
            writeParam (state, id::routeParam (0, id::route::source), sfxAAmp);
            writeParam (state, id::routeParam (0, id::route::dest),
                        routeDestValue (id::oscSlot (0, id::osc::pan)));
            writeParam (state, id::routeParam (0, id::route::depth), 0.8f);
            writeParam (state, id::oscSlot (1, id::osc::enable), 1.0f);
            writeParam (state, id::oscSlot (1, id::osc::mode), (float) (int) params::OscMode::pluck);
            writeParam (state, id::oscSlot (1, id::osc::level), -9.0f);
            writeParam (state, id::oscSlot (1, id::osc::pluckDamp), 0.3f);
            writeParam (state, id::routeParam (1, id::route::source), sfxAAmp);
            writeParam (state, id::routeParam (1, id::route::dest),
                        routeDestValue (id::oscSlot (1, id::osc::pluckDamp)));
            writeParam (state, id::routeParam (1, id::route::depth), 0.6f);
            writeParam (state, id::fx::reverbEnable, 1.0f);
            writeParam (state, id::fx::reverbSize, 0.5f);
            writeParam (state, id::fx::reverbMix, 0.4f);
            break;
    }

    return state;
}

int PresetManager::generateFactoryPresets (const std::vector<Pack>& packs,
                                           const juce::File& libraryRoot)
{
    int written = 0;

    // Variant assignment is round-robin by the pack's position in this
    // alphabetically-sorted (case-insensitive) copy, NOT the pack's name
    // hash -- see PresetManager.h. Sorting a copy keeps `packs`'s incoming
    // order (and thus the caller's iteration) untouched.
    auto sortedPacks = packs;
    std::sort (sortedPacks.begin(), sortedPacks.end(),
               [] (const Pack& a, const Pack& b) { return a.name.compareIgnoreCase (b.name) < 0; });

    for (int sortedIndex = 0; sortedIndex < (int) sortedPacks.size(); ++sortedIndex)
    {
        const auto& pack = sortedPacks[(size_t) sortedIndex];
        if (pack.wavs.isEmpty())
            continue;

        const auto categoryDir = presetsRoot.getChildFile ("Factory")
                                            .getChildFile (pack.name);

        const auto keysName = pack.name + " Keys";
        const auto textureName = pack.name + " Texture";
        const auto pulseName = pack.name + " Pulse";

        // Skip only when the folder already holds all three presets stamped
        // at the current recipe version; anything missing, unstamped
        // (pre-v2), or at an older version triggers a full regeneration of
        // the pack's three presets.
        const auto isCurrent = [&categoryDir] (const juce::String& name)
        {
            const auto f = categoryDir.getChildFile (juce::File::createLegalFileName (name)
                                                       + presetExtension);
            if (! f.existsAsFile())
                return false;
            const auto xml = juce::XmlDocument::parse (f);
            if (xml == nullptr || ! xml->hasTagName (presetTag))
                return false;
            return xml->getIntAttribute ("recipe", 1) == factoryRecipeVersion;
        };

        if (isCurrent (keysName) && isCurrent (textureName) && isCurrent (pulseName))
            continue;

        const auto smallest = pack.wavs.getFirst();
        const auto largest = pack.wavs.getLast();
        const auto middle = pack.wavs[pack.wavs.size() / 2];

        {
            const auto variant = keysVariantForIndex (sortedIndex);
            const auto state = buildKeysState (smallest, libraryRoot, variant);
            written += writePreset (categoryDir.getChildFile (
                juce::File::createLegalFileName (keysName) + presetExtension),
                keysName, state, factoryRecipeVersion) ? 1 : 0;
        }
        {
            const auto variant = textureVariantForIndex (sortedIndex);
            const auto state = buildTextureState (smallest, middle, largest, libraryRoot, variant);
            written += writePreset (categoryDir.getChildFile (
                juce::File::createLegalFileName (textureName) + presetExtension),
                textureName, state, factoryRecipeVersion) ? 1 : 0;
        }
        {
            const auto variant = pulseVariantForIndex (sortedIndex);
            const auto state = buildPulseState (smallest, middle, largest, libraryRoot, variant);
            written += writePreset (categoryDir.getChildFile (
                juce::File::createLegalFileName (pulseName) + presetExtension),
                pulseName, state, factoryRecipeVersion) ? 1 : 0;
        }
    }

    if (written > 0)
        rescan();

    return written;
}

} // namespace spa::library
