#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace spa::dsp
{

// An SFX/sample loaded into an oscillator slot, plus the offline follower
// analysis computed at load time (background thread). The amp and pitch
// curves are what the SFX-follower mod sources stream at play time — no
// real-time analysis ever runs on the audio thread.
struct SampleData
{
    juce::AudioBuffer<float> audio;      // 1 or 2 channels
    double sourceSampleRate = 48000.0;
    juce::String name;

    // Follower curves, one value per analysis hop.
    // amp: normalized 0..1 (peak of the file = 1).
    // pitch: MIDI note mapped 24..96 -> 0..1; 0 where unvoiced/uncertain.
    std::vector<float> ampCurve;
    std::vector<float> pitchCurve;
    double hopSeconds = 0.010;

    int lengthSamples() const noexcept { return audio.getNumSamples(); }
    double lengthSeconds() const noexcept
    {
        return audio.getNumSamples() / sourceSampleRate;
    }

    // Curve lookup by playback position (audio-thread safe).
    float curveValue (const std::vector<float>& curve, double seconds) const noexcept
    {
        if (curve.empty() || hopSeconds <= 0.0)
            return 0.0f;

        const auto idx = seconds / hopSeconds;
        const auto i0 = juce::jlimit (0, (int) curve.size() - 1, (int) idx);
        const auto i1 = juce::jmin (i0 + 1, (int) curve.size() - 1);
        const auto frac = (float) (idx - (double) i0);
        return curve[(size_t) i0] + frac * (curve[(size_t) i1] - curve[(size_t) i0]);
    }

    float ampAt (double seconds) const noexcept   { return curveValue (ampCurve, seconds); }
    float pitchAt (double seconds) const noexcept { return curveValue (pitchCurve, seconds); }
};

struct LoadedSample
{
    std::shared_ptr<const SampleData> sample;  // null on failure
    juce::String error;
};

// Polled between chunks of expensive work (retry-loop attempts, analysis
// hops) so a caller can bail a load early once the result is no longer
// wanted. Optional -- an empty/null function means "never cancel", the
// original unconditional behavior.
using ShouldCancelFn = std::function<bool()>;

// Reads an audio file and runs the offline follower analysis (RMS envelope +
// YIN pitch track). Synchronous and allocating — background thread only.
// If shouldCancel becomes true partway through, returns early with a null
// sample (the caller is expected to discard the result anyway in that case
// — see ContentLoadWorker's cancellation contract).
LoadedSample loadSampleFromFile (const juce::File& file,
                                 const ShouldCancelFn& shouldCancel = {});

} // namespace spa::dsp
