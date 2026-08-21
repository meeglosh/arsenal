#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

#include "params/ParameterRegistry.h"
#include "dsp/SPASynthVoice.h"
#include "dsp/FXChain.h"
#include "dsp/Arpeggiator.h"
#include "dsp/MidiClockSync.h"
#include "library/PresetManager.h"
#include "MidiLearn.h"

namespace spa
{

class SPASynthProcessor : public juce::AudioProcessor,
                         public juce::ChangeBroadcaster,
                         private juce::Timer,
                         private juce::AudioProcessorValueTreeState::Listener
{
public:
    SPASynthProcessor();
    ~SPASynthProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    // Host-triggered bypass. JUCE's default processBlockBypassed() only clears
    // channels beyond the input bus, which for an instrument (no audio input
    // bus) means it hard-zeros the entire output the instant a host calls it --
    // any reverb/delay/convolve tail is cut with a click instead of ringing
    // out. We run the exact same engine/FX pipeline as processBlock, just with
    // new note-on messages filtered out of the incoming MIDI first, so already-
    // sounding voices release/tail off naturally (as if the keys had just been
    // let go) while nothing new can start. No `getBypassParameter()` override:
    // its default (nullptr) is what makes the host call this method directly
    // rather than routing bypass through a plugin parameter, which is exactly
    // the mechanism we want.
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "SPASynth"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return fxChain.tailSeconds (fxParams); }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }

    // Wavetable slot management (message thread). Loads run on a background
    // thread; ChangeBroadcaster fires when a slot's table changes.
    void loadWavetableFromFile (int slot, const juce::File& file);
    void setFactoryWavetable (int slot);
    juce::String getWavetableName (int slot) const;
    juce::String getWavetableError (int slot) const;

    // Sample/SFX slot management — same threading contract as wavetables.
    void loadSampleFromFile (int slot, const juce::File& file);
    juce::String getSampleName (int slot) const;
    juce::String getSampleError (int slot) const;
    juce::File getSampleFile (int slot) const
    {
        return juce::File (slotSamples[(size_t) slot].path);
    }

    // Quick-swap: the WAV files in the currently loaded sample's pack (its
    // parent folder), name-sorted, but only when that sample lives under the
    // library root. Empty otherwise (no sample, or a user file loaded from
    // outside the library) — the UI hides the swap affordance in that case.
    juce::Array<juce::File> getPackSiblings (int slot) const;

    // True while a background load for the slot is still in flight — the UI
    // shows a loading state instead of stale/empty content (big SFX files
    // take a visible moment when a preset switches).
    bool isWavetableLoading (int slot) const
    {
        return slotTables[(size_t) slot].pendingLoads.load() > 0;
    }
    bool isSampleLoading (int slot) const
    {
        return slotSamples[(size_t) slot].pendingLoads.load() > 0;
    }

    // Content access for UI displays (message thread).
    std::shared_ptr<const dsp::Wavetable> getWavetable (int slot) const
    {
        return slotTables[(size_t) slot].current;
    }
    std::shared_ptr<const dsp::SampleData> getSample (int slot) const
    {
        return slotSamples[(size_t) slot].current;
    }

    // Audio -> UI telemetry (lock-free; UI reads on its repaint timers).
    dsp::Telemetry& getTelemetry() { return telemetry; }

    // MIDI Learn (right-click assignments).
    MidiLearnManager& getMidiLearn() { return *midiLearn; }

    // On-screen / computer-keyboard input. The editor's MidiKeyboardComponent
    // drives this state; processBlock merges its notes into the MIDI stream.
    juce::MidiKeyboardState& getKeyboardState() { return keyboardState; }

    // Panic: kill all sound and clear stuck state (e.g. a latched arp chord).
    // Message-thread safe — just raises a flag the audio thread services. Also
    // fired by incoming MIDI All Sound/Notes Off (CC 120/123).
    void panic() { panicRequested.store (true, std::memory_order_relaxed); }

    // Standalone tempo (no host playhead). BPM + sync source (0 = internal,
    // 1 = external MIDI clock); set from the standalone UI, ignored when a host
    // provides tempo. getCurrentBpm() is the resolved live tempo for display.
    void setInternalBpm (double bpm);
    void setTempoSyncMode (int mode);
    double getInternalBpm() const { return internalBpm.load (std::memory_order_relaxed); }
    int getTempoSyncMode() const { return tempoSyncMode.load (std::memory_order_relaxed); }
    double getCurrentBpm() const { return currentBpm.load (std::memory_order_relaxed); }

    // FX chain order (drag-reorderable, saved per preset): FXChain module ids in
    // processing order. RT-safe hand-off via a single packed atomic.
    void setFxOrder (const juce::Array<int>& moduleIds);
    juce::Array<int> getFxOrder() const;

    // Convolve IR (SFX / user WAV as impulse). Loads it into the FX chain and
    // remembers the path (portable, saved per preset). Empty name = none.
    void loadConvolutionIR (const juce::File& file);
    juce::String getConvolutionIRName() const { return juce::File (convIrPath).getFileNameWithoutExtension(); }
    // Shaped-IR waveform envelope for the Convolve display (message thread).
    const std::array<float, dsp::FXChain::convEnvPoints>& getConvolutionEnvelope() const
    {
        return fxChain.convolutionEnvelope();
    }
    bool hasConvolutionIR() const { return fxChain.hasConvolutionIR(); }

    // RANDOMIZE ALL (message thread). Wildness and lock state live as state
    // properties so they persist with the session but stay non-automatable.
    void randomizeAll();
    float getRandomWildness() const;
    void setRandomWildness (float wildness);
    bool isLockGroupLocked (int group) const;
    void setLockGroupLocked (int group, bool locked);

    // Presets + library (message thread).
    library::PresetManager& getPresetManager() { return *presetManager; }

    // Full plugin state as a tree (params + portable wavetable/sample paths).
    // Shared by host chunks and the preset system; MIDI mappings ride along
    // only in host sessions (presets must never clobber hardware setups).
    juce::ValueTree buildStateTree (bool includeMidiMap = true);
    void restoreStateTree (const juce::ValueTree&);

    // Rescans the configured library and (re)generates factory presets for
    // any packs that don't have them yet.
    bool refreshLibrary();   // false if the library root is missing/invalid

private:
    void updateSharedState (int blockLength);
    void scanMidiControllers (const juce::MidiBuffer& midi);
    void installTable (int slot, std::shared_ptr<const dsp::Wavetable> table,
                       juce::String path, juce::String error);
    void installSample (int slot, std::shared_ptr<const dsp::SampleData> sample,
                        juce::String path, juce::String error);
    void timerCallback() override;

    // AudioProcessorValueTreeState::Listener. Registered ONLY for the osc-slot
    // mode parameters (see the ctor), to lazily allocate Pluck buffers -- see
    // ensurePluckAllocatedForSlot() and the comment on its definition for why
    // this callback is safely message-thread-only.
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void ensurePluckAllocatedForSlot (int slot);

    juce::AudioProcessorValueTreeState apvts;

    dsp::SharedState shared;   // written on audio thread, read by voices
    dsp::Telemetry telemetry;
    dsp::Arpeggiator arp;
    dsp::SharedState::GlideState glideState;   // updated by the synth's note hooks
    dsp::GlideSynthesiser synth { shared };
    dsp::FXChain fxChain;
    dsp::FXChain::Params fxParams;

    // Message-thread flags: once a slot's Pluck (Karplus-Strong) buffers
    // have been lazily allocated across all voices (see parameterChanged()/
    // ensurePluckAllocatedForSlot(), with timerCallback() as a fallback),
    // set so the check doesn't keep re-taking the callback lock every tick.
    std::array<bool, params::numOscSlots> pluckAllocated {};

    // Paraphonic shared amp envelope (voice mode = Paraphonic): one ADSR gated
    // by the collective key count, rendered per-block into paraEnvBuf for the
    // voices to read. paraGateWasOn tracks the gate edge across blocks.
    juce::ADSR paraEnv;
    juce::AudioBuffer<float> paraEnvBuf;
    bool paraGateWasOn = false;

    // Whole-synth oversampling: the entire engine (arp, voices, FX, master) runs
    // at hostRate * factor into an oversampled block, then a near-zero-latency
    // IIR polyphase filter decimates back to the host rate. Off by default.
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    int currentOsFactor = 1;
    double hostSampleRate = 48000.0;
    int hostBlockSize = 512;
    int osLatencyHost = 0;
    std::atomic<int> pendingOsFactor { 1 };

    // MIDI scaled into the (possibly oversampled) engine sample domain; a
    // persistent member (preallocated in prepareEngine, like Arpeggiator::scratch)
    // so processBlock never heap-allocates via a fresh local MidiBuffer.
    juce::MidiBuffer scaledMidi;

    // Preallocated scratch buffer for processBlockBypassed's note-on filtering
    // (same no-audio-thread-allocation rationale as scaledMidi above).
    juce::MidiBuffer bypassMidi;

    void prepareEngine (double engineRate, int engineBlock);   // rate-dependent setup
    void rebuildOversampling (int factor);                     // message thread only
    void renderEngine (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> masterGain;

    double currentSampleRate = 44100.0;
    float lastModWheel = 0.0f;
    float lastAftertouch = 0.0f;
    std::array<double, params::numLFOs> lfoPhaseAccum {};  // free-running LFO phases

    // --- Wavetable storage -------------------------------------------------
    // Audio thread reads `live` each block; message thread owns `current` and
    // retires old tables, which are freed on a timer well after the audio
    // thread has moved on.
    struct SlotTable
    {
        std::shared_ptr<const dsp::Wavetable> current;       // message thread
        std::atomic<const dsp::Wavetable*> live { nullptr }; // audio thread
        juce::String path;                                    // "" = factory
        juce::String error;
        std::atomic<int> pendingLoads { 0 };                  // in-flight background loads
        int requestSerial = 0;   // latest-swap-wins, same rationale as SlotSample::requestSerial
    };
    std::shared_ptr<const dsp::Wavetable> factoryTable;
    std::array<SlotTable, params::maxOscSlots> slotTables;
    std::vector<std::shared_ptr<const dsp::Wavetable>> retiredTables;  // message thread

    // Sample/SFX storage, same ownership scheme as wavetables.
    struct SlotSample
    {
        std::shared_ptr<const dsp::SampleData> current;       // message thread
        std::atomic<const dsp::SampleData*> live { nullptr }; // audio thread
        juce::String path;
        juce::String error;
        std::atomic<int> pendingLoads { 0 };                  // in-flight background loads
        int requestSerial = 0;   // latest-swap-wins: newest request supersedes older ones
    };
    std::array<SlotSample, params::maxOscSlots> slotSamples;
    std::vector<std::shared_ptr<const dsp::SampleData>> retiredSamples;  // message thread

    // Constructed after the APVTS (they capture parameter/default state).
    std::unique_ptr<MidiLearnManager> midiLearn;
    std::unique_ptr<library::PresetManager> presetManager;

    // On-screen keyboard note source (editor writes, processBlock reads).
    juce::MidiKeyboardState keyboardState;

    // Raised by panic() / MIDI CC 120/123; serviced at the top of processBlock.
    std::atomic<bool> panicRequested { false };

    // Raised by processBlock's non-finite output scan; serviced by the 150ms
    // timer under getCallbackLock() (same pattern as pendingOsFactor), which
    // resets the FX chain so a poisoned feedback structure (delay/reverb/etc.)
    // cannot keep recirculating garbage after the current block is silenced.
    std::atomic<bool> fxStateFlushPending { false };

    // Tempo: internal BPM + external MIDI clock for the standalone. Resolved
    // once per block into the block* fields (host playhead wins when present).
    std::atomic<double> internalBpm { 120.0 };
    std::atomic<int> tempoSyncMode { 0 };       // 0 = internal, 1 = external MIDI clock
    std::atomic<double> currentBpm { 120.0 };   // resolved live tempo (UI display)
    dsp::MidiClockSync midiClock;
    double blockBpm = 120.0;
    bool blockPlaying = true;
    double blockPpq = 0.0;

    // Packed FX chain order (4 bits/module); set by the UI, read each block.
    std::atomic<juce::uint64> fxOrderPacked { dsp::FXChain::defaultOrderPacked() };

    // Lookahead-limiter latency: written from the audio thread, applied via
    // setLatencySamples on the timer (message thread) when it changes.
    std::atomic<int> desiredLatency { 0 };

    juce::String convIrPath;   // Convolve impulse path (portable, per preset)

    // Cached raw parameter pointers (atomic floats owned by the APVTS).
    struct RawSlot
    {
        std::atomic<float>* enable = nullptr;
        std::atomic<float>* mode = nullptr;
        std::atomic<float>* phase = nullptr;
        std::atomic<float>* phaseMode = nullptr;
        std::atomic<float>* unisonCount = nullptr;
        std::atomic<float>* sampleStart = nullptr;
        std::atomic<float>* loop = nullptr;
        std::atomic<float>* loopStart = nullptr;
        std::atomic<float>* loopEnd = nullptr;
        std::atomic<float>* keytrack = nullptr;
        std::atomic<float>* rootNote = nullptr;
        std::atomic<float>* grainPitch = nullptr;
        std::atomic<float>* analogShape = nullptr;
        std::atomic<float>* fmRatio = nullptr;
        std::atomic<float>* noiseColor = nullptr;
    };

    struct RawLFO
    {
        std::atomic<float>* shape = nullptr;
        std::atomic<float>* rate = nullptr;
        std::atomic<float>* sync = nullptr;
        std::atomic<float>* division = nullptr;
        std::atomic<float>* phase = nullptr;
        std::atomic<float>* retrig = nullptr;
        std::atomic<float>* unipolar = nullptr;
    };

    struct RawRoute
    {
        std::atomic<float>* source = nullptr;
        std::atomic<float>* dest = nullptr;
        std::atomic<float>* depth = nullptr;
    };

    struct RawChaos
    {
        std::atomic<float>* enable = nullptr;
        std::atomic<float>* pitchOn = nullptr;
        std::atomic<float>* pitchAmount = nullptr;
        std::atomic<float>* phaseOn = nullptr;
        std::atomic<float>* phaseAmount = nullptr;
        std::atomic<float>* positionOn = nullptr;
        std::atomic<float>* positionAmount = nullptr;
        std::atomic<float>* ampOn = nullptr;
        std::atomic<float>* ampAmount = nullptr;
        std::atomic<float>* satOn = nullptr;
        std::atomic<float>* saturation = nullptr;
        std::atomic<float>* distOn = nullptr;
        std::atomic<float>* distortion = nullptr;
    };

    struct RawArp
    {
        std::atomic<float>* enable = nullptr;
        std::atomic<float>* mode = nullptr;
        std::atomic<float>* division = nullptr;
        std::atomic<float>* octaves = nullptr;
        std::atomic<float>* gate = nullptr;
        std::atomic<float>* swing = nullptr;
        std::atomic<float>* latch = nullptr;
        std::atomic<float>* phrase = nullptr;
        std::atomic<float>* velMode = nullptr;
        std::atomic<float>* chance = nullptr;
        std::atomic<float>* stutter = nullptr;
        std::atomic<float>* jump = nullptr;
        std::atomic<float>* humanize = nullptr;
    };

    struct Raw
    {
        std::atomic<float>* masterGain = nullptr;
        std::atomic<float>* glideMode = nullptr;
        std::atomic<float>* glideTime = nullptr;
        std::atomic<float>* voiceMode = nullptr;
        std::atomic<float>* notePriority = nullptr;
        std::atomic<float>* unisonVoices = nullptr;
        std::atomic<float>* unisonDetune = nullptr;
        std::atomic<float>* unisonWidth = nullptr;
        std::atomic<float>* ampAttack = nullptr;
        std::atomic<float>* ampDecay = nullptr;
        std::atomic<float>* ampSustain = nullptr;
        std::atomic<float>* ampRelease = nullptr;
        std::atomic<float>* oversampling = nullptr;
        std::atomic<float>* filter1Enable = nullptr;
        std::atomic<float>* filterType = nullptr;
        std::atomic<float>* filterKeytrack = nullptr;
        std::atomic<float>* filter2Enable = nullptr;
        std::atomic<float>* filter2Type = nullptr;
        std::atomic<float>* filter2Keytrack = nullptr;
        std::atomic<float>* filterRouting = nullptr;
        RawArp arp {};
        RawChaos chaos {};
        std::array<RawSlot, params::numOscSlots> slots {};
        std::array<RawLFO, params::numLFOs> lfos {};
        std::array<std::atomic<float>*, params::numMacros> macros {};
        std::array<RawRoute, params::numModRoutes> routes {};
        // One pointer per mod destination, in dense mod-dest index order.
        std::vector<std::atomic<float>*> dests;

        struct RawFX
        {
            std::atomic<float>* distEnable = nullptr;
            std::atomic<float>* distType = nullptr;
            std::atomic<float>* distDrive = nullptr;
            std::atomic<float>* distTone = nullptr;
            std::atomic<float>* distMix = nullptr;
            std::atomic<float>* chorusEnable = nullptr;
            std::atomic<float>* chorusRate = nullptr;
            std::atomic<float>* chorusDepth = nullptr;
            std::atomic<float>* chorusFeedback = nullptr;
            std::atomic<float>* chorusMix = nullptr;
            std::atomic<float>* delayEnable = nullptr;
            std::atomic<float>* delaySync = nullptr;
            std::atomic<float>* delayTime = nullptr;
            std::atomic<float>* delayDivision = nullptr;
            std::atomic<float>* delayFeedback = nullptr;
            std::atomic<float>* delayPingPong = nullptr;
            std::atomic<float>* delayMix = nullptr;
            std::atomic<float>* reverbEnable = nullptr;
            std::atomic<float>* reverbMode = nullptr;
            std::atomic<float>* reverbPreDelay = nullptr;
            std::atomic<float>* reverbSize = nullptr;
            std::atomic<float>* reverbDecay = nullptr;
            std::atomic<float>* reverbDamping = nullptr;
            std::atomic<float>* reverbModDepth = nullptr;
            std::atomic<float>* reverbLowCut = nullptr;
            std::atomic<float>* reverbHighCut = nullptr;
            std::atomic<float>* reverbWidth = nullptr;
            std::atomic<float>* reverbMix = nullptr;
            std::atomic<float>* eqEnable = nullptr;
            std::atomic<float>* eqCharacter = nullptr;
            struct EqBandPtrs
            {
                std::atomic<float>* enable = nullptr;
                std::atomic<float>* type = nullptr;
                std::atomic<float>* freq = nullptr;
                std::atomic<float>* gain = nullptr;
                std::atomic<float>* q = nullptr;
            };
            std::array<EqBandPtrs, 8> eqBands {};

            std::atomic<float>* modEnable = nullptr;
            std::atomic<float>* modType = nullptr;
            std::atomic<float>* modRate = nullptr;
            std::atomic<float>* modSync = nullptr;
            std::atomic<float>* modDivision = nullptr;
            std::atomic<float>* modDepth = nullptr;
            std::atomic<float>* modFeedback = nullptr;
            std::atomic<float>* modStages = nullptr;
            std::atomic<float>* modCentre = nullptr;
            std::atomic<float>* modManual = nullptr;
            std::atomic<float>* modWidth = nullptr;
            std::atomic<float>* modMix = nullptr;

            std::atomic<float>* tremEnable = nullptr;
            std::atomic<float>* tremRate = nullptr;
            std::atomic<float>* tremSync = nullptr;
            std::atomic<float>* tremDivision = nullptr;
            std::atomic<float>* tremDepth = nullptr;
            std::atomic<float>* tremShape = nullptr;
            std::atomic<float>* tremStereo = nullptr;
            std::atomic<float>* tremMix = nullptr;
            std::atomic<float>* vibEnable = nullptr;
            std::atomic<float>* vibRate = nullptr;
            std::atomic<float>* vibSync = nullptr;
            std::atomic<float>* vibDivision = nullptr;
            std::atomic<float>* vibDepth = nullptr;
            std::atomic<float>* vibMix = nullptr;

            std::atomic<float>* limEnable = nullptr;
            std::atomic<float>* limDrive = nullptr;
            std::atomic<float>* limCeiling = nullptr;
            std::atomic<float>* limRelease = nullptr;
            std::atomic<float>* limAutoRelease = nullptr;
            std::atomic<float>* limCharacter = nullptr;
            std::atomic<float>* limStereoLink = nullptr;
            std::atomic<float>* limTruePeak = nullptr;
            std::atomic<float>* limLookahead = nullptr;
            std::atomic<float>* limAutoGain = nullptr;

            std::atomic<float>* convEnable = nullptr;
            std::atomic<float>* convMix = nullptr;
            std::atomic<float>* convWidth = nullptr;
            std::atomic<float>* convPreDelay = nullptr;
            std::atomic<float>* convDecay = nullptr;
            std::atomic<float>* convDamping = nullptr;
        } fx {};
    } raw;

    void updateFXParams();

    static constexpr int numVoices = 16;

    JUCE_DECLARE_WEAK_REFERENCEABLE (SPASynthProcessor)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SPASynthProcessor)
};

} // namespace spa
