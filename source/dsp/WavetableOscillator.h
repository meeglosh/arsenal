#pragma once

#include "Wavetable.h"
#include "../params/ParameterRegistry.h"

#include <array>

namespace spa::dsp
{

// One oscillator slot within a voice: up to 8 unison sub-oscillators reading
// a shared wavetable. Per-block parameter snapshot, per-sample rendering.
// Real-time safe; the table itself is owned by the processor.
class UnisonOscillator
{
public:
    static constexpr int maxUnison = 8;

    struct BlockParams
    {
        const Wavetable* table = nullptr;
        float baseFrequencyHz = 440.0f;
        float position = 0.0f;       // 0..1 morph
        int unisonCount = 1;
        float detuneCents = 0.0f;    // full spread, symmetric around base
        float blend = 1.0f;          // detuned-voice level relative to centre
        float width = 0.0f;          // stereo spread of detuned voices
        float pan = 0.0f;            // -1..1 slot pan
        float phaseOffset = 0.0f;    // cycles; chaos phase drift lands here
    };

    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        noteOn (params::PhaseMode::reset, 0.0f, nullptr);
    }

    void noteOn (params::PhaseMode mode, float phaseParam, juce::Random* random) noexcept
    {
        if (mode == params::PhaseMode::free_)
            return;

        // One phase for every unison voice (matching Reset's "all aligned"
        // behaviour, just at a random rotation instead of a fixed one) --
        // NOT one independent random draw per sub-oscillator. Independent
        // draws could land two unison voices at (near) opposite phase; at
        // zero/near-zero detune (a legitimate, common roll) they then never
        // beat back out of that alignment, silencing the slot for the life
        // of the note. Found via a rare, non-reproducible "RANDOMIZE ALL
        // sometimes silent" report that traced to exactly this.
        const auto startPhase = mode == params::PhaseMode::random && random != nullptr
                               ? (double) random->nextFloat()
                               : (double) phaseParam;
        for (auto& ph : phases)
            ph = startPhase;
    }

    // Called once per block before getNextSample() calls.
    void updateBlock (const BlockParams& p) noexcept
    {
        table = p.table;
        position = p.position;
        count = juce::jlimit (1, maxUnison, p.unisonCount);
        phaseOffset = p.phaseOffset - std::floor (p.phaseOffset);  // wrap to [0,1)

        if (table == nullptr)
            return;

        mipLevel = table->mipLevelForFrequency (
            p.baseFrequencyHz * std::exp2 (p.detuneCents * 0.5f / 1200.0f), sampleRate);

        float gainNorm = 0.0f;

        for (int u = 0; u < count; ++u)
        {
            // Symmetric spread in [-1, 1]; single voice sits at 0.
            const auto spread = count > 1 ? -1.0f + 2.0f * (float) u / (float) (count - 1) : 0.0f;
            const auto freq = p.baseFrequencyHz
                            * std::exp2 (p.detuneCents * spread * 0.5f / 1200.0f);
            increments[(size_t) u] = (double) freq / sampleRate;

            const auto isCentre = spread == 0.0f || count == 1;
            const auto gain = isCentre ? 1.0f : p.blend;
            gainNorm += gain * gain;

            // Equal-power pan: unison spread combined with slot pan.
            const auto panPos = juce::jlimit (-1.0f, 1.0f, spread * p.width + p.pan);
            const auto angle = (panPos + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
            gainL[(size_t) u] = gain * std::cos (angle);
            gainR[(size_t) u] = gain * std::sin (angle);
        }

        // Keep perceived level constant as voices stack.
        const auto norm = gainNorm > 0.0f ? 1.0f / std::sqrt (gainNorm) : 1.0f;
        for (int u = 0; u < count; ++u)
        {
            gainL[(size_t) u] *= norm;
            gainR[(size_t) u] *= norm;
        }
    }

    struct StereoSample { float left = 0.0f, right = 0.0f; };

    StereoSample getNextSample() noexcept
    {
        if (table == nullptr || table->getNumFrames() == 0)
            return {};

        const auto numFrames = table->getNumFrames();
        const auto framePos = juce::jlimit (0.0f, 1.0f, position) * (float) (numFrames - 1);
        const auto frameA = juce::jmin ((int) framePos, numFrames - 1);
        const auto frameB = juce::jmin (frameA + 1, numFrames - 1);
        const auto frameFrac = framePos - (float) frameA;

        const auto* a = table->getFrame (mipLevel, frameA);
        const auto* b = table->getFrame (mipLevel, frameB);

        StereoSample out;

        for (int u = 0; u < count; ++u)
        {
            auto readPhase = phases[(size_t) u] + (double) phaseOffset;
            if (readPhase >= 1.0)
                readPhase -= 1.0;

            const auto idx = readPhase * (double) Wavetable::tableSize;
            const auto i0 = (int) idx;
            const auto frac = (float) (idx - (double) i0);

            const auto sampleA = a[i0] + frac * (a[i0 + 1] - a[i0]);
            const auto sampleB = b[i0] + frac * (b[i0 + 1] - b[i0]);
            const auto s = sampleA + frameFrac * (sampleB - sampleA);

            out.left += s * gainL[(size_t) u];
            out.right += s * gainR[(size_t) u];

            auto& ph = phases[(size_t) u];
            ph += increments[(size_t) u];
            if (ph >= 1.0)
                ph -= 1.0;
        }

        return out;
    }

private:
    const Wavetable* table = nullptr;
    double sampleRate = 44100.0;
    float position = 0.0f;
    float phaseOffset = 0.0f;
    int count = 1;
    int mipLevel = 0;

    std::array<double, maxUnison> phases {};
    std::array<double, maxUnison> increments {};
    std::array<float, maxUnison> gainL {};
    std::array<float, maxUnison> gainR {};
};

} // namespace spa::dsp
