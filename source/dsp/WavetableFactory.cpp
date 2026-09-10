#include "WavetableFactory.h"

#include <cmath>

namespace spa::dsp
{

namespace
{
    constexpr double pi = juce::MathConstants<double>::pi;

    // Deterministic pseudo-random value in [-1, 1), stable across runs (no
    // seeding/state) so table generation is reproducible for tests and for
    // identical builds across machines.
    float hash (int a, int b, int c) noexcept
    {
        const float x = std::sin ((float) a * 12.9898f + (float) b * 78.233f + (float) c * 37.719f)
                       * 43758.5453f;
        return 2.0f * (x - std::floor (x)) - 1.0f;
    }

    void addPartial (std::vector<float>& frame, double freqCycles, float amplitude, float phase)
    {
        const auto n = (int) frame.size();
        for (int i = 0; i < n; ++i)
            frame[(size_t) i] += amplitude * (float) std::sin (2.0 * pi * freqCycles * (double) i / n + phase);
    }

    // ---- time-domain generators (need real phase control) -----------------

    Wavetable buildSupersaw()
    {
        constexpr int numFrames = 48;
        constexpr int numHarmonics = 40;
        const int n = Wavetable::tableSize;

        std::vector<float> audio;
        audio.reserve ((size_t) numFrames * (size_t) n);

        for (int f = 0; f < numFrames; ++f)
        {
            const float smear = (float) f / (float) (numFrames - 1);
            std::vector<float> frame ((size_t) n, 0.0f);

            for (int k = 1; k <= numHarmonics; ++k)
            {
                const float base = 1.0f / (float) k;
                // Phase dispersion + slight amplitude jitter that grows with
                // frame index -- a static (unison-1) approximation of a
                // JP-8000-style detuned saw stack; the real detuning comes
                // from the oscillator's own unison knobs on top of this.
                const float jitterPhase = smear * (0.15f + 0.02f * (float) k) * hash (k, f, 1);
                const float jitterAmp = 1.0f + smear * 0.25f * hash (k, f, 2);
                addPartial (frame, (double) k, base * jitterAmp, jitterPhase);
            }

            audio.insert (audio.end(), frame.begin(), frame.end());
        }

        return Wavetable::fromAudioFrames ("Supersaw", audio.data(), (int) audio.size(), n);
    }

    Wavetable buildSyncSweep()
    {
        constexpr int numFrames = 48;
        const int n = Wavetable::tableSize;

        std::vector<float> audio;
        audio.reserve ((size_t) numFrames * (size_t) n);

        for (int f = 0; f < numFrames; ++f)
        {
            const double smear = (double) f / (double) (numFrames - 1);
            const double ratio = 1.0 + 3.0 * smear;   // sync ratio sweeps 1.0 -> 4.0
            std::vector<float> frame ((size_t) n, 0.0f);

            for (int i = 0; i < n; ++i)
            {
                // Hard sync: the slave sawtooth resets to phase 0 every time
                // the master (one cycle per frame) resets, but runs `ratio`
                // times faster in between -- the classic buzzy sync waveform.
                const double slavePhase = std::fmod (ratio * (double) i / (double) n, 1.0);
                frame[(size_t) i] = (float) (2.0 * slavePhase - 1.0);
            }

            audio.insert (audio.end(), frame.begin(), frame.end());
        }

        return Wavetable::fromAudioFrames ("Sync Sweep", audio.data(), (int) audio.size(), n);
    }

    // ---- harmonic-recipe generators (magnitude spectra via fromHarmonics) -

    Wavetable buildPwm()
    {
        constexpr int numFrames = 48;
        constexpr int maxHarmonics = 200;

        std::vector<std::vector<float>> frames;
        frames.reserve (numFrames);

        for (int f = 0; f < numFrames; ++f)
        {
            const float smear = (float) f / (float) (numFrames - 1);
            const float w = 0.5f - 0.45f * smear;   // pulse width 50% -> 5%

            std::vector<float> h ((size_t) maxHarmonics, 0.0f);
            for (int k = 1; k <= maxHarmonics; ++k)
                h[(size_t) (k - 1)] = 2.0f * (float) std::sin (pi * (double) k * (double) w) / (float) (pi * k);

            frames.push_back (std::move (h));
        }

        return Wavetable::fromHarmonics ("PWM", frames);
    }

    struct FormantSet { float f1, f2, f3; };

    FormantSet lerpFormant (const FormantSet& a, const FormantSet& b, float t)
    {
        return { a.f1 + (b.f1 - a.f1) * t, a.f2 + (b.f2 - a.f2) * t, a.f3 + (b.f3 - a.f3) * t };
    }

    Wavetable buildFormant()
    {
        constexpr int numFrames = 48;
        constexpr int maxHarmonics = 90;
        constexpr float fundamentalHz = 110.0f;   // nominal, for bin->Hz mapping only
        constexpr float bandwidthHz = 120.0f;

        // A -> E -> I -> O -> U, evenly spaced breakpoints across the frames.
        constexpr FormantSet vowelA { 700.0f, 1220.0f, 2600.0f };
        constexpr FormantSet vowelE { 530.0f, 1840.0f, 2480.0f };
        constexpr FormantSet vowelI { 270.0f, 2290.0f, 3010.0f };
        constexpr FormantSet vowelO { 570.0f, 840.0f, 2410.0f };
        constexpr FormantSet vowelU { 300.0f, 870.0f, 2240.0f };
        const FormantSet vowels[] { vowelA, vowelE, vowelI, vowelO, vowelU };
        constexpr int breakpoints[] { 0, 11, 23, 35, 47 };

        std::vector<std::vector<float>> frames;
        frames.reserve (numFrames);

        for (int f = 0; f < numFrames; ++f)
        {
            int seg = 0;
            while (seg < 3 && f > breakpoints[seg + 1])
                ++seg;
            const float span = (float) (breakpoints[seg + 1] - breakpoints[seg]);
            const float t = span > 0.0f ? (float) (f - breakpoints[seg]) / span : 0.0f;
            const auto fm = lerpFormant (vowels[seg], vowels[seg + 1], t);

            std::vector<float> h ((size_t) maxHarmonics, 0.0f);
            for (int k = 1; k <= maxHarmonics; ++k)
            {
                const float freqHz = fundamentalHz * (float) k;
                const auto bump = [freqHz] (float centre, float gain)
                {
                    const float d = (freqHz - centre) / bandwidthHz;
                    return gain * std::exp (-0.5f * d * d);
                };
                const float envelope = bump (fm.f1, 1.0f) + bump (fm.f2, 0.7f) + bump (fm.f3, 0.4f);
                h[(size_t) (k - 1)] = (1.0f / (float) k) * (0.2f + envelope);
            }

            frames.push_back (std::move (h));
        }

        return Wavetable::fromHarmonics ("Formant", frames);
    }

    Wavetable buildAdditive()
    {
        constexpr int numFrames = 48;
        constexpr int maxHarmonics = 64;
        constexpr int breakpoints[] { 0, 11, 23, 35, 47 };

        // Five harmonic recipes, keyframed across the frame range.
        std::vector<std::vector<float>> recipes (5, std::vector<float> ((size_t) maxHarmonics, 0.0f));
        for (int k = 1; k <= maxHarmonics; ++k)
        {
            const auto i = (size_t) (k - 1);
            recipes[0][i] = (k % 2 != 0) ? 1.0f / (float) k : 0.0f;                       // odd-only
            recipes[1][i] = 1.0f / (float) k;                                             // all 1/n
            recipes[2][i] = 1.0f / (float) (k * k);                                       // 1/n^2
            recipes[3][i] = (k % 2 == 0) ? 1.5f / (float) k : 0.3f / (float) k;           // even-emphasis
            recipes[4][i] = (k % 6 == 1) ? 1.0f / std::sqrt ((float) k) : 0.02f / (float) k; // sparse highs
        }

        std::vector<std::vector<float>> frames;
        frames.reserve (numFrames);

        for (int f = 0; f < numFrames; ++f)
        {
            int seg = 0;
            while (seg < 3 && f > breakpoints[seg + 1])
                ++seg;
            const float span = (float) (breakpoints[seg + 1] - breakpoints[seg]);
            const float t = span > 0.0f ? (float) (f - breakpoints[seg]) / span : 0.0f;

            std::vector<float> h ((size_t) maxHarmonics, 0.0f);
            for (int k = 0; k < maxHarmonics; ++k)
                h[(size_t) k] = recipes[(size_t) seg][(size_t) k]
                              + (recipes[(size_t) seg + 1][(size_t) k] - recipes[(size_t) seg][(size_t) k]) * t;

            frames.push_back (std::move (h));
        }

        return Wavetable::fromHarmonics ("Additive", frames);
    }

    Wavetable buildUnisonSpread()
    {
        constexpr int numFrames = 48;
        constexpr int maxHarmonics = 60;

        std::vector<std::vector<float>> frames;
        frames.reserve (numFrames);

        for (int f = 0; f < numFrames; ++f)
        {
            const float smear = (float) f / (float) (numFrames - 1);
            const float sideWeight = 0.5f * smear;   // grows 0 -> 0.5 across the table

            std::vector<float> h ((size_t) maxHarmonics, 0.0f);
            for (int k = 1; k <= maxHarmonics; ++k)
            {
                const float base = 1.0f / (float) k;
                // Alternating phase per harmonic (negative amplitude flips
                // phase 180 degrees -- fromHarmonics assigns fixed sine phase
                // per bin, so sign is the only phase control available).
                const float sign = (k % 2 == 0) ? -1.0f : 1.0f;

                h[(size_t) (k - 1)] += base * (1.0f - sideWeight);
                if (k - 1 >= 1)
                    h[(size_t) (k - 2)] += base * sideWeight * 0.5f * sign;
                if (k + 1 <= maxHarmonics)
                    h[(size_t) k] += base * sideWeight * 0.5f * sign;
            }

            frames.push_back (std::move (h));
        }

        return Wavetable::fromHarmonics ("Unison Spread", frames);
    }

    Wavetable buildBells()
    {
        constexpr int numFrames = 40;
        constexpr int maxHarmonics = 40;

        // Inharmonic bell-like partial ratios, quantised to the nearest
        // integer harmonic bin (fromHarmonics only supports integer
        // harmonics of the fundamental).
        constexpr float ratios[] { 1.0f, 2.76f, 5.4f, 8.93f, 13.34f, 18.64f };
        constexpr float weights[] { 1.0f, 0.6f, 0.35f, 0.2f, 0.1f, 0.05f };
        constexpr int numPartials = (int) std::size (ratios);

        std::vector<std::vector<float>> frames;
        frames.reserve (numFrames);

        for (int f = 0; f < numFrames; ++f)
        {
            const float brightness = (float) f / (float) (numFrames - 1);

            std::vector<float> h ((size_t) maxHarmonics, 0.0f);
            for (int j = 0; j < numPartials; ++j)
            {
                const int idx = juce::jlimit (1, maxHarmonics, juce::roundToInt (ratios[j]));
                const float ampScale = (j == 0) ? 1.0f : (0.2f + 0.8f * brightness);
                h[(size_t) (idx - 1)] += weights[j] * ampScale;
            }

            frames.push_back (std::move (h));
        }

        return Wavetable::fromHarmonics ("Bells", frames);
    }
}

const juce::StringArray& wavetableTableChoiceNames()
{
    static const juce::StringArray names {
        "Basic Shapes", "Supersaw", "PWM", "Formant",
        "Additive", "Unison Spread", "Sync Sweep", "Bells",
    };
    jassert (names.size() == numWavetableTableChoices);
    return names;
}

Wavetable WavetableFactory::build (int tableChoice)
{
    const auto choice = (WavetableTableChoice) juce::jlimit (0, numWavetableTableChoices - 1, tableChoice);

    switch (choice)
    {
        case WavetableTableChoice::supersaw:      return buildSupersaw();
        case WavetableTableChoice::pwm:           return buildPwm();
        case WavetableTableChoice::formant:       return buildFormant();
        case WavetableTableChoice::additive:      return buildAdditive();
        case WavetableTableChoice::unisonSpread:  return buildUnisonSpread();
        case WavetableTableChoice::syncSweep:     return buildSyncSweep();
        case WavetableTableChoice::bells:         return buildBells();
        case WavetableTableChoice::basicShapes:
        case WavetableTableChoice::count:
        default:                                  return Wavetable::createBasicShapes();
    }
}

} // namespace spa::dsp
