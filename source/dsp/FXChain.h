#pragma once

#include <atomic>
#include <cmath>
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "ModEffect.h"
#include "TremVib.h"
#include "Limiter.h"
#include "FDNReverb.h"
#include "ParametricEQ.h"
#include "../params/ParameterRegistry.h"

namespace spa::dsp
{

// Global stereo FX chain, processed after the synth mix and before master
// gain. Modules run in the order listed in `processOrder` — fixed for v1 but
// architected as an ordered list so reordering is a data change, not a
// rewrite.
class FXChain
{
public:
    FXChain() = default;

    // Append-only: module ids are serialized in the per-preset chain order.
    enum class Module { distortion, chorus, delay, reverb, eq, mod, tremVib, limiter, convolve };
    static constexpr int numModules = 9;

    // Pack/unpack the chain order into a uint64 (4 bits/module): a single atomic
    // for the lock-free UI->audio hand-off and compact preset storage. Unpack
    // validates the value is a permutation and falls back to the natural order.
    static juce::uint64 packOrder (const Module* order)
    {
        juce::uint64 v = 0;
        for (int i = 0; i < numModules; ++i)
            v |= (juce::uint64) ((int) order[i] & 0xF) << (i * 4);
        return v;
    }
    static juce::uint64 defaultOrderPacked()
    {
        Module def[numModules] { Module::distortion, Module::chorus, Module::mod,
                                 Module::tremVib, Module::delay, Module::reverb,
                                 Module::convolve, Module::eq, Module::limiter };
        return packOrder (def);
    }
    static void unpackOrder (juce::uint64 packed, Module* order)
    {
        bool seen[16] = {}; bool ok = true; int tmp[numModules];
        for (int i = 0; i < numModules; ++i)
        {
            const int id = (int) ((packed >> (i * 4)) & 0xF);
            tmp[i] = id;
            if (id < 0 || id >= numModules || seen[id]) { ok = false; break; }
            seen[id] = true;
        }
        for (int i = 0; i < numModules; ++i)
            order[i] = ok ? (Module) tmp[i] : (Module) i;
    }

    struct Params
    {
        bool distEnable = false;
        int distType = 0;          // Soft/Hard/Fold
        float distDrive = 0.3f;
        float distToneHz = 8000.0f;
        float distMix = 1.0f;

        bool chorusEnable = false;
        float chorusRate = 0.8f;
        float chorusDepth = 0.3f;
        float chorusFeedback = 0.0f;
        float chorusMix = 0.5f;

        bool delayEnable = false;
        bool delaySync = true;
        float delayTimeMs = 350.0f;
        int delayDivision = 6;
        float delayFeedback = 0.35f;
        bool delayPingPong = false;
        float delayMix = 0.35f;

        bool reverbEnable = false;
        int reverbMode = 0;         // 0 Hall 1 Plate 2 Chamber 3 Room 4 Spring
        float reverbPreDelay = 20.0f;
        float reverbSize = 0.5f;
        float reverbDecay = 2.0f;   // RT60 seconds
        float reverbDamping = 0.5f; // HF damp
        float reverbModDepth = 0.2f;
        float reverbLowCut = 20.0f;
        float reverbHighCut = 12000.0f;
        float reverbWidth = 1.0f;
        float reverbMix = 0.3f;

        bool eqEnable = false;
        int eqCharacter = 0;   // 0 Clean 1 Modern 2 Vintage 3 Tube
        std::array<ParametricEQ::Band, ParametricEQ::numBands> eqBands {};

        double bpm = 120.0;

        bool modEnable = false;
        int modType = 0;           // 0 = Phaser, 1 = Flanger
        float modRate = 0.5f;
        bool modSync = false;
        int modDivision = 6;
        float modDepth = 0.5f;
        float modFeedback = 0.3f;
        int modStages = 6;
        float modCentreHz = 800.0f;
        float modManualMs = 3.0f;
        float modWidth = 0.5f;
        float modMix = 0.5f;

        bool tremEnable = false;
        float tremRate = 5.0f;
        bool tremSync = false;
        int tremDivision = 6;
        float tremDepth = 0.5f;
        int tremShape = 0;
        float tremStereo = 0.0f;
        float tremMix = 1.0f;

        bool vibEnable = false;
        float vibRate = 5.0f;
        bool vibSync = false;
        int vibDivision = 6;
        float vibDepth = 0.5f;
        float vibMix = 1.0f;

        bool limEnable = false;
        float limDrive = 0.0f;
        float limCeiling = -0.3f;
        float limRelease = 120.0f;
        bool limAutoRelease = false;
        int limCharacter = 0;
        float limStereoLink = 1.0f;
        bool limTruePeak = false;
        bool limLookahead = false;
        bool limAutoGain = false;

        bool convEnable = false;
        float convMix = 0.3f;
        float convWidth = 1.0f;
        float convPreDelay = 0.0f;   // ms, wet pre-delay
        float convDecay = 1.0f;      // 0..1 IR tail length (shorter = tighter)
        float convDamping = 0.0f;    // 0..1 HF damping of the IR

        // Runtime FX processing order (drag-reorderable, saved per preset).
        Module order[numModules] {
            Module::distortion, Module::chorus, Module::mod, Module::tremVib,
            Module::delay, Module::reverb, Module::convolve, Module::eq, Module::limiter
        };
    };

    // Crush (bit-crusher distortion type) drive mappings, shared between the
    // DSP (processDistortion) and the UI (Displays.cpp's transfer-curve
    // staircase) so they can never drift apart. Both are exponential so the
    // DRIVE knob stays useful across its whole range: linear bit reduction
    // left most of the knob's travel inaudible (drive 0.5 -> ~9.5 bits).
    //   drive 0    -> 16 bits / no decimation (transparent)
    //   drive 0.5  -> ~6.9 bits / ~6.3-sample hold
    //   drive 0.8  -> ~4.2 bits / ~19-sample hold
    //   drive 1    -> 3 bits / a ~40-sample hold at 48 kHz
    static float crushBitsForDrive (float drive)
    {
        return 16.0f * std::pow (3.0f / 16.0f, drive);
    }
    static float crushHoldForDrive (float drive, double sampleRate)
    {
        return (float) (std::pow (40.0, (double) drive) * (sampleRate / 48000.0));
    }

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    void process (juce::AudioBuffer<float>& buffer, const Params& params);

    // Worst-case ring-out for AudioProcessor::getTailLengthSeconds().
    double tailSeconds (const Params& params) const;

    // Lookahead-limiter latency (reported to the host) + gain reduction meter.
    int limiterLatencySamples (const Params& p) const;
    float limiterGainReductionDb() const { return limiterEffect.gainReductionDb(); }
    float limiterOutputPeak() const { return limiterEffect.outputPeak(); }

    // Convolve (SFX / user WAV as impulse). The raw IR is read once and kept;
    // decay/damping reshape it and it is (re)loaded into juce::dsp::Convolution
    // on a background thread. All of these run on the message thread.
    void loadConvolutionIR (const juce::File& irFile);
    void setConvolutionShaping (float decay, float damping);   // reshapes if changed
    bool hasConvolutionIR() const { return convIrLoaded.load (std::memory_order_relaxed); }

    // Downsampled magnitude envelope of the shaped IR for the UI waveform.
    static constexpr int convEnvPoints = 256;
    const std::array<float, convEnvPoints>& convolutionEnvelope() const { return irEnvelope; }
    double convolutionLengthSeconds() const { return irLengthSeconds.load (std::memory_order_relaxed); }

private:
    void processDistortion (juce::AudioBuffer<float>&, const Params&);
    void processChorus (juce::AudioBuffer<float>&, const Params&);
    void processDelay (juce::AudioBuffer<float>&, const Params&);
    void processReverb (juce::AudioBuffer<float>&, const Params&);
    void processEQ (juce::AudioBuffer<float>&, const Params&);
    void processMod (juce::AudioBuffer<float>&, const Params&);
    void processTremVib (juce::AudioBuffer<float>&, const Params&);
    void processLimiter (juce::AudioBuffer<float>&, const Params&);
    void processConvolve (juce::AudioBuffer<float>&, const Params&);

    double sampleRate = 48000.0;
    ModEffect modEffect;
    TremVib tremVibEffect;
    Limiter limiterEffect;
    // Non-uniform partitioned convolution (256-sample head) rather than the
    // default uniform-block engine: IRs here run up to 10s (see
    // loadConvolutionIR's cap), and JUCE's own docs recommend NonUniform with
    // a >=256-sample head for reverberation-length IRs (>=~4096 samples) to
    // keep average CPU down on the long tail, at the cost of a little extra
    // latency at the head vs the zero-latency uniform default.
    juce::dsp::Convolution convolution { juce::dsp::Convolution::NonUniform { 256 } };
    juce::AudioBuffer<float> convScratch;
    // Written on the message thread (load/reshape), read on the audio thread
    // (process()) and from hasConvolutionIR() — same relaxed-atomic pattern as
    // the rest of the codebase's cross-thread flags.
    std::atomic<bool> convIrLoaded { false };

    // Raw (unshaped) IR kept so decay/damping can reshape without re-reading the
    // file; the reshaped copy is what gets loaded into the convolution engine.
    void reshapeConvolutionIR();
    juce::AudioFormatManager convFormats;
    juce::AudioBuffer<float> rawIR;
    double rawIRSampleRate = 0.0;
    bool haveRawIR = false;
    float convDecayApplied = 1.0f, convDampingApplied = 0.0f;
    // Written on the message thread (reshapeConvolutionIR), read from
    // tailSeconds() on the audio thread (getTailLengthSeconds).
    std::atomic<double> irLengthSeconds { 0.0 };
    std::array<float, convEnvPoints> irEnvelope {};

    // Wet pre-delay ring (per channel), up to 200 ms.
    std::array<juce::AudioBuffer<float>, 2> convPreBuf;
    int convPreWrite = 0;

    // Distortion tone filter (post-shaper lowpass), one per channel.
    std::array<juce::dsp::FirstOrderTPTFilter<float>, 2> toneFilters;

    // Crush distortion (sample-and-hold decimation) state, one per channel:
    // the currently-held output sample and a fractional phase accumulator
    // counting down the hold length. Fixed-size, no allocation.
    std::array<float, 2> crushHold {};
    std::array<float, 2> crushPhase {};

    juce::dsp::Chorus<float> chorus;

    // Delay: fixed max 4 s ring buffer per channel.
    juce::AudioBuffer<float> delayBuffer;
    int delayWritePos = 0;
    juce::SmoothedValue<float> delaySamplesSmoothed;

    FDNReverb reverb;

    // 8-band parametric EQ (hand-rolled biquads, character saturation).
    ParametricEQ eq;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FXChain)
};

} // namespace spa::dsp
