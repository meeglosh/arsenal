#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace spa::dsp
{

// Switchable modulation effect: Phaser (cascaded first-order allpass, variable
// stages) or Flanger (modulated short delay with bipolar feedback). Stereo,
// with an L/R LFO phase spread. Fed already-resolved rate in Hz by the FXChain.
class ModEffect
{
public:
    enum class Type { phaser, flanger };

    // True max flanger modulated delay, derived from ParameterRegistry
    // (source/params/ParameterRegistry.cpp): fx::modManual ("Mod Delay") has
    // range {0.1, 20.0} ms and feeds Params::manualMs, which processFlanger
    // clamps to [0.1, 20] as `baseMs`; processFlanger's own sweep term is
    // `sweepMs = 0.5 + 9.0f * depth` with `depth` fed by fx::modDepth's
    // registry range [0, 1] -> sweep tops out at 0.5 + 9.0 = 9.5 ms. Max
    // modulated delay = 20 + 9.5 = 29.5 ms. Sized here instead of a fixed
    // 2048-sample buffer because the FX chain (and this effect with it) runs
    // at the oversampled ENGINE rate -- up to 8x host rate -- so a
    // compile-time sample count silently clamps the achievable delay at high
    // rates (2048 samples is only ~5.3ms at 384kHz, well under 29.5ms).
    // +4ms safety margin, +2 samples for the linear-interpolation read
    // window (dl[i0]/dl[i1]).
    static int requiredFlangerDelaySamples (double sampleRateHz)
    {
        constexpr float kMaxDelayMs = 20.0f + 9.5f + 4.0f;
        return (int) std::ceil (kMaxDelayMs * 0.001 * sampleRateHz) + 2;
    }

    // Allocates (message-thread only -- see prepareEngine/rebuildOversampling
    // in SPASynthProcessor, which run this under getCallbackLock() with the
    // audio thread blocked) to fit the actual processing sample rate, then
    // clears state. Never called from the audio thread.
    void prepare (double sr, int /*maxBlock*/)
    {
        sampleRate = sr;
        dlLen = requiredFlangerDelaySamples (sr);
        for (auto& ch : channels)
            ch.dl.assign ((size_t) dlLen, 0.0f);
        reset();
    }

    // Message-thread only (FXChain::reset(), called from SPASynthProcessor
    // under getCallbackLock()) -- but kept allocation-free anyway so it stays
    // safe even if that ever changes.
    void reset()
    {
        for (auto& ch : channels)
            ch.clear();
        lfoPhase = 0.0f;
        wasEnabled = false;
    }

    struct Params
    {
        bool enable = true;        // edge-detected here so FXChain can call
                                    // process() unconditionally (see below)
        Type type = Type::phaser;
        float rateHz = 0.5f;
        float depth = 0.5f;        // 0..1
        float feedback = 0.3f;     // -0.95..0.95 (flanger may be bipolar)
        int stages = 6;            // phaser allpass count
        float centreHz = 800.0f;   // phaser sweep centre
        float manualMs = 3.0f;     // flanger base delay
        float spread = 0.5f;       // 0..1 L/R LFO phase offset
        float mix = 0.5f;          // dry/wet
    };

    // Self-contained enable-edge tracking: FXChain::process() can gate this
    // module by simply not calling process() while disabled (cheapest), or by
    // calling it every block with `enable=false` (a trivial early-out) — either
    // way, `wasEnabled` still detects the false->true transition here rather
    // than needing the chain to remember it. On that edge, clear the per-
    // channel allpass/feedback/delay state so a hot phaser/flanger that was
    // disabled mid-ring doesn't dump its trapped feedback into the mix on
    // re-enable. `dl[]` is a preallocated (in prepare(), not here) buffer
    // sized for the current sample rate, so zeroing its contents on the
    // (infrequent) enable edge is a non-issue on the audio thread --
    // ChannelState::clear() only touches contents, never (re)allocates, so
    // this stays realtime-safe; lfoPhase is deliberately left running so
    // re-enabling doesn't also click the LFO back to phase 0.
    void process (juce::AudioBuffer<float>& buffer, const Params& p)
    {
        if (! p.enable)
        {
            wasEnabled = false;
            return;
        }
        if (! wasEnabled)
        {
            for (auto& ch : channels) ch.clear();
            wasEnabled = true;
        }

        const int n = buffer.getNumSamples();
        const int numCh = juce::jmin (2, buffer.getNumChannels());
        const float phaseInc = (float) (p.rateHz / sampleRate);
        const float spreadOffset = 0.5f * juce::jlimit (0.0f, 1.0f, p.spread);

        for (int i = 0; i < n; ++i)
        {
            for (int ch = 0; ch < numCh; ++ch)
            {
                auto& st = channels[ch];
                const float lp = lfoPhase + (ch == 1 ? spreadOffset : 0.0f);
                const float lfo = 0.5f + 0.5f * std::sin (lp * juce::MathConstants<float>::twoPi);

                float* d = buffer.getWritePointer (ch);
                const float dry = d[i];
                float wet = dry;

                if (p.type == Type::phaser)
                    wet = processPhaser (st, dry, lfo, p);
                else
                    wet = processFlanger (st, dry, lfo, p);

                d[i] = dry + (wet - dry) * juce::jlimit (0.0f, 1.0f, p.mix);
            }
            lfoPhase += phaseInc;
            if (lfoPhase >= 1.0f) lfoPhase -= 1.0f;
        }
    }

private:
    static constexpr int maxStages = 12;

    struct ChannelState
    {
        float ap[maxStages] = {};   // allpass state (phaser)
        float fbLast = 0.0f;
        std::vector<float> dl;      // flanger delay line, sized by ModEffect::prepare()
        int dlWrite = 0;

        // RT-safe state clear: touches contents only, never (re)allocates
        // `dl`, so this can run from the enable-edge inside process() (audio
        // thread) as well as from reset() (message-thread only).
        void clear()
        {
            for (auto& a : ap) a = 0.0f;
            fbLast = 0.0f;
            std::fill (dl.begin(), dl.end(), 0.0f);
            dlWrite = 0;
        }
    };

    float processPhaser (ChannelState& st, float in, float lfo, const Params& p)
    {
        // Sweep the allpass break frequency around the centre by +/- depth.
        const float minHz = juce::jmax (40.0f, p.centreHz * (1.0f - 0.9f * p.depth));
        const float maxHz = juce::jmin (0.45f * (float) sampleRate,
                                        p.centreHz * (1.0f + 2.0f * p.depth));
        const float fc = minHz + (maxHz - minHz) * lfo;
        const float t = std::tan (juce::MathConstants<float>::pi * fc / (float) sampleRate);
        const float a = (t - 1.0f) / (t + 1.0f);   // first-order allpass coeff

        const int stages = juce::jlimit (1, maxStages, p.stages);
        float x = in + st.fbLast * juce::jlimit (-0.95f, 0.95f, p.feedback);
        for (int s = 0; s < stages; ++s)
        {
            const float y = a * x + st.ap[s];
            st.ap[s] = x - a * y;
            x = y;
        }
        st.fbLast = x;
        return x;
    }

    float processFlanger (ChannelState& st, float in, float lfo, const Params& p)
    {
        const float baseMs = juce::jlimit (0.1f, 20.0f, p.manualMs);
        const float sweepMs = 0.5f + 9.0f * p.depth;
        const float delayMs = baseMs + sweepMs * lfo;
        const float delaySamps = juce::jlimit (1.0f, (float) (dlLen - 2),
                                               delayMs * 0.001f * (float) sampleRate);

        const float fb = juce::jlimit (-0.95f, 0.95f, p.feedback);
        st.dl[(size_t) st.dlWrite] = in + st.fbLast * fb;

        // Linear-interpolated read.
        float rp = (float) st.dlWrite - delaySamps;
        while (rp < 0.0f) rp += (float) dlLen;
        const int i0 = (int) rp;
        const float frac = rp - (float) i0;
        const int i1 = (i0 + 1) % dlLen;
        const float out = st.dl[(size_t) i0] + frac * (st.dl[(size_t) i1] - st.dl[(size_t) i0]);

        st.fbLast = out;
        st.dlWrite = (st.dlWrite + 1) % dlLen;
        return out;
    }

    double sampleRate = 48000.0;
    int dlLen = 0;   // flanger delay-line capacity in samples, set by prepare()
    float lfoPhase = 0.0f;
    bool wasEnabled = false;
    ChannelState channels[2];
};

} // namespace spa::dsp
