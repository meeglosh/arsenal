#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <complex>
#include <cmath>

namespace spa::dsp
{

// 8-band parametric EQ. Hand-rolled Direct-Form-II-transposed biquads so
// coefficient updates are pure arithmetic (no ReferenceCountedObject alloc on
// the audio thread, unlike juce::dsp::IIR). Per-band RBJ cookbook coefficients;
// an optional post-EQ "character" saturation stage colours the output. The
// magnitude response is exposed as a static function so the UI curve and the
// DSP agree exactly.
class ParametricEQ
{
public:
    static constexpr int numBands = 8;

    enum class Type { bell, lowShelf, highShelf, lowCut, highCut, notch };
    enum class Character { clean, modern, vintage, tube };

    struct Band
    {
        bool enabled = false;
        int type = 0;          // Type
        float freq = 1000.0f;
        float gainDb = 0.0f;
        float q = 0.707f;
    };

    void prepare (double sr, int /*maxBlock*/)
    {
        sampleRate = sr;
        reset();
        for (auto& b : cached) b = {};
        for (auto& b : cached) b.freq = -1.0f;   // force first recompute
    }

    void reset()
    {
        for (auto& band : state)
            for (auto& ch : band)
                ch = { 0.0f, 0.0f };
    }

    void setCharacter (int c) { character = (Character) juce::jlimit (0, 3, c); }

    // Recompute only the bands whose params changed. Pure float math.
    void updateBands (const std::array<Band, numBands>& bands)
    {
        for (int i = 0; i < numBands; ++i)
        {
            const auto& b = bands[(size_t) i];
            auto& c = cached[(size_t) i];
            // A disabled band's biquad state (z1/z2) keeps accumulating whatever
            // energy was ringing in it the instant it was skipped in process()
            // (see the `if (! active[i]) continue;` below) — a hi-Q band left
            // loud when disabled dumps that trapped energy back out on re-
            // enable. Clear it on the false->true edge, before the new active
            // flag takes effect, so re-enabling always starts from silence.
            if (b.enabled && ! active[(size_t) i])
                for (auto& ch : state[(size_t) i])
                    ch = { 0.0f, 0.0f };
            active[(size_t) i] = b.enabled;
            if (b.enabled
                && (b.type != c.type
                    || ! juce::approximatelyEqual (b.freq, c.freq)
                    || ! juce::approximatelyEqual (b.gainDb, c.gainDb)
                    || ! juce::approximatelyEqual (b.q, c.q)))
            {
                computeCoeffs (b, sampleRate, coeffs[(size_t) i].data());
                c = b;
            }
        }
    }

    void process (juce::AudioBuffer<float>& buffer)
    {
        const int n = buffer.getNumSamples();
        const int numCh = juce::jmin (2, buffer.getNumChannels());
        const float drive = characterDrive();

        for (int ch = 0; ch < numCh; ++ch)
        {
            float* d = buffer.getWritePointer (ch);
            for (int s = 0; s < n; ++s)
            {
                float x = d[s];
                for (int i = 0; i < numBands; ++i)
                {
                    if (! active[(size_t) i]) continue;
                    const auto& co = coeffs[(size_t) i];
                    auto& z = state[(size_t) i][(size_t) ch];
                    const float y = co[0] * x + z[0];
                    z[0] = co[1] * x - co[3] * y + z[1];
                    z[1] = co[2] * x - co[4] * y;
                    x = y;
                }
                d[s] = drive > 0.0f ? saturate (x, drive) : x;
            }
        }
    }

    // Magnitude of the whole active cascade at `freq`, in dB. Static so the UI
    // draws exactly what the DSP does. `char_` selects a broad tilt hint for the
    // saturation modes (the curve is otherwise the linear filter response).
    static float magnitudeDb (const std::array<Band, numBands>& bands,
                              float freq, double sr)
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * freq / sr;
        const std::complex<double> z1 = std::polar (1.0, -w);
        const std::complex<double> z2 = std::polar (1.0, -2.0 * w);
        double magDb = 0.0;
        for (const auto& b : bands)
        {
            if (! b.enabled) continue;
            double c[5];
            computeCoeffs (b, sr, c);
            const std::complex<double> num = c[0] + c[1] * z1 + c[2] * z2;
            const std::complex<double> den = 1.0 + c[3] * z1 + c[4] * z2;
            magDb += 20.0 * std::log10 (std::max (1.0e-9, std::abs (num / den)));
        }
        return (float) magDb;
    }

private:
    // RBJ audio-EQ cookbook, normalised so a0 = 1. Works for float or double out.
    template <typename T>
    static void computeCoeffs (const Band& b, double sr, T* out)
    {
        const double w0 = 2.0 * juce::MathConstants<double>::pi
                        * juce::jlimit (10.0, sr * 0.49, (double) b.freq) / sr;
        const double cw = std::cos (w0), sw = std::sin (w0);
        const double q = juce::jmax (0.05, (double) b.q);
        const double alpha = sw / (2.0 * q);
        const double A = std::pow (10.0, (double) b.gainDb / 40.0);
        const double sqA = std::sqrt (A);

        double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
        switch ((Type) b.type)
        {
            case Type::bell:
                b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A;
                a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A; break;
            case Type::lowShelf:
                b0 =     A * ((A + 1) - (A - 1) * cw + 2 * sqA * alpha);
                b1 = 2 * A * ((A - 1) - (A + 1) * cw);
                b2 =     A * ((A + 1) - (A - 1) * cw - 2 * sqA * alpha);
                a0 =         (A + 1) + (A - 1) * cw + 2 * sqA * alpha;
                a1 =    -2 * ((A - 1) + (A + 1) * cw);
                a2 =         (A + 1) + (A - 1) * cw - 2 * sqA * alpha; break;
            case Type::highShelf:
                b0 =      A * ((A + 1) + (A - 1) * cw + 2 * sqA * alpha);
                b1 = -2 * A * ((A - 1) + (A + 1) * cw);
                b2 =      A * ((A + 1) + (A - 1) * cw - 2 * sqA * alpha);
                a0 =          (A + 1) - (A - 1) * cw + 2 * sqA * alpha;
                a1 =      2 * ((A - 1) - (A + 1) * cw);
                a2 =          (A + 1) - (A - 1) * cw - 2 * sqA * alpha; break;
            case Type::lowCut:
                b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2;
                a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
            case Type::highCut:
                b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2;
                a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
            case Type::notch:
                b0 = 1; b1 = -2 * cw; b2 = 1;
                a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
        }
        out[0] = (T) (b0 / a0); out[1] = (T) (b1 / a0); out[2] = (T) (b2 / a0);
        out[3] = (T) (a1 / a0); out[4] = (T) (a2 / a0);
    }

    float characterDrive() const
    {
        switch (character)
        {
            case Character::clean:   return 0.0f;
            case Character::modern:  return 0.15f;
            case Character::vintage: return 0.5f;
            case Character::tube:    return 0.9f;
        }
        return 0.0f;
    }

    static float saturate (float x, float drive)
    {
        // Gentle asymmetric-ish soft clip; normalised so low levels pass ~unity.
        const float k = 1.0f + 3.0f * drive;
        return std::tanh (k * x) / std::tanh (k * 0.9f) * 0.9f;
    }

    double sampleRate = 48000.0;
    Character character = Character::clean;

    std::array<std::array<float, 5>, numBands> coeffs {};
    std::array<std::array<std::array<float, 2>, 2>, numBands> state {};
    std::array<bool, numBands> active {};
    std::array<Band, numBands> cached {};
};

} // namespace spa::dsp
