#include "WavetableLoader.h"

namespace spa::dsp
{

LoadedWavetable loadWavetableFromFile (const juce::File& file, const ShouldCancelFn& shouldCancel)
{
    const auto cancelled = [&shouldCancel] { return shouldCancel && shouldCancel(); };

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    // See SampleLoader.cpp: a just-reconnected external drive can briefly
    // fail reads while macOS finishes remounting, so retry before giving up
    // (background load thread only, never the audio thread). Each attempt
    // (and its preceding sleep) is a cancellation checkpoint.
    std::unique_ptr<juce::AudioFormatReader> reader;
    for (int attempt = 0; attempt < 4 && reader == nullptr; ++attempt)
    {
        if (attempt > 0)
            juce::Thread::sleep (60 * attempt);
        if (cancelled())
            return { nullptr, {} };
        reader.reset (formats.createReaderFor (file));
    }
    if (reader == nullptr)
        return { nullptr, "Unrecognized audio format: " + file.getFileName() };

    if (cancelled())
        return { nullptr, {} };

    const auto numSamples = (int) juce::jmin (reader->lengthInSamples,
                                              (juce::int64) Wavetable::tableSize * Wavetable::maxFrames);
    if (numSamples < 16)
        return { nullptr, "File too short to be a wavetable: " + file.getFileName() };

    // WAV headers carry a 16-bit channel count; a garbage value here would
    // otherwise drive a multi-GB allocation attempt. Clamp like SampleLoader.cpp.
    const auto numChannels = (int) juce::jmin (reader->numChannels, 2u);
    juce::AudioBuffer<float> buffer (numChannels, numSamples);
    if (! reader->read (&buffer, 0, numSamples, 0, true, true))
        return { nullptr, "Failed to read audio data: " + file.getFileName() };

    // Mix down to mono.
    std::vector<float> mono ((size_t) numSamples, 0.0f);
    const auto channelGain = 1.0f / (float) buffer.getNumChannels();
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const auto* src = buffer.getReadPointer (ch);
        for (int i = 0; i < numSamples; ++i)
            mono[(size_t) i] += src[i] * channelGain;
    }

    const bool multiFrame = numSamples >= Wavetable::tableSize
                         && numSamples % Wavetable::tableSize == 0;
    const auto frameSize = multiFrame ? Wavetable::tableSize : numSamples;

    auto table = std::make_shared<Wavetable> (
        Wavetable::fromAudioFrames (file.getFileNameWithoutExtension(),
                                    mono.data(), numSamples, frameSize));

    return { std::move (table), {} };
}

} // namespace spa::dsp
