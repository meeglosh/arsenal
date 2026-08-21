#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <vector>
#include <cmath>

namespace spa::dsp
{

// 4-line Feedback Delay Network reverb with input diffusion, per-line damping
// and modulation, pre-delay, output tone, width, and mode voicings (hall /
// plate / chamber / room / spring). A unitary (Hadamard) feedback matrix keeps
// it dense and stable; per-line decay gains set the RT60.
class FDNReverb
{
public:
    enum class Mode { hall, plate, chamber, room, spring };

    void prepare (double sr, int /*maxBlock*/)
    {
        sampleRate = sr;
        const int maxLine = (int) (0.12 * sr) + 8;   // longest base * max size + mod
        for (int i = 0; i < N; ++i)
            line[(size_t) i].assign ((size_t) maxLine, 0.0f);
        preBuf.assign ((size_t) (0.2 * sr) + 8, 0.0f);
        for (int i = 0; i < numAP; ++i)
            apBuf[(size_t) i].assign ((size_t) (apBaseLen[i] * (sr / 44100.0)) + 8, 0.0f);
        reset();
    }

    void reset()
    {
        for (auto& b : line)  std::fill (b.begin(), b.end(), 0.0f);
        for (auto& b : apBuf) std::fill (b.begin(), b.end(), 0.0f);
        std::fill (preBuf.begin(), preBuf.end(), 0.0f);
        lineW = {}; apW = {}; preW = 0;
        damp = {}; lfo = {};
        lowState = {}; highState = {};
        for (size_t i = 0; i < (size_t) N; ++i) lfo[i] = (float) i / (float) N;
    }

    struct Params
    {
        int mode = 0;
        float preDelayMs = 20.0f;
        float size = 0.5f;         // 0..1 room size (delay scale)
        float decaySec = 2.0f;     // RT60
        float hfDamp = 0.5f;       // 0..1
        float modDepth = 0.2f;     // 0..1 tail modulation
        float lowCutHz = 20.0f;
        float highCutHz = 12000.0f;
        float width = 1.0f;        // 0..1
        float mix = 0.3f;          // dry/wet (equal-power)
    };

    void process (juce::AudioBuffer<float>& buffer, const Params& p)
    {
        const int n = buffer.getNumSamples();
        const int numCh = juce::jmin (2, buffer.getNumChannels());

        // Mode voicing tweaks on top of the user params.
        const auto mode = (Mode) juce::jlimit (0, 4, p.mode);
        const float sizeScale = 0.3f + 1.7f * juce::jlimit (0.0f, 1.0f, p.size)
                              * modeSizeMul (mode);
        const float rt60 = juce::jmax (0.15f, p.decaySec * modeDecayMul (mode));
        const float dampC = juce::jlimit (0.02f, 0.95f, p.hfDamp * modeDampMul (mode));
        const float modAmt = juce::jlimit (0.0f, 1.0f, p.modDepth) * modeModMul (mode);

        int preSamps = juce::jlimit (0, (int) preBuf.size() - 2,
                                     (int) (p.preDelayMs * 0.001f * (float) sampleRate));

        // Delay lengths + decay gains.
        int len[N]; float g[N];
        for (int i = 0; i < N; ++i)
        {
            len[i] = juce::jlimit (4, (int) line[(size_t) i].size() - 4,
                                   (int) (baseLenMs[i] * sizeScale * 0.001f * (float) sampleRate));
            g[i] = std::pow (10.0f, -3.0f * ((float) len[i] / (float) sampleRate) / rt60);
        }

        const float modInc = (0.4f + 0.6f * (float) mode) / (float) sampleRate;   // slow, per-mode
        const float lowCoef = onePoleCoef (p.lowCutHz);
        const float highCoef = onePoleCoef (juce::jmax (500.0f, p.highCutHz));
        const float theta = juce::jlimit (0.0f, 1.0f, p.mix) * juce::MathConstants<float>::halfPi;
        const float dryG = std::cos (theta), wetG = std::sin (theta);
        const float width = juce::jlimit (0.0f, 1.0f, p.width);

        // Tail-modulation LFOs: previously std::sin() was called once per
        // delay line per sample. Instead, seed a unit-vector rotator per
        // line from its (fixed, per-line) starting phase once per block,
        // then advance it each sample via the standard 2x2 rotation
        // recurrence (one multiply-add pair per line per sample instead of
        // a transcendental call). Numerically identical to the old
        // std::sin((lfo[i] + modInc*s) * twoPi) up to fp rounding, since
        // the rotation increment (modInc*twoPi) is the same for every line.
        const float modAngleInc = modInc * juce::MathConstants<float>::twoPi;
        const float cosInc = std::cos (modAngleInc), sinInc = std::sin (modAngleInc);
        float lfoRe[N], lfoIm[N];
        for (int i = 0; i < N; ++i)
        {
            const float phase0 = lfo[(size_t) i] * juce::MathConstants<float>::twoPi;
            lfoRe[i] = std::cos (phase0);
            lfoIm[i] = std::sin (phase0);
        }

        float* L = buffer.getWritePointer (0);
        float* R = numCh > 1 ? buffer.getWritePointer (1) : L;

        for (int s = 0; s < n; ++s)
        {
            const float dryL = L[s], dryR = R[s];
            const float in = 0.5f * (dryL + dryR);

            // Pre-delay.
            preBuf[(size_t) preW] = in;
            int pr = preW - preSamps; if (pr < 0) pr += (int) preBuf.size();
            float x = preBuf[(size_t) pr];
            preW = (preW + 1) % (int) preBuf.size();

            // Input diffusion (allpass chain).
            for (int a = 0; a < numAP; ++a)
                x = allpass (a, x, 0.6f);

            // Read the delay lines with a little modulation.
            float y[N];
            for (int i = 0; i < N; ++i)
            {
                const float m = 1.0f + 3.0f * modAmt * lfoIm[i];
                float rp = (float) lineW[(size_t) i] - ((float) len[i] - m);
                const int sz = (int) line[(size_t) i].size();
                while (rp < 0.0f) rp += (float) sz;
                const int i0 = (int) rp; const float fr = rp - (float) i0;
                const int i1 = (i0 + 1) % sz;
                y[i] = line[(size_t) i][(size_t) i0] + fr * (line[(size_t) i][(size_t) i1] - line[(size_t) i][(size_t) i0]);

                // Advance this line's rotator by the fixed per-sample angle.
                const float newRe = lfoRe[i] * cosInc - lfoIm[i] * sinInc;
                const float newIm = lfoRe[i] * sinInc + lfoIm[i] * cosInc;
                lfoRe[i] = newRe; lfoIm[i] = newIm;
            }

            // Cheap periodic renormalization: guards against fp drift on the
            // unit circle for unusually large block sizes (e.g. offline
            // bounce with one huge callback). Not needed for normal
            // realtime block sizes -- each process() call already reseeds
            // the rotator from lfo[i] via std::cos/sin above, which is a
            // natural per-block resync -- but the check is a single branch
            // per sample so it costs nothing to leave in as a safety net.
            if ((s & 4095) == 4095)
            {
                for (int i = 0; i < N; ++i)
                {
                    const float invMag = 1.0f / std::sqrt (lfoRe[i] * lfoRe[i] + lfoIm[i] * lfoIm[i]);
                    lfoRe[i] *= invMag; lfoIm[i] *= invMag;
                }
            }

            // Damp, Hadamard mix, decay, write back.
            float d[N];
            for (int i = 0; i < N; ++i)
            {
                damp[(size_t) i] += dampC * (y[i] - damp[(size_t) i]);
                d[i] = damp[(size_t) i];
            }
            const float h0 = 0.5f * (d[0] + d[1] + d[2] + d[3]);
            const float h1 = 0.5f * (d[0] - d[1] + d[2] - d[3]);
            const float h2 = 0.5f * (d[0] + d[1] - d[2] - d[3]);
            const float h3 = 0.5f * (d[0] - d[1] - d[2] + d[3]);
            const float fb[N] = { g[0] * h0, g[1] * h1, g[2] * h2, g[3] * h3 };
            // 1/sqrt(N) injection (N=4 -> 0.5) + tap normalization below keep the
            // wet path near unity loudness; decay math (g/RT60) is untouched.
            const float xIn = x * 0.5f;
            for (int i = 0; i < N; ++i)
            {
                line[(size_t) i][(size_t) lineW[(size_t) i]] = xIn + fb[i];
                lineW[(size_t) i] = (lineW[(size_t) i] + 1) % (int) line[(size_t) i].size();
            }

            // Output: decorrelated L/R, tone, width, equal-power mix.
            float wetL = 0.5f * (y[0] + y[2]);
            float wetR = 0.5f * (y[1] + y[3]);

            lowState[0] += lowCoef * (wetL - lowState[0]); wetL -= lowState[0];   // low cut
            lowState[1] += lowCoef * (wetR - lowState[1]); wetR -= lowState[1];
            highState[0] += highCoef * (wetL - highState[0]); wetL = highState[0]; // high cut
            highState[1] += highCoef * (wetR - highState[1]); wetR = highState[1];

            const float mid = 0.5f * (wetL + wetR);
            const float side = 0.5f * (wetL - wetR) * width;
            wetL = mid + side; wetR = mid - side;

            L[s] = dryL * dryG + wetL * wetG;
            if (numCh > 1) R[s] = dryR * dryG + wetR * wetG;
        }
    }

private:
    static constexpr int N = 4;
    static constexpr int numAP = 4;
    static constexpr float baseLenMs[N] = { 23.7f, 29.3f, 37.1f, 43.9f };
    static constexpr float apBaseLen[numAP] = { 142.0f, 107.0f, 379.0f, 277.0f };

    float allpass (int a, float in, float fb)
    {
        auto& b = apBuf[(size_t) a];
        const int sz = (int) b.size();
        const int len = juce::jlimit (2, sz - 2,
                                      (int) (apBaseLen[a] * (float) (sampleRate / 44100.0)));
        int rp = apW[(size_t) a] - len; if (rp < 0) rp += sz;
        const float buffered = b[(size_t) rp];
        const float out = -in + buffered;
        b[(size_t) apW[(size_t) a]] = in + buffered * fb;
        apW[(size_t) a] = (apW[(size_t) a] + 1) % sz;
        return out;
    }

    float onePoleCoef (float hz) const
    {
        return juce::jlimit (0.0001f, 0.999f,
                             1.0f - std::exp (-juce::MathConstants<float>::twoPi
                                              * hz / (float) sampleRate));
    }

    static float modeSizeMul (Mode m) { return m == Mode::hall ? 1.3f : m == Mode::room ? 0.6f : m == Mode::plate ? 0.8f : 1.0f; }
    static float modeDecayMul (Mode m) { return m == Mode::hall ? 1.2f : m == Mode::room ? 0.5f : m == Mode::plate ? 0.9f : m == Mode::spring ? 0.7f : 1.0f; }
    static float modeDampMul (Mode m) { return m == Mode::plate ? 0.6f : m == Mode::room ? 1.2f : 1.0f; }
    static float modeModMul (Mode m) { return m == Mode::plate ? 0.5f : m == Mode::spring ? 2.0f : 1.0f; }

    double sampleRate = 48000.0;
    std::array<std::vector<float>, N> line;
    std::array<std::vector<float>, numAP> apBuf;
    std::vector<float> preBuf;
    std::array<int, N> lineW {};
    std::array<int, numAP> apW {};
    int preW = 0;
    std::array<float, N> damp {}, lfo {};
    std::array<float, 2> lowState {}, highState {};
};

} // namespace spa::dsp
