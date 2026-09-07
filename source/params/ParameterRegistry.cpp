#include "ParameterRegistry.h"

namespace spa::params
{

juce::String sectionName (Section s)
{
    switch (s)
    {
        case Section::global:  return "Global";
        case Section::oscA:    return "Osc A";
        case Section::oscB:    return "Osc B";
        case Section::oscC:    return "Osc C";
        case Section::filter1: return "Filter 1";
        case Section::filter2: return "Filter 2";
        case Section::ampEnv:  return "Amp Env";
        case Section::env2:    return "Env 2";
        case Section::env3:    return "Env 3";
        case Section::lfo1:    return "LFO 1";
        case Section::lfo2:    return "LFO 2";
        case Section::lfo3:    return "LFO 3";
        case Section::macros:   return "Macros";
        case Section::arp:      return "Arpeggiator";
        case Section::chaos:    return "Chaos";
        case Section::fxDist:   return "FX Dist";
        case Section::fxChorus: return "FX Chorus";
        case Section::fxDelay:  return "FX Delay";
        case Section::fxReverb: return "FX Reverb";
        case Section::fxEQ:     return "FX EQ";
        case Section::fxMod:    return "FX Mod";
        case Section::fxTremVib: return "FX Trem/Vib";
        case Section::fxLimiter: return "FX Limiter";
        case Section::fxConvolve: return "FX Convolve";
        case Section::matrix:   return "Mod Matrix";
    }
    return {};
}

Section oscSection (int slotIndex)
{
    jassert (slotIndex >= 0 && slotIndex < numOscSlots);
    return (Section) ((int) Section::oscA + slotIndex);
}

Section lfoSection (int lfoIndex)
{
    jassert (lfoIndex >= 0 && lfoIndex < numLFOs);
    return (Section) ((int) Section::lfo1 + lfoIndex);
}

const juce::StringArray& modSourceNames()
{
    static const juce::StringArray names {
        "None", "Env 1 (Amp)", "Env 2", "Env 3",
        "LFO 1", "LFO 2", "LFO 3",
        "Macro 1", "Macro 2", "Macro 3", "Macro 4",
        "Velocity", "Mod Wheel", "Aftertouch",
        "Chaos",
        "SFX A Amp", "SFX A Pitch",
        "SFX B Amp", "SFX B Pitch",
        "SFX C Amp", "SFX C Pitch",
    };
    jassert (names.size() == numModSources);
    return names;
}

const juce::StringArray& lfoDivisionNames()
{
    static const juce::StringArray names {
        "8/1", "4/1", "2/1", "1/1",
        "1/2", "1/2T", "1/4", "1/4.", "1/4T",
        "1/8", "1/8.", "1/8T", "1/16", "1/16T", "1/32",
    };
    return names;
}

float lfoDivisionBeats (int divisionChoice)
{
    static constexpr float beats[] = {
        32.0f, 16.0f, 8.0f, 4.0f,
        2.0f, 4.0f / 3.0f, 1.0f, 1.5f, 2.0f / 3.0f,
        0.5f, 0.75f, 1.0f / 3.0f, 0.25f, 1.0f / 6.0f, 0.125f,
    };
    static_assert (std::size (beats) == 15);
    return beats[juce::jlimit (0, (int) std::size (beats) - 1, divisionChoice)];
}

juce::String destDisplayName (const ParamDef& def)
{
    // Osc names already carry their slot letter ("A Position").
    const auto isOsc = def.section == Section::oscA || def.section == Section::oscB
                    || def.section == Section::oscC;
    return isOsc ? "Osc " + def.name : sectionName (def.section) + " " + def.name;
}

namespace id
{
    juce::String oscSlotLetter (int slotIndex)
    {
        return juce::String::charToString ((juce::juce_wchar) ('A' + slotIndex));
    }

    juce::String oscSlot (int slotIndex, const char* key)
    {
        return "osc" + oscSlotLetter (slotIndex) + "." + key;
    }

    juce::String envParam (int envNumber, const char* key)
    {
        jassert (envNumber == 2 || envNumber == 3);
        return "env" + juce::String (envNumber) + "." + key;
    }

    juce::String eqBand (int band, const char* key)
    {
        return "fxEQ.band" + juce::String (band) + "." + key;
    }

    juce::String lfoParam (int lfoIndex, const char* key)
    {
        return "lfo" + juce::String (lfoIndex + 1) + "." + key;
    }

    juce::String macro (int macroIndex)
    {
        return "macros.macro" + juce::String (macroIndex + 1);
    }

    juce::String routeParam (int routeIndex, const char* key)
    {
        return "matrix.route" + juce::String (routeIndex + 1) + "." + key;
    }
}

static juce::NormalisableRange<float> frequencyRange (float min, float max)
{
    juce::NormalisableRange<float> r (min, max);
    r.setSkewForCentre (std::sqrt (min * max)); // log-ish response, geometric centre
    return r;
}

static juce::NormalisableRange<float> skewedRange (float min, float max, float centre)
{
    juce::NormalisableRange<float> r (min, max);
    r.setSkewForCentre (centre); // more resolution around `centre`
    return r;
}

static void addOscSlotParams (std::vector<ParamDef>& p, int slot)
{
    const auto section = oscSection (slot);
    const auto letter = id::oscSlotLetter (slot) + " ";
    const auto pid = [slot] (const char* key) { return id::oscSlot (slot, key); };

    // Slot A defaults on; others off. Randomizer may toggle B/C but leaves at
    // least A biased strongly on so patches always sound.
    p.push_back ({ pid (id::osc::enable), letter + "Enable", section,
                   ParamKind::boolParam, {}, slot == 0 ? 1.0f : 0.0f, "",
                   false, { .enabled = true, .biasCentre = slot == 0 ? 1.0f : 0.6f,
                            .biasStrength = slot == 0 ? 1.0f : 0.3f } });
    p.push_back ({ pid (id::osc::position), letter + "Position", section,
                   ParamKind::floatParam, { 0.0f, 1.0f }, 0.0f, "",
                   true, { .enabled = true } });
    // Coarse never randomizes (like rootNote): semitone jumps break the key
    // the user is writing in. Fine still rolls — detune is character, not key.
    p.push_back ({ pid (id::osc::coarse), letter + "Coarse", section,
                   ParamKind::floatParam, { -24.0f, 24.0f, 1.0f }, 0.0f, "st",
                   true, { .enabled = false } });
    p.push_back ({ pid (id::osc::fine), letter + "Fine", section,
                   ParamKind::floatParam, { -100.0f, 100.0f, 1.0f }, 0.0f, "ct",
                   true, { .enabled = true, .biasCentre = 0.5f, .biasStrength = 0.9f } });
    p.push_back ({ pid (id::osc::level), letter + "Level", section,
                   ParamKind::floatParam, { -60.0f, 0.0f, 0.01f, 3.0f }, -6.0f, "dB",
                   true, { .enabled = true, .minNorm = 0.5f, .biasCentre = 0.8f,
                           .biasStrength = 0.5f } });
    p.push_back ({ pid (id::osc::pan), letter + "Pan", section,
                   ParamKind::floatParam, { -1.0f, 1.0f }, 0.0f, "",
                   true, { .enabled = true, .biasCentre = 0.5f, .biasStrength = 0.7f } });
    p.push_back ({ pid (id::osc::phase), letter + "Phase", section,
                   ParamKind::floatParam, { 0.0f, 1.0f }, 0.0f, "",
                   false, { .enabled = true, .biasCentre = 0.0f, .biasStrength = 0.8f } });
    p.push_back ({ pid (id::osc::phaseMode), letter + "Phase Mode", section,
                   ParamKind::choiceParam, {}, 0.0f, "",
                   false, { .enabled = true },
                   { "Reset", "Random", "Free" } });
    p.push_back ({ pid (id::osc::unisonCount), letter + "Unison", section,
                   ParamKind::intParam, { 1.0f, 8.0f, 1.0f }, 1.0f, "",
                   false, { .enabled = true, .maxNorm = 0.75f, .biasCentre = 0.1f,
                            .biasStrength = 0.5f } });
    p.push_back ({ pid (id::osc::unisonDetune), letter + "Uni Detune", section,
                   ParamKind::floatParam, { 0.0f, 100.0f, 0.0f, 0.5f }, 15.0f, "ct",
                   true, { .enabled = true, .maxNorm = 0.6f, .biasCentre = 0.2f,
                           .biasStrength = 0.4f } });
    p.push_back ({ pid (id::osc::unisonBlend), letter + "Uni Blend", section,
                   ParamKind::floatParam, { 0.0f, 1.0f }, 0.7f, "",
                   true, { .enabled = true } });
    p.push_back ({ pid (id::osc::unisonWidth), letter + "Uni Width", section,
                   ParamKind::floatParam, { 0.0f, 1.0f }, 0.8f, "",
                   true, { .enabled = true, .biasCentre = 0.7f, .biasStrength = 0.3f } });

    // --- Sample/SFX engine ---------------------------------------------------
    p.push_back ({ pid (id::osc::mode), letter + "Mode", section,
                   ParamKind::choiceParam, {}, 0.0f, "",
                   false, { .enabled = true, .biasCentre = 0.0f, .biasStrength = 0.5f },
                   { "Wavetable", "Sample", "Granular",
                     "Analog", "FM", "Noise", "Pluck" } });
    p.push_back ({ pid (id::osc::sampleStart), letter + "Smp Start", section,
                   ParamKind::floatParam, { 0.0f, 1.0f }, 0.0f, "",
                   false, { .enabled = true, .maxNorm = 0.7f, .biasCentre = 0.0f,
                            .biasStrength = 0.6f } });
    p.push_back ({ pid (id::osc::loop), letter + "Loop", section,
                   ParamKind::boolParam, {}, 1.0f, "", false, { .enabled = true } });
    p.push_back ({ pid (id::osc::loopStart), letter + "Loop Start", section,
                   ParamKind::floatParam, { 0.0f, 1.0f }, 0.0f, "",
                   false, { .enabled = true, .biasCentre = 0.1f, .biasStrength = 0.5f } });
    p.push_back ({ pid (id::osc::loopEnd), letter + "Loop End", section,
                   ParamKind::floatParam, { 0.0f, 1.0f }, 1.0f, "",
                   false, { .enabled = true, .biasCentre = 0.9f, .biasStrength = 0.5f } });
    p.push_back ({ pid (id::osc::keytrack), letter + "Keytrack", section,
                   ParamKind::boolParam, {}, 1.0f, "", false, { .enabled = true } });
    p.push_back ({ pid (id::osc::rootNote), letter + "Root Note", section,
                   ParamKind::intParam, { 0.0f, 127.0f, 1.0f }, 60.0f, "",
                   false, { .enabled = false } });
    p.push_back ({ pid (id::osc::grainSize), letter + "Grain Size", section,
                   ParamKind::floatParam, { 10.0f, 500.0f, 0.0f, 0.4f }, 80.0f, "ms",
                   true, { .enabled = true, .biasCentre = 0.3f, .biasStrength = 0.3f } });
    p.push_back ({ pid (id::osc::grainDensity), letter + "Grain Density", section,
                   ParamKind::floatParam, { 1.0f, 100.0f, 0.0f, 0.4f }, 20.0f, "/s",
                   true, { .enabled = true, .biasCentre = 0.4f, .biasStrength = 0.3f } });
    p.push_back ({ pid (id::osc::grainPos), letter + "Grain Pos", section,
                   ParamKind::floatParam, { 0.0f, 1.0f }, 0.0f, "",
                   true, { .enabled = true } });
    p.push_back ({ pid (id::osc::grainSpray), letter + "Grain Spray", section,
                   ParamKind::floatParam, { 0.0f, 1.0f }, 0.1f, "",
                   true, { .enabled = true, .maxNorm = 0.7f, .biasCentre = 0.2f,
                           .biasStrength = 0.3f } });
    p.push_back ({ pid (id::osc::grainPitch), letter + "Grain Pitch", section,
                   ParamKind::floatParam, { -24.0f, 24.0f, 1.0f }, 0.0f, "st",
                   false, { .enabled = true, .minNorm = 0.25f, .maxNorm = 0.75f,
                            .biasCentre = 0.5f, .biasStrength = 0.7f } });

    // --- Analog / FM / Noise / Pluck (non-destination controls) ---------
    p.push_back ({ pid (id::osc::analogShape), letter + "Analog Shape", section,
                   ParamKind::choiceParam, {}, 0.0f, "",
                   false, { .enabled = true },
                   { "Saw", "Square", "Pulse", "Triangle", "Sine" } });
    p.push_back ({ pid (id::osc::fmRatio), letter + "FM Ratio", section,
                   ParamKind::floatParam, { 0.5f, 8.0f, 0.5f }, 2.0f, "x",
                   false, { .enabled = true, .biasCentre = 0.25f, .biasStrength = 0.4f } });
    p.push_back ({ pid (id::osc::noiseColor), letter + "Noise Color", section,
                   ParamKind::choiceParam, {}, 0.0f, "",
                   false, { .enabled = true },
                   { "White", "Pink", "Brown" } });
}

// Modulatable engine params, appended AFTER all pre-existing destinations so
// the dense mod-dest index space (and every saved route) stays stable.
static void addEngineDestParams (std::vector<ParamDef>& p)
{
    for (int slot = 0; slot < numOscSlots; ++slot)
    {
        const auto section = oscSection (slot);
        const auto letter = id::oscSlotLetter (slot) + " ";
        const auto pid = [slot] (const char* key) { return id::oscSlot (slot, key); };

        p.push_back ({ pid (id::osc::pulseWidth), letter + "Pulse Width", section,
                       ParamKind::floatParam, { 0.05f, 0.95f }, 0.5f, "",
                       true, { .enabled = true, .biasCentre = 0.5f, .biasStrength = 0.4f } });
        p.push_back ({ pid (id::osc::fmIndex), letter + "FM Index", section,
                       ParamKind::floatParam, { 0.0f, 10.0f, 0.0f, 0.6f }, 2.0f, "",
                       true, { .enabled = true, .maxNorm = 0.8f, .biasCentre = 0.3f,
                               .biasStrength = 0.3f } });
        p.push_back ({ pid (id::osc::pluckDamp), letter + "Pluck Damp", section,
                       ParamKind::floatParam, { 0.0f, 1.0f }, 0.5f, "",
                       true, { .enabled = true } });
    }
}

static void addADSRParams (std::vector<ParamDef>& p, Section section, const juce::String& idPrefix)
{
    p.push_back ({ idPrefix + ".attack", "Attack", section,
                   ParamKind::floatParam, { 0.001f, 10.0f, 0.0f, 0.35f }, 0.005f, "s",
                   true, { .enabled = true, .maxNorm = 0.6f, .biasCentre = 0.15f,
                           .biasStrength = 0.6f } });
    p.push_back ({ idPrefix + ".decay", "Decay", section,
                   ParamKind::floatParam, { 0.001f, 10.0f, 0.0f, 0.35f }, 0.2f, "s",
                   true, { .enabled = true } });
    p.push_back ({ idPrefix + ".sustain", "Sustain", section,
                   ParamKind::floatParam, { 0.0f, 1.0f }, 0.8f, "",
                   true, { .enabled = true, .minNorm = 0.2f } });
    p.push_back ({ idPrefix + ".release", "Release", section,
                   ParamKind::floatParam, { 0.001f, 20.0f, 0.0f, 0.35f }, 0.15f, "s",
                   true, { .enabled = true, .maxNorm = 0.7f } });
}

static void addLFOParams (std::vector<ParamDef>& p, int lfoIndex)
{
    const auto section = lfoSection (lfoIndex);
    const auto prefix = "L" + juce::String (lfoIndex + 1) + " ";
    const auto pid = [lfoIndex] (const char* key) { return id::lfoParam (lfoIndex, key); };

    p.push_back ({ pid (id::lfo::shape), prefix + "Shape", section,
                   ParamKind::choiceParam, {}, 0.0f, "",
                   false, { .enabled = true },
                   { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "S&H" } });
    p.push_back ({ pid (id::lfo::rate), prefix + "Rate", section,
                   ParamKind::floatParam, frequencyRange (0.01f, 40.0f), 1.0f, "Hz",
                   false, { .enabled = true, .minNorm = 0.2f, .maxNorm = 0.8f } });
    p.push_back ({ pid (id::lfo::sync), prefix + "Sync", section,
                   ParamKind::boolParam, {}, 0.0f, "",
                   false, { .enabled = true } });
    p.push_back ({ pid (id::lfo::division), prefix + "Division", section,
                   ParamKind::choiceParam, {}, 6.0f /* 1/4 */, "",
                   false, { .enabled = true, .minNorm = 0.2f, .maxNorm = 0.9f },
                   lfoDivisionNames() });
    p.push_back ({ pid (id::lfo::phase), prefix + "Phase", section,
                   ParamKind::floatParam, { 0.0f, 1.0f }, 0.0f, "",
                   false, { .enabled = true, .biasCentre = 0.0f, .biasStrength = 0.8f } });
    p.push_back ({ pid (id::lfo::retrig), prefix + "Retrig", section,
                   ParamKind::boolParam, {}, 1.0f, "",
                   false, { .enabled = true } });
    p.push_back ({ pid (id::lfo::unipolar), prefix + "Unipolar", section,
                   ParamKind::boolParam, {}, 0.0f, "",
                   false, { .enabled = true } });
}

// Non-matrix parameter definitions, in registry order.
static std::vector<ParamDef> buildCoreDefs()
{
    std::vector<ParamDef> p;

    p.push_back ({ id::masterGain, "Master", Section::global,
                   ParamKind::floatParam, { -60.0f, 6.0f, 0.01f, 2.5f }, -6.0f, "dB",
                   false, { .enabled = false } });

    // Portamento: randomizing pitch-slide behaviour is disorienting, so
    // RANDOMIZE ALL leaves both alone.
    p.push_back ({ id::glideMode, "Glide Mode", Section::global,
                   ParamKind::choiceParam, {}, 0.0f, "",
                   false, { .enabled = false },
                   { "Off", "Always", "Legato" } });
    p.push_back ({ id::glideTime, "Glide Time", Section::global,
                   ParamKind::floatParam, { 1.0f, 2000.0f, 1.0f, 0.35f }, 80.0f, "ms",
                   false, { .enabled = false } });

    p.push_back ({ id::voiceMode, "Voice Mode", Section::global,
                   ParamKind::choiceParam, {}, 0.0f, "",
                   false, { .enabled = false },
                   { "Poly", "Mono", "Duo", "Paraphonic", "Unison" } });
    p.push_back ({ id::notePriority, "Note Priority", Section::global,
                   ParamKind::choiceParam, {}, 0.0f, "",
                   false, { .enabled = false },
                   { "Last", "High", "Low" } });
    p.push_back ({ id::unisonVoices, "Unison Voices", Section::global,
                   ParamKind::intParam, { 1.0f, 7.0f, 1.0f }, 3.0f, "",
                   false, { .enabled = false } });
    p.push_back ({ id::unisonDetune, "Unison Detune", Section::global,
                   ParamKind::floatParam, { 0.0f, 50.0f, 0.1f }, 12.0f, "ct",
                   false, { .enabled = true, .maxNorm = 0.6f } });
    p.push_back ({ id::unisonWidth, "Unison Width", Section::global,
                   ParamKind::floatParam, { 0.0f, 1.0f }, 0.5f, "",
                   false, { .enabled = true } });
    p.push_back ({ id::oversampling, "Oversampling", Section::global,
                   ParamKind::choiceParam, {}, 0.0f, "",
                   false, { .enabled = false },
                   { "Off", "2x", "4x", "8x" } });

    for (int slot = 0; slot < numOscSlots; ++slot)
        addOscSlotParams (p, slot);

    // Default on so existing presets/sessions (no stored value -> default)
    // keep filter 1 unconditionally in the signal path, matching pre-1.0.4
    // behaviour. Bias toward "on" during RANDOMIZE ALL for the same reason.
    p.push_back ({ id::filter1Enable, "On", Section::filter1,
                   ParamKind::boolParam, {}, 1.0f, "",
                   false, { .enabled = true, .biasCentre = 0.8f, .biasStrength = 0.5f } });
    p.push_back ({ id::filter1Type, "Type", Section::filter1,
                   ParamKind::choiceParam, {}, 0.0f, "",
                   false, { .enabled = true },
                   { "LP 12", "LP 24", "HP 12", "HP 24",
                     "BP 12", "BP 24", "Notch 12", "Notch 24" } });
    p.push_back ({ id::filter1Cutoff, "Cutoff", Section::filter1,
                   ParamKind::floatParam, frequencyRange (20.0f, 20000.0f), 20000.0f, "Hz",
                   true, { .enabled = true, .minNorm = 0.2f, .biasCentre = 0.6f,
                           .biasStrength = 0.3f } });
    p.push_back ({ id::filter1Resonance, "Resonance", Section::filter1,
                   ParamKind::floatParam, { 0.0f, 1.0f }, 0.0f, "",
                   true, { .enabled = true, .maxNorm = 0.85f, .biasCentre = 0.3f,
                           .biasStrength = 0.4f } });
    p.push_back ({ id::filter1Drive, "Drive", Section::filter1,
                   ParamKind::floatParam, { 0.0f, 1.0f }, 0.0f, "",
                   true, { .enabled = true, .maxNorm = 0.7f, .biasCentre = 0.2f,
                           .biasStrength = 0.5f } });
    p.push_back ({ id::filter1Keytrack, "Keytrack", Section::filter1,
                   ParamKind::floatParam, { 0.0f, 1.0f }, 0.0f, "",
                   false, { .enabled = true, .biasCentre = 0.3f, .biasStrength = 0.4f } });

    // --- Filter 2 (non-destination controls; dests are appended later) ---
    p.push_back ({ id::filter2Enable, "F2 On", Section::filter2,
                   ParamKind::boolParam, {}, 0.0f, "",
                   false, { .enabled = true, .biasCentre = 0.3f, .biasStrength = 0.4f } });
    p.push_back ({ id::filter2Type, "F2 Type", Section::filter2,
                   ParamKind::choiceParam, {}, 0.0f, "",
                   false, { .enabled = true },
                   { "LP 12", "LP 24", "HP 12", "HP 24",
                     "BP 12", "BP 24", "Notch 12", "Notch 24" } });
    p.push_back ({ id::filter2Keytrack, "F2 Keytrack", Section::filter2,
                   ParamKind::floatParam, { 0.0f, 1.0f }, 0.0f, "",
                   false, { .enabled = true, .biasCentre = 0.3f, .biasStrength = 0.4f } });
    p.push_back ({ id::filterRouting, "Routing", Section::filter2,
                   ParamKind::choiceParam, {}, 0.0f, "",
                   false, { .enabled = true }, { "Series", "Parallel" } });

    addADSRParams (p, Section::ampEnv, "ampEnv");
    addADSRParams (p, Section::env2, "env2");
    addADSRParams (p, Section::env3, "env3");

    for (int i = 0; i < numLFOs; ++i)
        addLFOParams (p, i);

    // Macros have no dedicated panel (the arp took it); they remain matrix
    // sources + host-automation targets but must NOT randomize — an invisible
    // parameter shifting under RANDOMIZE ALL is undiagnosable from the UI.
    for (int m = 0; m < numMacros; ++m)
        p.push_back ({ id::macro (m), "Macro " + juce::String (m + 1), Section::macros,
                       ParamKind::floatParam, { 0.0f, 1.0f }, 0.0f, "",
                       false, { .enabled = false } });

    // --- Arpeggiator ----------------------------------------------------
    namespace arpid = id::arp;
    juce::StringArray phraseNames;
    for (const auto& phrase : arpPhrases())
        phraseNames.add (phrase.name);

    p.push_back ({ arpid::enable, "Arp On", Section::arp,
                   ParamKind::boolParam, {}, 0.0f, "",
                   false, { .enabled = true, .biasCentre = 0.15f, .biasStrength = 0.6f } });
    p.push_back ({ arpid::mode, "Arp Mode", Section::arp,
                   ParamKind::choiceParam, {}, 0.0f, "",
                   false, { .enabled = true },
                   { "Up", "Down", "Up-Down", "Down-Up", "Up-Down Incl",
                     "Converge", "Diverge", "As Played", "Chord",
                     "Random", "Random Walk", "Phrase" } });
    p.push_back ({ arpid::division, "Arp Rate", Section::arp,
                   ParamKind::choiceParam, {}, 12.0f /* 1/16 */, "",
                   false, { .enabled = true, .minNorm = 0.4f, .maxNorm = 1.0f },
                   lfoDivisionNames() });
    p.push_back ({ arpid::octaves, "Arp Octaves", Section::arp,
                   ParamKind::intParam, { 1.0f, 4.0f, 1.0f }, 1.0f, "",
                   false, { .enabled = true, .maxNorm = 0.7f } });
    p.push_back ({ arpid::gate, "Arp Gate", Section::arp,
                   ParamKind::floatParam, { 0.05f, 1.0f }, 0.8f, "",
                   false, { .enabled = true, .minNorm = 0.3f } });
    p.push_back ({ arpid::swing, "Arp Swing", Section::arp,
                   ParamKind::floatParam, { 0.0f, 0.75f }, 0.0f, "",
                   false, { .enabled = true, .maxNorm = 0.6f, .biasCentre = 0.1f,
                            .biasStrength = 0.5f } });
    p.push_back ({ arpid::latch, "Arp Latch", Section::arp,
                   ParamKind::boolParam, {}, 0.0f, "",
                   false, { .enabled = false } });
    p.push_back ({ arpid::phrase, "Arp Phrase", Section::arp,
                   ParamKind::choiceParam, {}, 0.0f, "",
                   false, { .enabled = true }, phraseNames });
    p.push_back ({ arpid::velMode, "Arp Velocity", Section::arp,
                   ParamKind::choiceParam, {}, 0.0f, "",
                   false, { .enabled = true },
                   { "As Played", "Fixed", "Accent" } });
    p.push_back ({ arpid::chance, "Arp Chance", Section::arp,
                   ParamKind::floatParam, { 0.0f, 1.0f }, 1.0f, "",
                   false, { .enabled = true, .minNorm = 0.5f, .biasCentre = 0.9f,
                            .biasStrength = 0.5f } });
    p.push_back ({ arpid::stutter, "Arp Stutter", Section::arp,
                   ParamKind::floatParam, { 0.0f, 1.0f }, 0.0f, "",
                   false, { .enabled = true, .maxNorm = 0.6f, .biasCentre = 0.15f,
                            .biasStrength = 0.5f } });
    p.push_back ({ arpid::jump, "Arp Jump", Section::arp,
                   ParamKind::floatParam, { 0.0f, 1.0f }, 0.0f, "",
                   false, { .enabled = true, .maxNorm = 0.6f, .biasCentre = 0.15f,
                            .biasStrength = 0.5f } });
    p.push_back ({ arpid::humanize, "Arp Humanize", Section::arp,
                   ParamKind::floatParam, { 0.0f, 1.0f }, 0.0f, "",
                   false, { .enabled = true, .maxNorm = 0.7f, .biasCentre = 0.25f,
                            .biasStrength = 0.4f } });

    // --- Organic Chaos ------------------------------------------------------
    namespace ch = id::chaos;
    p.push_back ({ ch::enable, "Chaos Enable", Section::chaos,
                   ParamKind::boolParam, {}, 1.0f, "",
                   false, { .enabled = true, .biasCentre = 0.8f, .biasStrength = 0.4f } });
    p.push_back ({ ch::depth, "Depth", Section::chaos,
                   ParamKind::floatParam, { 0.0f, 1.0f }, 0.4f, "",
                   true, { .enabled = true, .biasCentre = 0.4f, .biasStrength = 0.3f } });
    p.push_back ({ ch::rate, "Rate", Section::chaos,
                   ParamKind::floatParam, frequencyRange (0.05f, 25.0f), 2.0f, "Hz",
                   true, { .enabled = true, .minNorm = 0.1f, .maxNorm = 0.8f } });
    p.push_back ({ ch::mix, "Mix", Section::chaos,
                   ParamKind::floatParam, { 0.0f, 1.0f }, 1.0f, "",
                   true, { .enabled = true, .minNorm = 0.3f, .biasCentre = 0.8f,
                           .biasStrength = 0.3f } });
    p.push_back ({ ch::pitchOn, "Pitch Drift", Section::chaos,
                   ParamKind::boolParam, {}, 1.0f, "", false, { .enabled = true } });
    p.push_back ({ ch::pitchAmount, "Pitch Amt", Section::chaos,
                   ParamKind::floatParam, { 0.0f, 100.0f, 0.0f, 0.4f }, 8.0f, "ct",
                   false, { .enabled = true, .maxNorm = 0.5f, .biasCentre = 0.15f,
                            .biasStrength = 0.5f } });
    p.push_back ({ ch::phaseOn, "Phase Drift", Section::chaos,
                   ParamKind::boolParam, {}, 1.0f, "", false, { .enabled = true } });
    p.push_back ({ ch::phaseAmount, "Phase Amt", Section::chaos,
                   ParamKind::floatParam, { 0.0f, 1.0f }, 0.15f, "",
                   false, { .enabled = true, .biasCentre = 0.2f, .biasStrength = 0.4f } });
    p.push_back ({ ch::positionOn, "Position Drift", Section::chaos,
                   ParamKind::boolParam, {}, 1.0f, "", false, { .enabled = true } });
    p.push_back ({ ch::positionAmount, "Position Amt", Section::chaos,
                   ParamKind::floatParam, { 0.0f, 1.0f }, 0.2f, "",
                   false, { .enabled = true, .biasCentre = 0.25f, .biasStrength = 0.3f } });
    p.push_back ({ ch::ampOn, "Amp Drift", Section::chaos,
                   ParamKind::boolParam, {}, 1.0f, "", false, { .enabled = true } });
    p.push_back ({ ch::ampAmount, "Amp Amt", Section::chaos,
                   ParamKind::floatParam, { 0.0f, 1.0f }, 0.2f, "",
                   false, { .enabled = true, .biasCentre = 0.2f, .biasStrength = 0.4f } });
    p.push_back ({ ch::satOn, "Sat Drift", Section::chaos,
                   ParamKind::boolParam, {}, 0.0f, "", false, { .enabled = true } });
    p.push_back ({ ch::saturation, "Saturation", Section::chaos,
                   ParamKind::floatParam, { 0.0f, 1.0f }, 0.3f, "",
                   false, { .enabled = true, .maxNorm = 0.8f, .biasCentre = 0.3f,
                            .biasStrength = 0.3f } });
    p.push_back ({ ch::distOn, "Dist Drift", Section::chaos,
                   ParamKind::boolParam, {}, 0.0f, "", false, { .enabled = true } });
    p.push_back ({ ch::distortion, "Distortion", Section::chaos,
                   ParamKind::floatParam, { 0.0f, 1.0f }, 0.2f, "",
                   false, { .enabled = true, .maxNorm = 0.7f, .biasCentre = 0.2f,
                            .biasStrength = 0.4f } });

    addEngineDestParams (p);   // appended: keeps existing dest indices stable

    // Filter convenience mods (also appended for dest-index stability).
    p.push_back ({ id::filter1EnvAmount, "Env Amt", Section::filter1,
                   ParamKind::floatParam, { -1.0f, 1.0f }, 0.0f, "",
                   true, { .enabled = true, .biasCentre = 0.5f, .biasStrength = 0.35f } });
    p.push_back ({ id::filter1Mix, "Mix", Section::filter1,
                   ParamKind::floatParam, { 0.0f, 1.0f }, 1.0f, "",
                   true, { .enabled = true, .minNorm = 0.5f, .biasCentre = 0.95f,
                           .biasStrength = 0.5f } });

    p.push_back ({ id::filter2Cutoff, "F2 Cutoff", Section::filter2,
                   ParamKind::floatParam, frequencyRange (20.0f, 20000.0f), 20000.0f, "Hz",
                   true, { .enabled = true, .minNorm = 0.2f, .biasCentre = 0.6f,
                           .biasStrength = 0.3f } });
    p.push_back ({ id::filter2Resonance, "F2 Resonance", Section::filter2,
                   ParamKind::floatParam, { 0.0f, 1.0f }, 0.0f, "",
                   true, { .enabled = true, .maxNorm = 0.85f, .biasCentre = 0.3f,
                           .biasStrength = 0.4f } });
    p.push_back ({ id::filter2Drive, "F2 Drive", Section::filter2,
                   ParamKind::floatParam, { 0.0f, 1.0f }, 0.0f, "",
                   true, { .enabled = true, .maxNorm = 0.7f, .biasCentre = 0.2f,
                           .biasStrength = 0.5f } });
    p.push_back ({ id::filter2EnvAmount, "F2 Env Amt", Section::filter2,
                   ParamKind::floatParam, { -1.0f, 1.0f }, 0.0f, "",
                   true, { .enabled = true, .biasCentre = 0.5f, .biasStrength = 0.35f } });
    p.push_back ({ id::filter2Mix, "F2 Mix", Section::filter2,
                   ParamKind::floatParam, { 0.0f, 1.0f }, 1.0f, "",
                   true, { .enabled = true, .minNorm = 0.5f, .biasCentre = 0.95f,
                           .biasStrength = 0.5f } });

    // --- FX chain -------------------------------------------------------
    namespace fx = id::fx;

    p.push_back ({ fx::distEnable, "Dist On", Section::fxDist,
                   ParamKind::boolParam, {}, 0.0f, "",
                   false, { .enabled = true, .biasCentre = 0.3f, .biasStrength = 0.3f } });
    p.push_back ({ fx::distType, "Dist Type", Section::fxDist,
                   ParamKind::choiceParam, {}, 0.0f, "", false, { .enabled = true },
                   { "Soft", "Hard", "Fold", "Crush" } });
    p.push_back ({ fx::distDrive, "Dist Drive", Section::fxDist,
                   ParamKind::floatParam, { 0.0f, 1.0f }, 0.3f, "",
                   false, { .enabled = true, .maxNorm = 0.8f, .biasCentre = 0.3f,
                            .biasStrength = 0.3f } });
    p.push_back ({ fx::distTone, "Dist Tone", Section::fxDist,
                   ParamKind::floatParam, frequencyRange (500.0f, 20000.0f), 8000.0f, "Hz",
                   false, { .enabled = true, .minNorm = 0.3f } });
    p.push_back ({ fx::distMix, "Dist Mix", Section::fxDist,
                   ParamKind::floatParam, { 0.0f, 1.0f }, 1.0f, "",
                   false, { .enabled = true, .minNorm = 0.3f } });

    p.push_back ({ fx::chorusEnable, "Chorus On", Section::fxChorus,
                   ParamKind::boolParam, {}, 0.0f, "",
                   false, { .enabled = true, .biasCentre = 0.4f, .biasStrength = 0.3f } });
    p.push_back ({ fx::chorusRate, "Chorus Rate", Section::fxChorus,
                   ParamKind::floatParam, frequencyRange (0.05f, 5.0f), 0.8f, "Hz",
                   false, { .enabled = true, .maxNorm = 0.7f } });
    p.push_back ({ fx::chorusDepth, "Chorus Depth", Section::fxChorus,
                   ParamKind::floatParam, { 0.0f, 1.0f }, 0.3f, "",
                   false, { .enabled = true, .biasCentre = 0.35f, .biasStrength = 0.3f } });
    p.push_back ({ fx::chorusFeedback, "Chorus FB", Section::fxChorus,
                   ParamKind::floatParam, { -0.9f, 0.9f }, 0.0f, "",
                   false, { .enabled = true, .biasCentre = 0.5f, .biasStrength = 0.6f } });
    p.push_back ({ fx::chorusMix, "Chorus Mix", Section::fxChorus,
                   ParamKind::floatParam, { 0.0f, 1.0f }, 0.5f, "",
                   false, { .enabled = true, .biasCentre = 0.5f, .biasStrength = 0.3f } });

    p.push_back ({ fx::delayEnable, "Delay On", Section::fxDelay,
                   ParamKind::boolParam, {}, 0.0f, "",
                   false, { .enabled = true, .biasCentre = 0.4f, .biasStrength = 0.3f } });
    p.push_back ({ fx::delaySync, "Delay Sync", Section::fxDelay,
                   ParamKind::boolParam, {}, 1.0f, "",
                   false, { .enabled = true, .biasCentre = 0.8f, .biasStrength = 0.5f } });
    p.push_back ({ fx::delayTime, "Delay Time", Section::fxDelay,
                   ParamKind::floatParam, { 1.0f, 2000.0f, 0.0f, 0.4f }, 350.0f, "ms",
                   false, { .enabled = true, .minNorm = 0.2f, .maxNorm = 0.8f } });
    p.push_back ({ fx::delayDivision, "Delay Div", Section::fxDelay,
                   ParamKind::choiceParam, {}, 6.0f /* 1/4 */, "",
                   false, { .enabled = true, .minNorm = 0.3f, .maxNorm = 0.9f },
                   lfoDivisionNames() });
    p.push_back ({ fx::delayFeedback, "Delay FB", Section::fxDelay,
                   ParamKind::floatParam, { 0.0f, 0.95f }, 0.35f, "",
                   false, { .enabled = true, .maxNorm = 0.75f, .biasCentre = 0.4f,
                            .biasStrength = 0.3f } });
    p.push_back ({ fx::delayPingPong, "Ping Pong", Section::fxDelay,
                   ParamKind::boolParam, {}, 0.0f, "", false, { .enabled = true } });
    p.push_back ({ fx::delayMix, "Delay Mix", Section::fxDelay,
                   ParamKind::floatParam, { 0.0f, 1.0f }, 0.35f, "",
                   false, { .enabled = true, .maxNorm = 0.8f, .biasCentre = 0.35f,
                            .biasStrength = 0.3f } });

    p.push_back ({ fx::reverbEnable, "Reverb On", Section::fxReverb,
                   ParamKind::boolParam, {}, 0.0f, "",
                   false, { .enabled = true, .biasCentre = 0.6f, .biasStrength = 0.3f } });
    p.push_back ({ fx::reverbMode, "Reverb Mode", Section::fxReverb,
                   ParamKind::choiceParam, {}, 0.0f, "",
                   false, { .enabled = true },
                   juce::StringArray { "Hall", "Plate", "Chamber", "Room", "Spring" } });
    p.push_back ({ fx::reverbPreDelay, "Reverb Pre", Section::fxReverb,
                   ParamKind::floatParam, { 0.0f, 200.0f }, 20.0f, "ms",
                   false, { .enabled = true, .maxNorm = 0.4f } });
    p.push_back ({ fx::reverbSize, "Reverb Size", Section::fxReverb,
                   ParamKind::floatParam, { 0.0f, 1.0f }, 0.5f, "",
                   false, { .enabled = true } });
    // Top end pulled in from 12s: at max Decay + Hall's decay multiplier the
    // old range produced an effective RT60 approaching 17s, which read as
    // uncontrolled runaway feedback rather than a long tail (tester report).
    p.push_back ({ fx::reverbDecay, "Reverb Decay", Section::fxReverb,
                   ParamKind::floatParam, skewedRange (0.2f, 8.0f, 2.5f), 2.0f, "s",
                   false, { .enabled = true, .maxNorm = 0.6f } });
    p.push_back ({ fx::reverbDamping, "Reverb Damp", Section::fxReverb,
                   ParamKind::floatParam, { 0.0f, 1.0f }, 0.5f, "",
                   false, { .enabled = true } });
    p.push_back ({ fx::reverbModDepth, "Reverb Mod", Section::fxReverb,
                   ParamKind::floatParam, { 0.0f, 1.0f }, 0.2f, "",
                   false, { .enabled = true, .maxNorm = 0.6f } });
    p.push_back ({ fx::reverbLowCut, "Reverb LoCut", Section::fxReverb,
                   ParamKind::floatParam, frequencyRange (20.0f, 2000.0f), 20.0f, "Hz",
                   false, { .enabled = true, .maxNorm = 0.5f } });
    p.push_back ({ fx::reverbHighCut, "Reverb HiCut", Section::fxReverb,
                   ParamKind::floatParam, frequencyRange (1000.0f, 20000.0f), 12000.0f, "Hz",
                   false, { .enabled = true, .minNorm = 0.4f } });
    p.push_back ({ fx::reverbWidth, "Reverb Width", Section::fxReverb,
                   ParamKind::floatParam, { 0.0f, 1.0f }, 1.0f, "",
                   false, { .enabled = true, .minNorm = 0.4f } });
    p.push_back ({ fx::reverbMix, "Reverb Mix", Section::fxReverb,
                   ParamKind::floatParam, { 0.0f, 1.0f }, 0.3f, "",
                   false, { .enabled = true, .maxNorm = 0.8f, .biasCentre = 0.3f,
                            .biasStrength = 0.3f } });

    p.push_back ({ fx::eqEnable, "EQ On", Section::fxEQ,
                   ParamKind::boolParam, {}, 0.0f, "",
                   false, { .enabled = true, .biasCentre = 0.3f, .biasStrength = 0.4f } });
    p.push_back ({ fx::eqCharacter, "EQ Character", Section::fxEQ,
                   ParamKind::choiceParam, {}, 0.0f, "",
                   false, { .enabled = true },
                   juce::StringArray { "Clean", "Modern", "Vintage", "Tube" } });

    // 8 parametric bands. Disabled by default (flat); default freqs spread log-
    // wide with shelf types at the ends, so enabling a band drops a sensible node.
    {
        struct BandDef { float freq; int type; };
        const BandDef defs[8] = {
            {    80.0f, 1 /* Low Shelf */ }, {   200.0f, 0 }, {  500.0f, 0 },
            {  1200.0f, 0 }, {  3000.0f, 0 }, {  6000.0f, 0 },
            { 10000.0f, 2 /* High Shelf */ }, { 15000.0f, 0 } };

        for (int b = 0; b < 8; ++b)
        {
            const auto bn = "EQ B" + juce::String (b + 1) + " ";
            p.push_back ({ id::eqBand (b, fx::eqband::enable), bn + "On", Section::fxEQ,
                           ParamKind::boolParam, {}, 0.0f, "",
                           false, { .enabled = true, .biasCentre = 0.2f, .biasStrength = 0.5f } });
            p.push_back ({ id::eqBand (b, fx::eqband::type), bn + "Type", Section::fxEQ,
                           ParamKind::choiceParam, {}, (float) defs[b].type, "",
                           false, { .enabled = false },
                           juce::StringArray { "Bell", "Low Shelf", "High Shelf",
                                               "Low Cut", "High Cut", "Notch" } });
            p.push_back ({ id::eqBand (b, fx::eqband::freq), bn + "Freq", Section::fxEQ,
                           ParamKind::floatParam, frequencyRange (20.0f, 20000.0f),
                           defs[b].freq, "Hz", false, { .enabled = true } });
            p.push_back ({ id::eqBand (b, fx::eqband::gain), bn + "Gain", Section::fxEQ,
                           ParamKind::floatParam, { -24.0f, 24.0f, 0.1f }, 0.0f, "dB",
                           false, { .enabled = true, .biasCentre = 0.5f, .biasStrength = 0.6f } });
            p.push_back ({ id::eqBand (b, fx::eqband::q), bn + "Q", Section::fxEQ,
                           ParamKind::floatParam, skewedRange (0.1f, 18.0f, 1.0f), 0.707f, "",
                           false, { .enabled = true, .biasCentre = 0.3f } });
        }
    }

    // FX Mod (Phaser / Flanger, switchable).
    p.push_back ({ fx::modEnable, "Mod On", Section::fxMod,
                   ParamKind::boolParam, {}, 0.0f, "",
                   false, { .enabled = true, .biasCentre = 0.4f, .biasStrength = 0.3f } });
    p.push_back ({ fx::modType, "Mod Type", Section::fxMod,
                   ParamKind::choiceParam, {}, 0.0f, "",
                   false, { .enabled = false },
                   juce::StringArray { "Phaser", "Flanger" } });
    p.push_back ({ fx::modRate, "Mod Rate", Section::fxMod,
                   ParamKind::floatParam, frequencyRange (0.02f, 8.0f), 0.5f, "Hz",
                   false, { .enabled = true, .maxNorm = 0.6f } });
    p.push_back ({ fx::modSync, "Mod Sync", Section::fxMod,
                   ParamKind::boolParam, {}, 0.0f, "", false, { .enabled = true } });
    p.push_back ({ fx::modDivision, "Mod Div", Section::fxMod,
                   ParamKind::choiceParam, {}, 6.0f, "",
                   false, { .enabled = false }, lfoDivisionNames() });
    p.push_back ({ fx::modDepth, "Mod Depth", Section::fxMod,
                   ParamKind::floatParam, { 0.0f, 1.0f }, 0.5f, "",
                   false, { .enabled = true, .biasCentre = 0.5f, .biasStrength = 0.3f } });
    p.push_back ({ fx::modFeedback, "Mod FB", Section::fxMod,
                   ParamKind::floatParam, { -0.95f, 0.95f }, 0.3f, "",
                   false, { .enabled = true, .biasCentre = 0.5f, .biasStrength = 0.5f } });
    p.push_back ({ fx::modStages, "Mod Stages", Section::fxMod,
                   ParamKind::choiceParam, {}, 2.0f, "",
                   false, { .enabled = false },
                   juce::StringArray { "2", "4", "6", "8", "12" } });
    p.push_back ({ fx::modCentre, "Mod Centre", Section::fxMod,
                   ParamKind::floatParam, frequencyRange (100.0f, 6000.0f), 800.0f, "Hz",
                   false, { .enabled = true } });
    p.push_back ({ fx::modManual, "Mod Delay", Section::fxMod,
                   ParamKind::floatParam, { 0.1f, 20.0f, 0.01f }, 3.0f, "ms",
                   false, { .enabled = true } });
    p.push_back ({ fx::modWidth, "Mod Width", Section::fxMod,
                   ParamKind::floatParam, { 0.0f, 1.0f }, 0.5f, "",
                   false, { .enabled = true } });
    p.push_back ({ fx::modMix, "Mod Mix", Section::fxMod,
                   ParamKind::floatParam, { 0.0f, 1.0f }, 0.5f, "",
                   false, { .enabled = true, .biasCentre = 0.5f, .biasStrength = 0.3f } });

    // FX Tremolo / Vibrato (independent sections in one tab).
    p.push_back ({ fx::tremEnable, "Trem On", Section::fxTremVib,
                   ParamKind::boolParam, {}, 0.0f, "", false, { .enabled = true } });
    p.push_back ({ fx::tremRate, "Trem Rate", Section::fxTremVib,
                   ParamKind::floatParam, frequencyRange (0.05f, 20.0f), 5.0f, "Hz",
                   false, { .enabled = true, .maxNorm = 0.6f } });
    p.push_back ({ fx::tremSync, "Trem Sync", Section::fxTremVib,
                   ParamKind::boolParam, {}, 0.0f, "", false, { .enabled = true } });
    p.push_back ({ fx::tremDivision, "Trem Div", Section::fxTremVib,
                   ParamKind::choiceParam, {}, 6.0f, "",
                   false, { .enabled = false }, lfoDivisionNames() });
    p.push_back ({ fx::tremDepth, "Trem Depth", Section::fxTremVib,
                   ParamKind::floatParam, { 0.0f, 1.0f }, 0.5f, "",
                   false, { .enabled = true } });
    p.push_back ({ fx::tremShape, "Trem Shape", Section::fxTremVib,
                   ParamKind::choiceParam, {}, 0.0f, "",
                   false, { .enabled = false },
                   juce::StringArray { "Sine", "Triangle", "Square", "Saw" } });
    p.push_back ({ fx::tremStereo, "Trem Stereo", Section::fxTremVib,
                   ParamKind::floatParam, { 0.0f, 1.0f }, 0.0f, "",
                   false, { .enabled = true } });
    p.push_back ({ fx::tremMix, "Trem Mix", Section::fxTremVib,
                   ParamKind::floatParam, { 0.0f, 1.0f }, 1.0f, "",
                   false, { .enabled = true } });
    p.push_back ({ fx::vibEnable, "Vib On", Section::fxTremVib,
                   ParamKind::boolParam, {}, 0.0f, "", false, { .enabled = true } });
    p.push_back ({ fx::vibRate, "Vib Rate", Section::fxTremVib,
                   ParamKind::floatParam, frequencyRange (0.05f, 14.0f), 5.0f, "Hz",
                   false, { .enabled = true, .maxNorm = 0.6f } });
    p.push_back ({ fx::vibSync, "Vib Sync", Section::fxTremVib,
                   ParamKind::boolParam, {}, 0.0f, "", false, { .enabled = true } });
    p.push_back ({ fx::vibDivision, "Vib Div", Section::fxTremVib,
                   ParamKind::choiceParam, {}, 6.0f, "",
                   false, { .enabled = false }, lfoDivisionNames() });
    p.push_back ({ fx::vibDepth, "Vib Depth", Section::fxTremVib,
                   ParamKind::floatParam, { 0.0f, 1.0f }, 0.4f, "",
                   false, { .enabled = true } });
    p.push_back ({ fx::vibMix, "Vib Mix", Section::fxTremVib,
                   ParamKind::floatParam, { 0.0f, 1.0f }, 1.0f, "",
                   false, { .enabled = true } });

    // FX Limiter / Maximizer (defaults last in the chain).
    p.push_back ({ fx::limEnable, "Lim On", Section::fxLimiter,
                   ParamKind::boolParam, {}, 0.0f, "", false, { .enabled = true } });
    p.push_back ({ fx::limDrive, "Lim Drive", Section::fxLimiter,
                   ParamKind::floatParam, { 0.0f, 24.0f, 0.1f }, 0.0f, "dB",
                   false, { .enabled = true, .maxNorm = 0.5f } });
    p.push_back ({ fx::limCeiling, "Lim Ceiling", Section::fxLimiter,
                   ParamKind::floatParam, { -12.0f, 0.0f, 0.1f }, -0.3f, "dB",
                   false, { .enabled = false } });
    p.push_back ({ fx::limRelease, "Lim Release", Section::fxLimiter,
                   ParamKind::floatParam, { 1.0f, 1000.0f, 0.0f, 0.4f }, 120.0f, "ms",
                   false, { .enabled = true } });
    p.push_back ({ fx::limAutoRelease, "Lim Auto Rel", Section::fxLimiter,
                   ParamKind::boolParam, {}, 0.0f, "", false, { .enabled = true } });
    p.push_back ({ fx::limCharacter, "Lim Character", Section::fxLimiter,
                   ParamKind::choiceParam, {}, 0.0f, "",
                   false, { .enabled = false },
                   juce::StringArray { "Clean", "Punchy", "Aggressive" } });
    p.push_back ({ fx::limStereoLink, "Lim Link", Section::fxLimiter,
                   ParamKind::floatParam, { 0.0f, 1.0f }, 1.0f, "",
                   false, { .enabled = false } });
    p.push_back ({ fx::limTruePeak, "Lim True Peak", Section::fxLimiter,
                   ParamKind::boolParam, {}, 0.0f, "", false, { .enabled = false } });
    p.push_back ({ fx::limLookahead, "Lim Lookahead", Section::fxLimiter,
                   ParamKind::boolParam, {}, 0.0f, "", false, { .enabled = false } });
    p.push_back ({ fx::limAutoGain, "Lim Auto Gain", Section::fxLimiter,
                   ParamKind::boolParam, {}, 0.0f, "", false, { .enabled = false } });

    // FX Convolve (SFX / user WAV as impulse; IR path stored in the state tree).
    p.push_back ({ fx::convEnable, "Conv On", Section::fxConvolve,
                   ParamKind::boolParam, {}, 0.0f, "", false, { .enabled = false } });
    p.push_back ({ fx::convMix, "Conv Mix", Section::fxConvolve,
                   ParamKind::floatParam, { 0.0f, 1.0f }, 0.3f, "",
                   false, { .enabled = true, .maxNorm = 0.6f } });
    p.push_back ({ fx::convWidth, "Conv Width", Section::fxConvolve,
                   ParamKind::floatParam, { 0.0f, 1.0f }, 1.0f, "",
                   false, { .enabled = false } });
    p.push_back ({ fx::convPreDelay, "Conv Pre", Section::fxConvolve,
                   ParamKind::floatParam, { 0.0f, 200.0f }, 0.0f, "ms",
                   false, { .enabled = true, .maxNorm = 0.4f } });
    p.push_back ({ fx::convDecay, "Conv Decay", Section::fxConvolve,
                   ParamKind::floatParam, { 0.05f, 1.0f }, 1.0f, "",
                   false, { .enabled = true, .minNorm = 0.3f } });
    p.push_back ({ fx::convDamping, "Conv Damp", Section::fxConvolve,
                   ParamKind::floatParam, { 0.0f, 1.0f }, 0.0f, "",
                   false, { .enabled = true, .maxNorm = 0.6f } });

    return p;
}

const std::vector<ArpPhrase>& arpPhrases()
{
    static const std::vector<ArpPhrase> phrases = {
        { "Root Pulse",     4, { 0, 0, 12, 0 } },
        { "Octaves",        2, { 0, 12 } },
        { "Fifths",         4, { 0, 7, 12, 7 } },
        { "Major Arp",      4, { 0, 4, 7, 12 } },
        { "Minor Arp",      4, { 0, 3, 7, 12 } },
        { "Sus4 Climb",     4, { 0, 5, 7, 12 } },
        { "Minor Run",      8, { 0, 3, 5, 7, 10, 7, 5, 3 } },
        { "Pentatonic",     8, { 0, 3, 5, 7, 10, 12, 10, 7 } },
        { "Octave Bounce",  6, { 0, 12, 24, 12, 0, -12 } },
        { "Stairway",       8, { 0, 7, 3, 10, 5, 12, 7, 15 } },
        { "Trance Gate",    8, { 0, 0, 12, 0, 0, 12, 0, 12 } },
        { "Ripple",        12, { 0, 4, 7, 12, 16, 19, 24, 19, 16, 12, 7, 4 } },
    };
    return phrases;
}

const std::vector<ParamDef>& all()
{
    static const std::vector<ParamDef> defs = []
    {
        auto p = buildCoreDefs();

        // Matrix routes are appended after everything else so the destination
        // choice list can be generated from the defs above.
        juce::StringArray destNames { "None" };
        for (const auto& def : p)
            if (def.modDestination)
                destNames.add (destDisplayName (def));

        for (int r = 0; r < numModRoutes; ++r)
        {
            const auto label = "R" + juce::String (r + 1) + " ";
            p.push_back ({ id::routeParam (r, id::route::source), label + "Source",
                           Section::matrix, ParamKind::choiceParam, {}, 0.0f, "",
                           false, { .enabled = true }, modSourceNames() });
            p.push_back ({ id::routeParam (r, id::route::dest), label + "Dest",
                           Section::matrix, ParamKind::choiceParam, {}, 0.0f, "",
                           false, { .enabled = true }, destNames });
            p.push_back ({ id::routeParam (r, id::route::depth), label + "Depth",
                           Section::matrix, ParamKind::floatParam,
                           { -1.0f, 1.0f }, 0.0f, "",
                           false, { .enabled = true, .biasCentre = 0.5f,
                                    .biasStrength = 0.4f } });
        }

        return p;
    }();

    return defs;
}

const std::vector<ModDest>& modDestinations()
{
    static const std::vector<ModDest> dests = []
    {
        std::vector<ModDest> d;
        int index = 0;
        for (const auto& def : all())
            if (def.modDestination)
                d.push_back ({ &def, index++ });
        return d;
    }();

    return dests;
}

int numModDests()
{
    return (int) modDestinations().size();
}

int modDestIndex (const juce::String& paramID)
{
    for (const auto& dest : modDestinations())
        if (dest.def->id == paramID)
            return dest.index;

    return -1;
}

const ParamDef* find (const juce::String& paramID)
{
    for (const auto& def : all())
        if (paramID == def.id)
            return &def;

    return nullptr;
}

static std::unique_ptr<juce::RangedAudioParameter> makeParameter (const ParamDef& def)
{
    const juce::ParameterID pid { def.id, 1 };

    switch (def.kind)
    {
        case ParamKind::boolParam:
            return std::make_unique<juce::AudioParameterBool> (pid, def.name,
                                                               def.defaultValue >= 0.5f);
        case ParamKind::intParam:
            return std::make_unique<juce::AudioParameterInt> (pid, def.name,
                                                              (int) def.range.start,
                                                              (int) def.range.end,
                                                              (int) def.defaultValue);
        case ParamKind::choiceParam:
            return std::make_unique<juce::AudioParameterChoice> (pid, def.name, def.choices,
                                                                 (int) def.defaultValue);
        case ParamKind::floatParam:
            break;
    }

    // Displayed values never need more than 2 decimals (and big values like
    // cutoff frequencies need fewer) — raw float noise like 0.6434523 is
    // useless to read, in the plugin UI and host automation lanes alike.
    const auto formatValue = [] (float value, int)
    {
        const auto magnitude = std::abs (value);
        return juce::String (value, magnitude >= 1000.0f ? 0
                                  : magnitude >= 100.0f ? 1 : 2);
    };

    return std::make_unique<juce::AudioParameterFloat> (
        pid, def.name, def.range, def.defaultValue,
        juce::AudioParameterFloatAttributes().withLabel (def.unit)
            .withStringFromValueFunction (formatValue));
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (auto section : allSections)
    {
        auto group = std::make_unique<juce::AudioProcessorParameterGroup> (
            sectionName (section), sectionName (section), "|");

        for (const auto& def : all())
            if (def.section == section)
                group->addChild (makeParameter (def));

        layout.add (std::move (group));
    }

    return layout;
}

} // namespace spa::params
