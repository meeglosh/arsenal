#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "Wavetable.h"
#include "WavetableOscillator.h"
#include "MultiModeFilter.h"
#include "LFO.h"
#include "ChaosGenerator.h"
#include "SamplePlayer.h"
#include "GranularPlayer.h"
#include "ExtraOscillators.h"
#include "Telemetry.h"

namespace spa::dsp
{

struct SPASynthSound : public juce::SynthesiserSound
{
    bool appliesToNote (int) override { return true; }
    bool appliesToChannel (int) override { return true; }
};

// Everything voices need for a block, written once per block by the processor
// (audio thread) before Synthesiser::renderNextBlock. Voices only read.
struct SharedState
{
    struct SlotStatic
    {
        bool enabled = false;
        params::OscMode mode = params::OscMode::wavetable;
        const Wavetable* table = nullptr;
        const SampleData* sample = nullptr;
        params::PhaseMode phaseMode = params::PhaseMode::reset;
        float phase = 0.0f;
        int unisonCount = 1;

        float sampleStart = 0.0f;
        bool loop = true;
        float loopStart = 0.0f;
        float loopEnd = 1.0f;
        bool keytrack = true;
        int rootNote = 60;
        float grainPitch = 0.0f;

        int analogShape = 0;
        float fmRatio = 2.0f;
        int noiseColor = 0;
    };

    struct Route
    {
        int source = 0;      // ModSource index
        int destIndex = 0;   // dense mod-dest index
        float depth = 0.0f;
    };

    // Portamento. GlideState is written by the synthesiser's note hooks in
    // MIDI event order (audio thread) and read by voices in startNote: the
    // glide origin (last note struck) and whether a previous key was still
    // physically held when this one landed (legato).
    struct GlideState
    {
        float lastNote = -1.0f;         // -1 = nothing struck yet
        int keysDown = 0;
        bool previousKeyHeld = false;
    };

    params::GlideMode glideMode = params::GlideMode::off;
    float glideTimeMs = 80.0f;
    GlideState* glide = nullptr;   // owned by the processor, audio thread only

    // Voice allocation. voiceMode/priority/unison config are written once per
    // block by the processor. pendingUnison* are set by the synthesiser right
    // before each unison voice's startNote so the voice knows its detune/pan
    // slot (audio thread only, synchronous — startNote runs inside noteOn).
    params::VoiceMode voiceMode = params::VoiceMode::poly;
    params::NotePriority notePriority = params::NotePriority::last;
    int unisonVoices = 1;
    float unisonDetuneCents = 0.0f;
    float unisonWidth = 0.0f;
    int pendingUnisonIndex = 0;
    int pendingUnisonCount = 1;

    // Paraphonic shared amp gate: the processor advances one ADSR from the
    // collective key state and renders it per-sample into paraEnvBlock for the
    // current block; paraphonic voices read it for gain instead of their own
    // amp envelope, and clear once the gate is released and the level hits zero.
    const float* paraEnvBlock = nullptr;   // length >= block; null unless paraphonic
    bool paraGateActive = false;

    std::array<SlotStatic, params::maxOscSlots> slots {};
    bool filter1Enabled = true;
    params::FilterType filterType = params::FilterType::lp12;
    float filterKeytrack = 0.0f;
    bool filter2Enabled = false;
    params::FilterType filter2Type = params::FilterType::lp12;
    float filter2Keytrack = 0.0f;
    bool filterParallel = false;

    // Normalized (0..1) base values for every mod destination, indexed by the
    // registry's dense mod-dest index.
    std::array<float, params::maxModDests> baseNorm {};

    int numActiveRoutes = 0;
    std::array<Route, params::numModRoutes> routes {};

    std::array<LFO::Params, params::numLFOs> lfo {};
    std::array<double, params::numLFOs> lfoGlobalPhase {};  // at block start

    std::array<float, params::numMacros> macros {};
    float modWheel = 0.0f;
    float aftertouch = 0.0f;
    double bpm = 120.0;

    Telemetry* telemetry = nullptr;   // audio -> UI channel, set once at startup

    // Organic Chaos statics (depth/rate/mix are mod destinations and live in
    // baseNorm; these are the plain toggles/amounts).
    struct Chaos
    {
        bool enabled = true;
        bool pitchOn = true;
        float pitchAmountCents = 8.0f;
        bool phaseOn = true;
        float phaseAmount = 0.15f;
        bool positionOn = true;
        float positionAmount = 0.2f;
        bool ampOn = true;
        float ampAmount = 0.2f;
        bool satOn = false;
        float saturation = 0.3f;
        bool distOn = false;
        float distortion = 0.2f;
    } chaos;
};

// Synthesiser wrapper that maintains the glide state around each note event.
// Voices read it inside startNote (which fires within Synthesiser::noteOn),
// so lastNote must be updated AFTER the base call: new voices glide FROM the
// previous note, not from themselves.
class GlideSynthesiser : public juce::Synthesiser
{
public:
    explicit GlideSynthesiser (SharedState& s) : shared (s) {}

    void noteOn (int channel, int note, float velocity) override
    {
        switch (shared.voiceMode)
        {
            case params::VoiceMode::mono:
                pushHeld (note, velocity); applyMonoDuo (channel, 1, true); break;
            case params::VoiceMode::duo:
                pushHeld (note, velocity); applyMonoDuo (channel, 2, true); break;

            case params::VoiceMode::unison:
                bumpGlide();
                stopNoteVoices (channel, note, false);   // fresh stack on re-press
                triggerUnison (channel, note, velocity);
                glide().lastNote = (float) note;
                break;

            case params::VoiceMode::poly:
            case params::VoiceMode::paraphonic:
            default:
                bumpGlide();
                juce::Synthesiser::noteOn (channel, note, velocity);
                glide().lastNote = (float) note;
                break;
        }
    }

    void noteOff (int channel, int note, float velocity, bool allowTailOff) override
    {
        if (shared.voiceMode == params::VoiceMode::mono
            || shared.voiceMode == params::VoiceMode::duo)
        {
            removeHeld (note);
            applyMonoDuo (channel, shared.voiceMode == params::VoiceMode::duo ? 2 : 1,
                          allowTailOff);
        }
        else if (shared.voiceMode == params::VoiceMode::paraphonic)
        {
            // Voices keep sounding under the shared envelope; only the collective
            // key count matters. They clear when the shared gate finally closes.
            glide().keysDown = juce::jmax (0, glide().keysDown - 1);
        }
        else
        {
            glide().keysDown = juce::jmax (0, glide().keysDown - 1);
            juce::Synthesiser::noteOff (channel, note, velocity, allowTailOff);
        }
    }

    void allNotesOff (int channel, bool allowTailOff) override
    {
        glide().keysDown = 0;
        heldCount = 0;
        soundingCount = 0;
        juce::Synthesiser::allNotesOff (channel, allowTailOff);
    }

private:
    static constexpr int maxHeld = 32;
    struct Held { int note = -1; float vel = 0.0f; };

    SharedState::GlideState& glide() { return *shared.glide; }

    void bumpGlide()
    {
        glide().previousKeyHeld = glide().keysDown > 0;
        ++glide().keysDown;
    }

    void pushHeld (int note, float vel)
    {
        removeHeld (note);                       // press order, no duplicates
        if (heldCount < maxHeld) held[(size_t) heldCount++] = { note, vel };
        glide().keysDown = heldCount;
    }

    void removeHeld (int note)
    {
        int w = 0;
        for (int i = 0; i < heldCount; ++i)
            if (held[(size_t) i].note != note) held[(size_t) w++] = held[(size_t) i];
        heldCount = w;
        glide().keysDown = heldCount;
    }

    // The up-to-maxSound held notes that should sound, chosen by priority.
    int computeDesired (int maxSound, std::array<Held, 2>& out) const
    {
        std::array<Held, maxHeld> pool = held;
        int poolN = heldCount, count = 0;
        while (count < maxSound && poolN > 0)
        {
            int pick = poolN - 1;   // 'last' = most recently pressed
            if (shared.notePriority == params::NotePriority::high)
            {
                pick = 0;
                for (int i = 1; i < poolN; ++i)
                    if (pool[(size_t) i].note > pool[(size_t) pick].note) pick = i;
            }
            else if (shared.notePriority == params::NotePriority::low)
            {
                pick = 0;
                for (int i = 1; i < poolN; ++i)
                    if (pool[(size_t) i].note < pool[(size_t) pick].note) pick = i;
            }
            out[(size_t) count++] = pool[(size_t) pick];
            pool[(size_t) pick] = pool[(size_t) --poolN];   // remove picked
        }
        return count;
    }

    void applyMonoDuo (int channel, int maxSound, bool allowTailOff)
    {
        std::array<Held, 2> want {};
        const int wantN = computeDesired (maxSound, want);
        const int soundingBefore = soundingCount;

        // Stop sounding notes no longer wanted. A note still physically held
        // (merely suppressed by priority) is hard-stopped so only the wanted
        // voices sound; a note whose key was released decays naturally.
        for (int i = 0; i < soundingCount; ++i)
        {
            bool keep = false;
            for (int j = 0; j < wantN; ++j) if (want[(size_t) j].note == sounding[(size_t) i]) keep = true;
            if (! keep)
                stopNoteVoices (channel, sounding[(size_t) i],
                                isHeld (sounding[(size_t) i]) ? false : allowTailOff);
        }

        // Start wanted notes not already sounding. Legato (glide from the last
        // note) only when we are replacing at capacity, not filling a chord.
        for (int j = 0; j < wantN; ++j)
        {
            bool already = false;
            for (int i = 0; i < soundingCount; ++i) if (sounding[(size_t) i] == want[(size_t) j].note) already = true;
            if (already) continue;
            glide().previousKeyHeld = soundingBefore >= maxSound;
            juce::Synthesiser::noteOn (channel, want[(size_t) j].note, want[(size_t) j].vel);
            glide().lastNote = (float) want[(size_t) j].note;
        }

        soundingCount = wantN;
        for (int j = 0; j < wantN; ++j) sounding[(size_t) j] = want[(size_t) j].note;
    }

    void triggerUnison (int channel, int note, float velocity)
    {
        auto sound = getSound (0);
        if (sound == nullptr) return;
        const int n = juce::jlimit (1, 7, shared.unisonVoices);
        for (int i = 0; i < n; ++i)
        {
            shared.pendingUnisonIndex = i;
            shared.pendingUnisonCount = n;
            if (auto* v = findFreeVoice (sound.get(), channel, note, isNoteStealingEnabled()))
                startVoice (v, sound.get(), channel, note, velocity);
        }
        shared.pendingUnisonIndex = 0;
        shared.pendingUnisonCount = 1;
    }

    bool isHeld (int note) const
    {
        for (int i = 0; i < heldCount; ++i) if (held[(size_t) i].note == note) return true;
        return false;
    }

    void stopNoteVoices (int channel, int note, bool allowTailOff)
    {
        for (int i = 0; i < getNumVoices(); ++i)
        {
            auto* v = getVoice (i);
            if (v->isVoiceActive() && v->getCurrentlyPlayingNote() == note
                && v->isPlayingChannel (channel))
                stopVoice (v, 1.0f, allowTailOff);
        }
    }

    SharedState& shared;
    std::array<Held, maxHeld> held {};
    int heldCount = 0;
    std::array<int, 2> sounding { -1, -1 };
    int soundingCount = 0;
};

class SPASynthVoice : public juce::SynthesiserVoice
{
public:
    // Modulation is evaluated at this granularity within a block.
    static constexpr int chunkSize = 64;

    explicit SPASynthVoice (const SharedState& shared);

    bool canPlaySound (juce::SynthesiserSound* sound) override;
    void startNote (int midiNoteNumber, float velocity,
                    juce::SynthesiserSound*, int currentPitchWheelPosition) override;
    void stopNote (float velocity, bool allowTailOff) override;
    void pitchWheelMoved (int newPitchWheelValue) override;
    void controllerMoved (int, int) override {}
    void setCurrentPlaybackSampleRate (double newRate) override;
    void renderNextBlock (juce::AudioBuffer<float>& outputBuffer,
                          int startSample, int numSamples) override;

    // Message-thread only: lazily backs the given slot's Pluck (Karplus-
    // Strong) buffer once that slot is actually used in Pluck mode. See
    // PluckString::ensureAllocated() and SPASynthProcessor::timerCallback().
    void ensurePluckAllocated (int slot)
    {
        if (slot >= 0 && slot < (int) plucks.size())
            plucks[(size_t) slot].ensureAllocated();
    }

private:
    // Computes modulation sources and effective parameter values for one
    // chunk, then configures oscillators/filter/envelopes from them.
    // blockOffset = samples since block start (for global LFO phase).
    void computeChunk (int blockOffset, int chunkLen);

    float baseValue (int destIndex) const;  // denormalized, no modulation

    const SharedState& shared;
    std::array<UnisonOscillator, params::maxOscSlots> oscs;
    std::array<SamplePlayer, params::maxOscSlots> samplePlayers;
    std::array<GranularPlayer, params::maxOscSlots> granularPlayers;
    std::array<AnalogOscillator, params::maxOscSlots> analogOscs;
    std::array<FMOscillator, params::maxOscSlots> fmOscs;
    std::array<NoiseGenerator, params::maxOscSlots> noiseGens;
    std::array<PluckString, params::maxOscSlots> plucks;
    std::array<float, params::maxOscSlots> slotPulseWidth {}, slotFMIndex {}, slotPluckDamp {};
    std::array<SamplePlayer::Params, params::maxOscSlots> sampleParams {};
    std::array<GranularPlayer::Params, params::maxOscSlots> granularParams {};
    MultiModeFilter filter, filter2;
    juce::ADSR ampEnv, env2, env3;
    std::array<LFO, params::numLFOs> lfos;
    ChaosGenerator chaosGen;
    juce::Random random;

    int currentNote = -1;
    int noteSerial = -1;       // telemetry writer arbitration

    // Portamento: the pitch actually sounding, ramped toward the struck note
    // per modulation chunk. Equal to the note when glide is off.
    float glideNote = -1.0f, glideTarget = -1.0f;
    float glideRate = 0.0f;    // semitones per second
    float velocity = 1.0f;
    float pitchBendSemitones = 0.0f;
    float unisonDetuneSemis = 0.0f;   // voice-stack unison (Unison mode)
    float unisonPanL = 1.0f, unisonPanR = 1.0f;
    bool paraSawGate = false;         // paraphonic: gate opened since this note
    float ampEnvLast = 0.0f;   // amp env value fed back as mod source
    std::array<float, params::maxOscSlots> slotGains {};  // per-chunk linear slot gains

    // Chaos state carried between chunks: effective depth/rate/mix from the
    // previous chunk (chaos feeds the matrix that modulates them — one-chunk
    // latency breaks the cycle), plus the current shaper drives.
    float chaosDepth = 0.0f, chaosRate = 2.0f, chaosMix = 1.0f;
    float satDrive = 0.0f, distDrive = 0.0f, chaosAmpGain = 1.0f;
    float filterMixValue = 1.0f;    // per-chunk dry/wet, filter 1
    float filter2MixValue = 1.0f;   // per-chunk dry/wet, filter 2

    // Per-slot pan gains for sample/granular modes (wavetable pan is handled
    // inside the unison oscillator), and last chunk's effective grain position
    // (feeds the follower playhead, same one-chunk-latency trick as chaos).
    std::array<float, params::maxOscSlots> slotPanL {}, slotPanR {};
    std::array<float, params::maxOscSlots> lastGrainPos {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SPASynthVoice)
};

} // namespace spa::dsp
