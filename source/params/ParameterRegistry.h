#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

namespace spa::params
{

// Voice architecture is sized for 4 oscillator slots; v1 exposes 3.
inline constexpr int numOscSlots = 3;
inline constexpr int maxOscSlots = 4;

inline constexpr int numLFOs = 3;
inline constexpr int numMacros = 4;
inline constexpr int numModRoutes = 16;

// Sections drive UI grouping, host parameter groups, and the per-section lock
// toggles used by RANDOMIZE ALL. Keep in sync with sectionName().
enum class Section
{
    global,
    oscA,
    oscB,
    oscC,
    filter1,
    filter2,
    ampEnv,
    env2,
    env3,
    lfo1,
    lfo2,
    lfo3,
    macros,
    arp,
    chaos,
    fxDist,
    fxChorus,
    fxDelay,
    fxReverb,
    fxEQ,
    matrix,
    fxMod,
    fxTremVib,
    fxLimiter,
    fxConvolve,
};

inline constexpr Section allSections[] = {
    Section::global, Section::oscA, Section::oscB, Section::oscC,
    Section::filter1, Section::filter2, Section::ampEnv, Section::env2, Section::env3,
    Section::lfo1, Section::lfo2, Section::lfo3, Section::macros,
    Section::arp, Section::chaos, Section::fxDist, Section::fxChorus,
    Section::fxDelay, Section::fxReverb, Section::fxEQ, Section::fxMod,
    Section::fxTremVib, Section::fxLimiter, Section::fxConvolve, Section::matrix,
};

juce::String sectionName (Section);
Section oscSection (int slotIndex);
Section lfoSection (int lfoIndex);

// Modulation sources. The matrix source choice parameters use exactly this
// order, so it is APPEND-ONLY: new sources (Chaos, SFX followers) go at the
// end or saved projects break.
enum class ModSource
{
    none,
    env1,       // amp envelope
    env2,
    env3,
    lfo1,
    lfo2,
    lfo3,
    macro1,
    macro2,
    macro3,
    macro4,
    velocity,
    modWheel,
    aftertouch,
    chaos,
    sfxAmpA,     // SFX followers: amp + pitch per slot, interleaved
    sfxPitchA,
    sfxAmpB,
    sfxPitchB,
    sfxAmpC,
    sfxPitchC,
    count,
};

// First SFX follower source; slot s amp = sfxFollowerBase + 2*s, pitch = +1.
inline constexpr int sfxFollowerBase = (int) ModSource::sfxAmpA;

inline constexpr int numModSources = (int) ModSource::count;

const juce::StringArray& modSourceNames();

// Randomization metadata consumed by RANDOMIZE ALL. Ranges are in normalized
// (0..1) parameter space. biasStrength 0 = uniform across [minNorm, maxNorm];
// 1 = tightly clustered around biasCentre.
struct RandomSpec
{
    bool enabled = true;
    float minNorm = 0.0f;
    float maxNorm = 1.0f;
    float biasCentre = 0.5f;
    float biasStrength = 0.0f;
};

enum class ParamKind { floatParam, intParam, boolParam, choiceParam };

struct ParamDef
{
    juce::String id;
    juce::String name;
    Section section;
    ParamKind kind = ParamKind::floatParam;
    juce::NormalisableRange<float> range;    // floatParam only
    float defaultValue = 0.0f;               // also holds int/bool/choice defaults
    juce::String unit;
    bool modDestination = false;             // can appear as a mod matrix destination
    RandomSpec random {};
    juce::StringArray choices {};            // choiceParam only
};

// A parameter that can be a modulation destination, with its dense index into
// the per-voice modulation arrays. Index order == registry order (stable).
struct ModDest
{
    const ParamDef* def;
    int index;
};

const std::vector<ModDest>& modDestinations();
int numModDests();

// Fixed capacity for per-voice modulation arrays (no allocation on the audio
// thread). numModDests() is asserted against this at startup.
inline constexpr int maxModDests = 96;

// Dense mod-dest index for a parameter ID, or -1 if it is not a destination.
int modDestIndex (const juce::String& paramID);

// Human-readable destination name used in matrix choice parameters.
juce::String destDisplayName (const ParamDef&);

// Stable parameter IDs. Everything (DSP, UI, mod matrix, randomizer) refers to
// parameters through these, never through ad hoc string literals.
namespace id
{
    inline constexpr const char* masterGain = "global.masterGain";

    // Portamento (global, applied per voice from the last struck note).
    inline constexpr const char* glideMode = "global.glideMode";   // Off/Always/Legato
    inline constexpr const char* glideTime = "global.glideTime";

    // Voice allocation.
    inline constexpr const char* voiceMode     = "global.voiceMode";
    inline constexpr const char* notePriority  = "global.notePriority";
    inline constexpr const char* unisonVoices  = "global.unisonVoices";
    inline constexpr const char* unisonDetune  = "global.unisonDetune";
    inline constexpr const char* unisonWidth   = "global.unisonWidth";
    inline constexpr const char* oversampling  = "global.oversampling";  // Off/2x/4x/8x

    inline constexpr const char* filter1Enable    = "filter1.enable";
    inline constexpr const char* filter1Type      = "filter1.type";
    inline constexpr const char* filter1Cutoff    = "filter1.cutoff";
    inline constexpr const char* filter1Resonance = "filter1.resonance";
    inline constexpr const char* filter1Drive     = "filter1.drive";
    inline constexpr const char* filter1Keytrack  = "filter1.keytrack";
    inline constexpr const char* filter1EnvAmount = "filter1.envAmount";
    inline constexpr const char* filter1Mix       = "filter1.mix";

    inline constexpr const char* filter2Enable    = "filter2.enable";
    inline constexpr const char* filter2Type      = "filter2.type";
    inline constexpr const char* filter2Cutoff    = "filter2.cutoff";
    inline constexpr const char* filter2Resonance = "filter2.resonance";
    inline constexpr const char* filter2Drive     = "filter2.drive";
    inline constexpr const char* filter2Keytrack  = "filter2.keytrack";
    inline constexpr const char* filter2EnvAmount = "filter2.envAmount";
    inline constexpr const char* filter2Mix       = "filter2.mix";
    inline constexpr const char* filterRouting    = "filter2.routing";   // Series/Parallel

    inline constexpr const char* ampAttack  = "ampEnv.attack";
    inline constexpr const char* ampDecay   = "ampEnv.decay";
    inline constexpr const char* ampSustain = "ampEnv.sustain";
    inline constexpr const char* ampRelease = "ampEnv.release";

    // Per-oscillator-slot parameter IDs: "oscA.position", "oscB.level", ...
    namespace osc
    {
        inline constexpr const char* enable       = "enable";
        inline constexpr const char* mode         = "mode";      // Wavetable/Sample/Granular
        inline constexpr const char* position     = "position";
        inline constexpr const char* coarse       = "coarse";
        inline constexpr const char* fine         = "fine";
        inline constexpr const char* level        = "level";
        inline constexpr const char* pan          = "pan";
        inline constexpr const char* phase        = "phase";
        inline constexpr const char* phaseMode    = "phaseMode";
        inline constexpr const char* table        = "table";     // wavetable-engine built-in Table menu
        inline constexpr const char* unisonCount  = "unisonCount";
        inline constexpr const char* unisonDetune = "unisonDetune";
        inline constexpr const char* unisonBlend  = "unisonBlend";
        inline constexpr const char* unisonWidth  = "unisonWidth";

        // Sample/SFX engine (classic playback)
        inline constexpr const char* sampleStart  = "sampleStart";
        inline constexpr const char* loop         = "loop";
        inline constexpr const char* loopStart    = "loopStart";
        inline constexpr const char* loopEnd      = "loopEnd";
        inline constexpr const char* keytrack     = "keytrack";
        inline constexpr const char* rootNote     = "rootNote";

        // Sample/SFX engine (granular)
        inline constexpr const char* grainSize    = "grainSize";
        inline constexpr const char* grainDensity = "grainDensity";
        inline constexpr const char* grainPos     = "grainPos";
        inline constexpr const char* grainSpray   = "grainSpray";
        inline constexpr const char* grainPitch   = "grainPitch";

        // Analog / FM / Noise / Pluck engines
        inline constexpr const char* analogShape  = "analogShape";
        inline constexpr const char* pulseWidth   = "pulseWidth";
        inline constexpr const char* fmRatio      = "fmRatio";
        inline constexpr const char* fmIndex      = "fmIndex";
        inline constexpr const char* noiseColor   = "noiseColor";
        inline constexpr const char* pluckDamp    = "pluckDamp";
    }

    juce::String oscSlot (int slotIndex, const char* key);
    juce::String oscSlotLetter (int slotIndex);

    // Envelope 2/3 parameter IDs: envParam(2, "attack") -> "env2.attack"
    juce::String envParam (int envNumber, const char* key);

    // Parametric-EQ band IDs: eqBand(0, "freq") -> "fxEQ.band0.freq"
    juce::String eqBand (int band, const char* key);

    // LFO parameter IDs: lfoParam(0, "rate") -> "lfo1.rate"
    namespace lfo
    {
        inline constexpr const char* shape    = "shape";
        inline constexpr const char* rate     = "rate";
        inline constexpr const char* sync     = "sync";
        inline constexpr const char* division = "division";
        inline constexpr const char* phase    = "phase";
        inline constexpr const char* retrig   = "retrig";
        inline constexpr const char* unipolar = "unipolar";
    }
    juce::String lfoParam (int lfoIndex, const char* key);

    juce::String macro (int macroIndex);  // "macros.macro1"

    // Arpeggiator.
    namespace arp
    {
        inline constexpr const char* enable  = "arp.enable";
        inline constexpr const char* mode    = "arp.mode";
        inline constexpr const char* division = "arp.division";
        inline constexpr const char* octaves = "arp.octaves";
        inline constexpr const char* gate    = "arp.gate";
        inline constexpr const char* swing   = "arp.swing";
        inline constexpr const char* latch   = "arp.latch";
        inline constexpr const char* phrase  = "arp.phrase";
        inline constexpr const char* velMode = "arp.velMode";

        // Probability controls (Elektron-style trig conditions).
        inline constexpr const char* chance   = "arp.chance";     // step fires vs rests
        inline constexpr const char* stutter  = "arp.stutter";    // ratchet chance (2-4 repeats)
        inline constexpr const char* jump     = "arp.jump";       // random +/- octave chance
        inline constexpr const char* humanize = "arp.humanize";   // velocity randomization
    }

    // Organic Chaos section.
    namespace chaos
    {
        inline constexpr const char* enable         = "chaos.enable";
        inline constexpr const char* depth          = "chaos.depth";
        inline constexpr const char* rate           = "chaos.rate";
        inline constexpr const char* mix            = "chaos.mix";
        inline constexpr const char* pitchOn        = "chaos.pitchOn";
        inline constexpr const char* pitchAmount    = "chaos.pitchAmount";
        inline constexpr const char* phaseOn        = "chaos.phaseOn";
        inline constexpr const char* phaseAmount    = "chaos.phaseAmount";
        inline constexpr const char* positionOn     = "chaos.positionOn";
        inline constexpr const char* positionAmount = "chaos.positionAmount";
        inline constexpr const char* ampOn          = "chaos.ampOn";
        inline constexpr const char* ampAmount      = "chaos.ampAmount";
        inline constexpr const char* satOn          = "chaos.satOn";
        inline constexpr const char* saturation     = "chaos.saturation";
        inline constexpr const char* distOn         = "chaos.distOn";
        inline constexpr const char* distortion     = "chaos.distortion";
    }

    // FX chain (global, post-synth).
    namespace fx
    {
        inline constexpr const char* distEnable = "fxDist.enable";
        inline constexpr const char* distType   = "fxDist.type";
        inline constexpr const char* distDrive  = "fxDist.drive";
        inline constexpr const char* distTone   = "fxDist.tone";
        inline constexpr const char* distMix    = "fxDist.mix";

        inline constexpr const char* chorusEnable   = "fxChorus.enable";
        inline constexpr const char* chorusRate     = "fxChorus.rate";
        inline constexpr const char* chorusDepth    = "fxChorus.depth";
        inline constexpr const char* chorusFeedback = "fxChorus.feedback";
        inline constexpr const char* chorusMix      = "fxChorus.mix";

        inline constexpr const char* delayEnable   = "fxDelay.enable";
        inline constexpr const char* delaySync     = "fxDelay.sync";
        inline constexpr const char* delayTime     = "fxDelay.time";
        inline constexpr const char* delayDivision = "fxDelay.division";
        inline constexpr const char* delayFeedback = "fxDelay.feedback";
        inline constexpr const char* delayPingPong = "fxDelay.pingpong";
        inline constexpr const char* delayMix      = "fxDelay.mix";

        inline constexpr const char* reverbEnable   = "fxReverb.enable";
        inline constexpr const char* reverbMode     = "fxReverb.mode";
        inline constexpr const char* reverbPreDelay = "fxReverb.predelay";
        inline constexpr const char* reverbSize     = "fxReverb.size";
        inline constexpr const char* reverbDecay    = "fxReverb.decay";
        inline constexpr const char* reverbDamping  = "fxReverb.damping";
        inline constexpr const char* reverbModDepth = "fxReverb.moddepth";
        inline constexpr const char* reverbLowCut   = "fxReverb.lowcut";
        inline constexpr const char* reverbHighCut  = "fxReverb.highcut";
        inline constexpr const char* reverbWidth    = "fxReverb.width";
        inline constexpr const char* reverbMix      = "fxReverb.mix";

        inline constexpr const char* eqEnable    = "fxEQ.enable";
        inline constexpr const char* eqCharacter = "fxEQ.character";

        // Per-band parametric-EQ keys, combined via eqBand(band, key) ->
        // "fxEQ.band0.freq" etc.
        namespace eqband
        {
            inline constexpr const char* enable = "enable";
            inline constexpr const char* type   = "type";   // Bell/LoShelf/HiShelf/LoCut/HiCut/Notch
            inline constexpr const char* freq   = "freq";
            inline constexpr const char* gain   = "gain";
            inline constexpr const char* q      = "q";
        }

        inline constexpr const char* modEnable   = "fxMod.enable";
        inline constexpr const char* modType     = "fxMod.type";
        inline constexpr const char* modRate     = "fxMod.rate";
        inline constexpr const char* modSync     = "fxMod.sync";
        inline constexpr const char* modDivision = "fxMod.division";
        inline constexpr const char* modDepth    = "fxMod.depth";
        inline constexpr const char* modFeedback = "fxMod.feedback";
        inline constexpr const char* modStages   = "fxMod.stages";
        inline constexpr const char* modCentre   = "fxMod.centre";
        inline constexpr const char* modManual   = "fxMod.manual";
        inline constexpr const char* modWidth    = "fxMod.width";
        inline constexpr const char* modMix      = "fxMod.mix";

        inline constexpr const char* tremEnable   = "fxTrem.enable";
        inline constexpr const char* tremRate     = "fxTrem.rate";
        inline constexpr const char* tremSync     = "fxTrem.sync";
        inline constexpr const char* tremDivision = "fxTrem.division";
        inline constexpr const char* tremDepth    = "fxTrem.depth";
        inline constexpr const char* tremShape    = "fxTrem.shape";
        inline constexpr const char* tremStereo   = "fxTrem.stereo";
        inline constexpr const char* tremMix      = "fxTrem.mix";
        inline constexpr const char* vibEnable    = "fxVib.enable";
        inline constexpr const char* vibRate      = "fxVib.rate";
        inline constexpr const char* vibSync      = "fxVib.sync";
        inline constexpr const char* vibDivision  = "fxVib.division";
        inline constexpr const char* vibDepth     = "fxVib.depth";
        inline constexpr const char* vibMix       = "fxVib.mix";

        inline constexpr const char* limEnable      = "fxLim.enable";
        inline constexpr const char* limDrive       = "fxLim.drive";
        inline constexpr const char* limCeiling     = "fxLim.ceiling";
        inline constexpr const char* limRelease     = "fxLim.release";
        inline constexpr const char* limAutoRelease = "fxLim.autoRelease";
        inline constexpr const char* limCharacter   = "fxLim.character";
        inline constexpr const char* limStereoLink  = "fxLim.stereoLink";
        inline constexpr const char* limTruePeak    = "fxLim.truePeak";
        inline constexpr const char* limLookahead   = "fxLim.lookahead";
        inline constexpr const char* limAutoGain     = "fxLim.autoGain";

        inline constexpr const char* convEnable   = "fxConv.enable";
        inline constexpr const char* convMix      = "fxConv.mix";
        inline constexpr const char* convWidth    = "fxConv.width";
        inline constexpr const char* convPreDelay = "fxConv.predelay";
        inline constexpr const char* convDecay    = "fxConv.decay";
        inline constexpr const char* convDamping  = "fxConv.damping";
    }

    // Matrix route parameter IDs: routeParam(0, "source") -> "matrix.route1.source"
    namespace route
    {
        inline constexpr const char* source = "source";
        inline constexpr const char* dest   = "dest";
        inline constexpr const char* depth  = "depth";
    }
    juce::String routeParam (int routeIndex, const char* key);
}

// Filter type choice order — DSP switches on the raw choice index, so this
// order is load-bearing.
enum class FilterType
{
    lp12, lp24, hp12, hp24, bp12, bp24, notch12, notch24,
};

// Oscillator phase behaviour on note-on.
enum class PhaseMode { reset, random, free_ };

// Portamento behaviour — choice order is load-bearing, append-only.
// legato only glides when the previous note is still held.
enum class GlideMode { off, always, legato };

// Voice allocation mode — choice order is load-bearing, append-only.
enum class VoiceMode { poly, mono, duo, paraphonic, unison };

// Note priority for mono/duo/paraphonic when more keys are held than voices.
enum class NotePriority { last, high, low };

// Oscillator slot engine — choice order is load-bearing, append-only.
enum class OscMode { wavetable, sample, granular, analog, fm, noise, pluck };

// Arpeggiator modes — choice order is load-bearing, append-only.
enum class ArpMode
{
    up, down, upDown, downUp, upDownInclusive,
    converge, diverge, asPlayed, chord,
    random, randomWalk, phrase,
};

// Melodic phrase patterns (semitone offsets from the lowest held note).
struct ArpPhrase
{
    const char* name;
    int length;
    int intervals[16];
};

const std::vector<ArpPhrase>& arpPhrases();

// LFO shape choice order — load-bearing, append-only.
enum class LFOShape { sine, triangle, sawUp, sawDown, square, sampleHold };

// Tempo-sync divisions, in beats (quarter notes). Choice order matches
// lfoDivisionBeats(). Append-only.
float lfoDivisionBeats (int divisionChoice);
const juce::StringArray& lfoDivisionNames();

const std::vector<ParamDef>& all();
const ParamDef* find (const juce::String& paramID);

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

} // namespace spa::params
