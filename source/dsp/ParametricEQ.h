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
//
// Low Cut / High Cut bands can cascade up to 4 biquad/1st-order sections for
// steeper slopes (6/12/18/24/36/48 dB/oct); every other type is a single
// section. Tilt Shelf is two sections (a low shelf + an inverted-gain high
// shelf around the same frequency).
class ParametricEQ
{
public:
    static constexpr int numBands = 8;
    static constexpr int maxStages = 4;

    // Append-only (serialized in presets as the band "type" choice index).
    enum class Type { bell, lowShelf, highShelf, lowCut, highCut, notch, bandPass, tiltShelf };
    enum class Character { clean, modern, vintage, tube };

    // Cut-slope choice indices (append-only, serialized as the band "slope").
    enum class Slope { db6, db12, db18, db24, db36, db48 };

    struct Band
    {
        bool enabled = false;
        int type = 0;          // Type
        int slope = 1;          // Slope; index 1 = "12 dB" reproduces the pre-slope behaviour
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
            for (auto& stage : band)
                for (auto& ch : stage)
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
                for (auto& stage : state[(size_t) i])
                    for (auto& ch : stage)
                        ch = { 0.0f, 0.0f };
            active[(size_t) i] = b.enabled;
            if (b.enabled
                && (b.type != c.type
                    || b.slope != c.slope
                    || ! juce::approximatelyEqual (b.freq, c.freq)
                    || ! juce::approximatelyEqual (b.gainDb, c.gainDb)
                    || ! juce::approximatelyEqual (b.q, c.q)))
            {
                float stageCoeffs[maxStages][5];
                numStages[(size_t) i] = computeCascade (b, sampleRate, stageCoeffs);
                for (int s = 0; s < numStages[(size_t) i]; ++s)
                    for (int k = 0; k < 5; ++k)
                        coeffs[(size_t) i][(size_t) s][(size_t) k] = stageCoeffs[s][k];
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
                    const int stages = numStages[(size_t) i];
                    for (int st = 0; st < stages; ++st)
                    {
                        const auto& co = coeffs[(size_t) i][(size_t) st];
                        auto& z = state[(size_t) i][(size_t) st][(size_t) ch];
                        const float y = co[0] * x + z[0];
                        z[0] = co[1] * x - co[3] * y + z[1];
                        z[1] = co[2] * x - co[4] * y;
                        x = y;
                    }
                }
                d[s] = drive > 0.0f ? saturate (x, drive) : x;
            }
        }
    }

    // Magnitude of the whole active cascade at `freq`, in dB. Static so the UI
    // draws exactly what the DSP does.
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
            double stageCoeffs[maxStages][5];
            const int stages = computeCascade (b, sr, stageCoeffs);
            for (int s = 0; s < stages; ++s)
            {
                const auto& c = stageCoeffs[s];
                const std::complex<double> num = c[0] + c[1] * z1 + c[2] * z2;
                const std::complex<double> den = 1.0 + c[3] * z1 + c[4] * z2;
                magDb += 20.0 * std::log10 (std::max (1.0e-9, std::abs (num / den)));
            }
        }
        return (float) magDb;
    }

    // Number of biquad/1st-order sections a Low Cut / High Cut band uses for a
    // given slope choice index. Shared with the UI for the slope badge.
    static int stageCountForSlope (int slopeIdx)
    {
        switch ((Slope) juce::jlimit (0, 5, slopeIdx))
        {
            case Slope::db6:  return 1;
            case Slope::db12: return 1;
            case Slope::db18: return 2;
            case Slope::db24: return 2;
            case Slope::db36: return 3;
            case Slope::db48: return 4;
        }
        return 1;
    }

    static int slopeDbPerOct (int slopeIdx)
    {
        static constexpr int db[6] = { 6, 12, 18, 24, 36, 48 };
        return db[(size_t) juce::jlimit (0, 5, slopeIdx)];
    }

private:
    struct StageSpec { bool firstOrder; double q; };

    // Butterworth Q cascades for the cut slopes above 12 dB/oct; the last
    // (highest-Q) stage is scaled by the band's own Q knob as a resonance
    // multiplier (1.0 at the neutral Q of 0.707), so the knob still does
    // something musical at every slope.
    static int stageSpecsFor (int slopeIdx, double q, StageSpec* specs)
    {
        const double res = q / 0.707;
        switch ((Slope) juce::jlimit (0, 5, slopeIdx))
        {
            case Slope::db6:
                specs[0] = { true, 0.0 };
                return 1;
            case Slope::db12:
                // Unchanged from the pre-slope behaviour: one 2nd-order section
                // at the band's own Q directly (no resonance scaling).
                specs[0] = { false, q };
                return 1;
            case Slope::db18:
                specs[0] = { true, 0.0 };
                specs[1] = { false, 1.0 * res };
                return 2;
            case Slope::db24:
                specs[0] = { false, 0.54 };
                specs[1] = { false, 1.31 * res };
                return 2;
            case Slope::db36:
                specs[0] = { false, 0.52 };
                specs[1] = { false, 0.71 };
                specs[2] = { false, 1.93 * res };
                return 3;
            case Slope::db48:
                specs[0] = { false, 0.51 };
                specs[1] = { false, 0.60 };
                specs[2] = { false, 0.90 };
                specs[3] = { false, 2.56 * res };
                return 4;
        }
        specs[0] = { false, q };
        return 1;
    }

    template <typename T>
    static void compute1stOrder (bool highpass, double freq, double sr, T* out)
    {
        const double wc = std::tan (juce::MathConstants<double>::pi
                                   * juce::jlimit (10.0, sr * 0.49, freq) / sr);
        const double a0 = wc + 1.0;
        if (highpass)   // lowCut
        {
            out[0] = (T) (1.0 / a0);  out[1] = (T) (-1.0 / a0); out[2] = (T) 0.0;
            out[3] = (T) ((wc - 1.0) / a0); out[4] = (T) 0.0;
        }
        else   // lowpass, highCut
        {
            out[0] = (T) (wc / a0); out[1] = (T) (wc / a0); out[2] = (T) 0.0;
            out[3] = (T) ((wc - 1.0) / a0); out[4] = (T) 0.0;
        }
    }

    template <typename T>
    static void compute2ndOrderCut (bool highpass, double freq, double q, double sr, T* out)
    {
        const double w0 = 2.0 * juce::MathConstants<double>::pi
                        * juce::jlimit (10.0, sr * 0.49, freq) / sr;
        const double cw = std::cos (w0), sw = std::sin (w0);
        const double alpha = sw / (2.0 * juce::jmax (0.05, q));
        double b0, b1, b2, a0, a1, a2;
        if (highpass)
        {
            b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2;
        }
        else
        {
            b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2;
        }
        a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
        out[0] = (T) (b0 / a0); out[1] = (T) (b1 / a0); out[2] = (T) (b2 / a0);
        out[3] = (T) (a1 / a0); out[4] = (T) (a2 / a0);
    }

    // RBJ audio-EQ cookbook (bell/shelf/notch/band-pass), normalised so a0=1.
    template <typename T>
    static void computeSingle (const Band& b, double sr, T* out)
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
            case Type::notch:
                b0 = 1; b1 = -2 * cw; b2 = 1;
                a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
            case Type::bandPass:
                // Constant 0 dB peak gain form.
                b0 = alpha; b1 = 0; b2 = -alpha;
                a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
            case Type::lowCut: case Type::highCut: case Type::tiltShelf:
                jassertfalse; break;   // handled by computeCascade
        }
        out[0] = (T) (b0 / a0); out[1] = (T) (b1 / a0); out[2] = (T) (b2 / a0);
        out[3] = (T) (a1 / a0); out[4] = (T) (a2 / a0);
    }

    // Fills up to maxStages sections; returns how many are active.
    template <typename T>
    static int computeCascade (const Band& b, double sr, T stageOut[][5])
    {
        switch ((Type) b.type)
        {
            case Type::lowCut:
            case Type::highCut:
            {
                const bool highpass = (Type) b.type == Type::lowCut;
                StageSpec specs[maxStages];
                const int n = stageSpecsFor (b.slope, (double) b.q, specs);
                for (int s = 0; s < n; ++s)
                {
                    if (specs[s].firstOrder)
                        compute1stOrder (highpass, (double) b.freq, sr, stageOut[s]);
                    else
                        compute2ndOrderCut (highpass, (double) b.freq, specs[s].q, sr, stageOut[s]);
                }
                return n;
            }
            case Type::tiltShelf:
            {
                // Positive gain tilts bright (boosts highs, cuts lows), like a
                // Pro-Q tilt shelf: low shelf gets -gain/2, high shelf +gain/2,
                // pivoting through `freq`.
                Band lo = b; lo.type = (int) Type::lowShelf;  lo.gainDb = -b.gainDb * 0.5f;
                Band hi = b; hi.type = (int) Type::highShelf; hi.gainDb =  b.gainDb * 0.5f;
                computeSingle (lo, sr, stageOut[0]);
                computeSingle (hi, sr, stageOut[1]);
                return 2;
            }
            case Type::bell:
            case Type::lowShelf:
            case Type::highShelf:
            case Type::notch:
            case Type::bandPass:
                computeSingle (b, sr, stageOut[0]);
                return 1;
        }
        jassertfalse;   // every Type is handled above; unreachable, kept for -Wreturn-type
        return 1;
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

    std::array<std::array<std::array<float, 5>, maxStages>, numBands> coeffs {};
    std::array<std::array<std::array<std::array<float, 2>, 2>, maxStages>, numBands> state {};
    std::array<int, numBands> numStages {};
    std::array<bool, numBands> active {};
    std::array<Band, numBands> cached {};
};

} // namespace spa::dsp
