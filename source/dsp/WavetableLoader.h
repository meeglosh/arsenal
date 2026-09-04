#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include "Wavetable.h"

#include <functional>
#include <memory>

namespace spa::dsp
{

struct LoadedWavetable
{
    std::shared_ptr<const Wavetable> table;  // null on failure
    juce::String error;
};

// Polled between chunks of work so a caller can bail a load early once the
// result is no longer wanted; see SampleData.h's ShouldCancelFn (same
// alias, redeclared here to avoid an extra include — identical type aliases
// may be repeated). Optional -- empty/null means "never cancel".
using ShouldCancelFn = std::function<bool()>;

// Reads a WAV (or AIFF/FLAC) into a Wavetable. Files whose length is a
// multiple of 2048 samples follow the Serum-style multi-frame convention;
// anything else is treated as a single-cycle wave and resampled.
// Synchronous and allocating — call from a background thread, never the
// audio thread. Unlike SampleLoader.cpp there's no per-sample analysis loop
// here (numSamples is already hard-capped below, independent of the file's
// reported sample rate), so the only meaningful cancellation checkpoint is
// before the decode.
LoadedWavetable loadWavetableFromFile (const juce::File& file,
                                       const ShouldCancelFn& shouldCancel = {});

} // namespace spa::dsp
