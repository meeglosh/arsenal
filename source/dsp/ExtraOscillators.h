#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>
#include <vector>

namespace spa::dsp
{

// ============================ Virtual analog ================================
// PolyBLEP band-limited saw/square/pulse; naive triangle/sine (already soft).
class AnalogOscillator
{
public:
    enum class Shape { saw, square, pulse, triangle, sine };

    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        phase = 0.0;
    }

    void noteOn() noexcept { phase = 0.0; }

    void setFrequency (float hz) noexcept
    {
        increment = juce::jlimit (0.0, 0.45, (double) hz / sampleRate);
    }

    float getNextSample (Shape shape, float pulseWidth) noexcept
    {
        const auto t = (float) phase;
        const auto dt = (float) increment;
        float value;

        switch (shape)
        {
            case Shape::saw:
                value = 2.0f * t - 1.0f - polyBlep (t, dt);
                break;

            case Shape::square:
            case Shape::pulse:
            {
                const auto width = shape == Shape::square ? 0.5f : pulseWidth;
                value = t < width ? 1.0f : -1.0f;
                value += polyBlep (t, dt);
                const auto t2 = t - width;
                value -= polyBlep (t2 - std::floor (t2), dt);
                break;
            }

            case Shape::triangle:
                value = 1.0f - 4.0f * std::abs (t - 0.5f);
                break;

            case Shape::sine:
            default:
                value = std::sin (juce::MathConstants<float>::twoPi * t);
                break;
        }

        phase += increment;
        if (phase >= 1.0)
            phase -= 1.0;

        return value;
    }

private:
    static float polyBlep (float t, float dt) noexcept
    {
        if (dt <= 0.0f)
            return 0.0f;
        if (t < dt)
        {
            const auto x = t / dt;
            return x + x - x * x - 1.0f;
        }
        if (t > 1.0f - dt)
        {
            const auto x = (t - 1.0f) / dt;
            return x * x + x + x + 1.0f;
        }
        return 0.0f;
    }

    double sampleRate = 48000.0;
    double phase = 0.0;
    double increment = 0.0;
};

// ================================ 2-op FM ===================================
class FMOscillator
{
public:
    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        carrierPhase = modPhase = 0.0;
    }

    void noteOn() noexcept { carrierPhase = modPhase = 0.0; }

    void setFrequency (float hz, float ratio) noexcept
    {
        carrierInc = juce::jlimit (0.0, 0.45, (double) hz / sampleRate);
        modInc = juce::jlimit (0.0, 0.45, (double) (hz * ratio) / sampleRate);
    }

    float getNextSample (float index) noexcept
    {
        const auto modulator = std::sin (juce::MathConstants<float>::twoPi * (float) modPhase);
        const auto value = std::sin (juce::MathConstants<float>::twoPi * (float) carrierPhase
                                     + index * modulator);

        carrierPhase += carrierInc;
        if (carrierPhase >= 1.0)
            carrierPhase -= 1.0;
        modPhase += modInc;
        if (modPhase >= 1.0)
            modPhase -= 1.0;

        return value;
    }

private:
    double sampleRate = 48000.0;
    double carrierPhase = 0.0, modPhase = 0.0;
    double carrierInc = 0.0, modInc = 0.0;
};

// ================================= Noise ====================================
class NoiseGenerator
{
public:
    enum class Color { white, pink, brown };

    void prepare() noexcept
    {
        b0 = b1 = b2 = brownState = 0.0f;
    }

    float getNextSample (Color color, juce::Random& random) noexcept
    {
        const auto white = random.nextFloat() * 2.0f - 1.0f;

        switch (color)
        {
            case Color::pink:
            {
                // Paul Kellet's economy pink filter.
                b0 = 0.99765f * b0 + white * 0.0990460f;
                b1 = 0.96300f * b1 + white * 0.2965164f;
                b2 = 0.57000f * b2 + white * 1.0526913f;
                return juce::jlimit (-1.0f, 1.0f,
                                     (b0 + b1 + b2 + white * 0.1848f) * 0.25f);
            }
            case Color::brown:
                brownState = juce::jlimit (-1.0f, 1.0f,
                                           brownState * 0.997f + white * 0.06f);
                return brownState * 3.0f;

            case Color::white:
            default:
                return white;
        }
    }

private:
    float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f, brownState = 0.0f;
};

// =============================== Pluck (KS) =================================
// Karplus-Strong plucked string: noise burst into a damped delay loop.
// The maxPeriod-sized buffer (16KB) is only ever needed by oscillator slots
// actually running in Pluck mode; most slots never touch it, and it's
// instantiated per-voice x per-slot, so allocating it unconditionally in
// prepare() left ~1MB permanently resident for synths that never use Pluck.
// Instead, prepare() only resets state; ensureAllocated() does the real
// allocation and is called lazily, from the message thread only, the first
// time a slot's mode is observed to be Pluck -- primarily synchronously from
// SPASynthProcessor::parameterChanged() (fired when the mode parameter is
// set to Pluck), with SPASynthProcessor::timerCallback() as a fallback safety
// net, both under the callback lock. noteOn() and getNextSample() both
// tolerate an unallocated buffer (silent no-op), so nothing ever allocates
// on the audio thread.
class PluckString
{
public:
    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        period = 100;
        writePos = 0;
    }

    // Message-thread only (never called from noteOn/process). Idempotent.
    void ensureAllocated()
    {
        if (buffer.empty())
            buffer.assign (maxPeriod, 0.0f);
    }

    void noteOn (float hz, juce::Random& random) noexcept
    {
        period = juce::jlimit (2, maxPeriod - 1,
                               (int) std::lround (sampleRate / juce::jmax (20.0f, hz)));
        writePos = 0;

        if (buffer.empty())
            return;   // not yet lazily allocated -- silent for this brief window

        for (int i = 0; i < period; ++i)
            buffer[(size_t) i] = random.nextFloat() * 2.0f - 1.0f;
    }

    void setFrequency (float hz) noexcept
    {
        period = juce::jlimit (2, maxPeriod - 1,
                               (int) std::lround (sampleRate / juce::jmax (20.0f, hz)));
    }

    float getNextSample (float damping) noexcept
    {
        if (buffer.empty())
            return 0.0f;

        const auto read = writePos % period;
        const auto next = (writePos + 1) % period;
        const auto value = buffer[(size_t) read];

        // Damping 0..1 -> loop feedback 0.90..0.999 (bright, long tails high).
        const auto feedback = 0.90f + 0.099f * juce::jlimit (0.0f, 1.0f, damping);
        buffer[(size_t) read] = feedback * 0.5f * (value + buffer[(size_t) next]);

        writePos = next;
        return value;
    }

private:
    static constexpr int maxPeriod = 4096;

    std::vector<float> buffer;
    double sampleRate = 48000.0;
    int period = 100;
    int writePos = 0;
};

} // namespace spa::dsp
