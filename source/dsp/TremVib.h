#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

namespace spa::dsp
{

// Independent Tremolo (amplitude mod, with stereo phase offset) and Vibrato
// (pitch mod via a modulated short delay). Either or both can run. Rates arrive
// already resolved to Hz from the FXChain.
class TremVib
{
public:
    void prepare (double sr, int /*maxBlock*/) { sampleRate = sr; reset(); }

    void reset()
    {
        tremPhase = 0.0f;
        vibPhase = 0.0f;
        for (auto& ch : vibDelay) { for (auto& s : ch) s = 0.0f; }
        vibWrite = 0;
        vibWasOn = false;
    }

    struct Params
    {
        bool tremOn = false;
        float tremRateHz = 5.0f;
        float tremDepth = 0.5f;
        int tremShape = 0;      // 0 sine, 1 tri, 2 square, 3 saw
        float tremStereo = 0.0f;   // L/R LFO phase offset (0..1)
        float tremMix = 1.0f;

        bool vibOn = false;
        float vibRateHz = 5.0f;
        float vibDepth = 0.5f;      // 0..1 -> delay sweep
        float vibMix = 1.0f;
    };

    void process (juce::AudioBuffer<float>& buffer, const Params& p)
    {
        const int n = buffer.getNumSamples();
        const int numCh = juce::jmin (2, buffer.getNumChannels());
        const float tremInc = (float) (p.tremRateHz / sampleRate);
        const float vibInc  = (float) (p.vibRateHz / sampleRate);
        const float stereo  = 0.5f * juce::jlimit (0.0f, 1.0f, p.tremStereo);
        const float sweepSamps = juce::jlimit (0.0f, (float) (vibMax - 2),
                                               p.vibDepth * 0.006f * (float) sampleRate);

        // Non-recursive (no feedback into the write side), so a stale
        // vibDelay buffer left over from before vibrato was switched off is
        // bounded to one ~21 ms delay-line's worth of old (possibly loud)
        // audio replaying once on re-enable — a brief echo, not a decaying
        // blast like the EQ/mod feedback cases. Still cheap to clear on the
        // false->true edge (a couple KB, once per toggle), so do it anyway
        // for a clean re-enable; p.vibOn is constant for this whole call so
        // the edge check belongs outside the per-sample loop.
        if (p.vibOn && ! vibWasOn)
            for (auto& ch : vibDelay) for (auto& s : ch) s = 0.0f;
        vibWasOn = p.vibOn;

        for (int i = 0; i < n; ++i)
        {
            for (int ch = 0; ch < numCh; ++ch)
            {
                float* d = buffer.getWritePointer (ch);
                float x = d[i];

                if (p.tremOn)
                {
                    const float ph = tremPhase + (ch == 1 ? stereo : 0.0f);
                    const float lfo = shapeVal (p.tremShape, ph);          // 0..1
                    const float gain = 1.0f - p.tremDepth * (1.0f - lfo);
                    x = x * (1.0f - p.tremMix + p.tremMix * gain);
                }

                if (p.vibOn)
                {
                    auto& line = vibDelay[(size_t) ch];
                    line[(size_t) vibWrite] = x;
                    const float lfo = shapeVal (0, vibPhase);              // sine
                    const float delay = 1.0f + sweepSamps * lfo;
                    float rp = (float) vibWrite - delay;
                    while (rp < 0.0f) rp += (float) vibMax;
                    int i0 = (int) rp;
                    const float frac = rp - (float) i0;
                    while (i0 >= vibMax) i0 -= vibMax;   // float wrap can round to exactly vibMax -- see FDNReverb.h
                    const int i1 = (i0 + 1) % vibMax;
                    const float wet = line[(size_t) i0] + frac * (line[(size_t) i1] - line[(size_t) i0]);
                    x = x * (1.0f - p.vibMix) + wet * p.vibMix;
                }

                d[i] = x;
            }

            tremPhase += tremInc; if (tremPhase >= 1.0f) tremPhase -= 1.0f;
            vibPhase  += vibInc;  if (vibPhase  >= 1.0f) vibPhase  -= 1.0f;
            if (p.vibOn) vibWrite = (vibWrite + 1) % vibMax;
        }
    }

private:
    static float shapeVal (int shape, float phase)   // -> 0..1
    {
        phase -= std::floor (phase);
        switch (shape)
        {
            case 1:  return 1.0f - std::abs (2.0f * phase - 1.0f);         // triangle
            case 2:  return phase < 0.5f ? 1.0f : 0.0f;                    // square
            case 3:  return phase;                                        // saw
            default: return 0.5f + 0.5f * std::sin (phase * juce::MathConstants<float>::twoPi);
        }
    }

    static constexpr int vibMax = 1024;   // ~21 ms at 48k
    double sampleRate = 48000.0;
    float tremPhase = 0.0f, vibPhase = 0.0f;
    float vibDelay[2][vibMax] {};
    int vibWrite = 0;
    bool vibWasOn = false;
};

} // namespace spa::dsp
