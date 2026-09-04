#include "SampleData.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>

namespace spa::dsp
{

namespace
{
    // Analysis runs at a decimated rate: plenty for follower curves, and it
    // keeps YIN affordable on long files.
    constexpr double analysisRate = 16000.0;
    constexpr int hopSamples = 160;         // 10 ms at 16 kHz
    constexpr int yinWindow = 1024;         // 64 ms
    constexpr int yinMaxLag = 400;          // 40 Hz floor
    constexpr int yinMinLag = 16;           // 1 kHz ceiling
    constexpr float yinThreshold = 0.15f;
    constexpr double maxAnalysisSeconds = 120.0;

    bool cancelled (const ShouldCancelFn& shouldCancel)
    {
        return shouldCancel && shouldCancel();
    }

    std::vector<float> decimateToMono (const juce::AudioBuffer<float>& audio,
                                       double sourceRate)
    {
        const auto ratio = sourceRate / analysisRate;
        const auto outLen = (int) juce::jmin ((double) audio.getNumSamples() / ratio,
                                              maxAnalysisSeconds * analysisRate);

        std::vector<float> mono ((size_t) outLen, 0.0f);
        const auto channelGain = 1.0f / (float) audio.getNumChannels();

        for (int i = 0; i < outLen; ++i)
        {
            const auto srcIdx = juce::jmin ((int) (i * ratio), audio.getNumSamples() - 1);
            for (int ch = 0; ch < audio.getNumChannels(); ++ch)
                mono[(size_t) i] += audio.getSample (ch, srcIdx) * channelGain;
        }

        return mono;
    }

    std::vector<float> analyzeAmplitude (const std::vector<float>& mono,
                                         const ShouldCancelFn& shouldCancel)
    {
        std::vector<float> curve;
        curve.reserve (mono.size() / hopSamples + 1);

        float peak = 1.0e-9f;
        for (size_t start = 0; start + 1 < mono.size(); start += hopSamples)
        {
            if (cancelled (shouldCancel))
                break;   // caller discards a cancelled result — see loadSampleFromFile

            const auto end = juce::jmin (start + (size_t) hopSamples * 2, mono.size());
            double sum = 0.0;
            for (auto i = start; i < end; ++i)
                sum += (double) mono[i] * mono[i];

            const auto rms = (float) std::sqrt (sum / (double) (end - start));
            curve.push_back (rms);
            peak = juce::jmax (peak, rms);
        }

        for (auto& v : curve)
            v /= peak;

        return curve;
    }

    // YIN (de Cheveigné & Kawahara) with the cumulative-mean normalization.
    // Returns MIDI note mapped 24..96 -> 0..1, or 0 where unvoiced.
    std::vector<float> analyzePitch (const std::vector<float>& mono,
                                     const ShouldCancelFn& shouldCancel)
    {
        std::vector<float> curve;
        curve.reserve (mono.size() / hopSamples + 1);

        std::vector<float> d ((size_t) yinMaxLag + 1);

        // This is the expensive part of load-time analysis: ~yinMaxLag *
        // yinWindow multiply-adds per hop (~400k), times up to ~12000 hops for
        // a maxAnalysisSeconds-length file — billions of ops worst case.
        // Checked every hop (this outer loop), not just once per file, so a
        // request superseded by rapid quick-swap auditioning bails within a
        // fraction of a hop's cost instead of grinding through the whole
        // file for a result that's about to be thrown away.
        for (size_t start = 0; start + 1 < mono.size(); start += hopSamples)
        {
            if (cancelled (shouldCancel))
                break;   // caller discards a cancelled result — see loadSampleFromFile

            if (start + yinWindow + yinMaxLag >= mono.size())
            {
                curve.push_back (curve.empty() ? 0.0f : curve.back());
                continue;
            }

            // Difference function.
            for (int lag = 1; lag <= yinMaxLag; ++lag)
            {
                double sum = 0.0;
                for (int i = 0; i < yinWindow; ++i)
                {
                    const auto diff = mono[start + (size_t) i]
                                    - mono[start + (size_t) (i + lag)];
                    sum += (double) diff * diff;
                }
                d[(size_t) lag] = (float) sum;
            }

            // Cumulative mean normalized difference.
            float cumulative = 0.0f;
            int bestLag = 0;
            for (int lag = 1; lag <= yinMaxLag; ++lag)
            {
                cumulative += d[(size_t) lag];
                const auto cmnd = cumulative > 0.0f
                                ? d[(size_t) lag] * (float) lag / cumulative
                                : 1.0f;
                if (lag >= yinMinLag && cmnd < yinThreshold)
                {
                    bestLag = lag;
                    break;
                }
            }

            if (bestLag == 0)
            {
                curve.push_back (0.0f);  // unvoiced
                continue;
            }

            const auto freq = (float) (analysisRate / bestLag);
            const auto midi = 69.0f + 12.0f * std::log2 (freq / 440.0f);
            curve.push_back (juce::jlimit (0.0f, 1.0f, (midi - 24.0f) / 72.0f));
        }

        return curve;
    }
}

LoadedSample loadSampleFromFile (const juce::File& file, const ShouldCancelFn& shouldCancel)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    // A just-reconnected external drive can briefly fail reads while macOS
    // finishes remounting the volume, which createReaderFor reports as an
    // unrecognized format even though the file is fine moments later. Retry
    // a few times before giving up (runs on a background load thread, never
    // the audio thread, so blocking here is fine). Each attempt (and its
    // preceding sleep, up to 180ms) is a cancellation checkpoint, so a
    // request superseded during a remount stall doesn't sit through the
    // whole retry budget for nothing.
    std::unique_ptr<juce::AudioFormatReader> reader;
    for (int attempt = 0; attempt < 4 && reader == nullptr; ++attempt)
    {
        if (attempt > 0)
            juce::Thread::sleep (60 * attempt);
        if (cancelled (shouldCancel))
            return { nullptr, {} };
        reader.reset (formats.createReaderFor (file));
    }
    if (reader == nullptr)
        return { nullptr, "Unrecognized audio format: " + file.getFileName() };

    if (reader->lengthInSamples < 64)
        return { nullptr, "File too short: " + file.getFileName() };

    // A corrupt (or hostile) header can claim an arbitrary sample rate,
    // including non-finite — casting Inf/NaN to int64 below would be UB, and
    // even a huge-but-finite value would blow the time-based cap that
    // follows through the roof, attempting a runaway allocation. Reject
    // outright rather than trying to cap our way out of garbage input.
    if (! std::isfinite (reader->sampleRate) || reader->sampleRate <= 0.0
        || reader->sampleRate > 400000.0)
        return { nullptr, "Implausible sample rate: " + file.getFileName() };

    if (reader->numChannels == 0)
        return { nullptr, "No audio channels: " + file.getFileName() };

    auto data = std::make_shared<SampleData>();
    data->sourceSampleRate = reader->sampleRate;
    data->name = file.getFileNameWithoutExtension();

    const auto numChannels = (int) juce::jmin (reader->numChannels, 2u);

    // Cap at a sane 10 minutes of source-rate audio, same clamp-before-cast
    // pattern as FXChain::loadConvolutionIR. Floor-guarded so a
    // near-zero sampleRate (already rejected above, but keep the guard for
    // belt-and-braces) can't produce a cap below the 64-sample minimum
    // already checked.
    const juce::int64 maxSamplesByTime = juce::jmax ((juce::int64) 64,
                                               (juce::int64) (reader->sampleRate * 600.0));

    // Independent, byte-level backstop that doesn't trust the reported
    // sample rate at all beyond the sanity check above: even a "plausible"
    // but very high rate held for 10 minutes can demand an enormous
    // allocation. ~512MB comfortably covers a genuine 10-minute stereo
    // float32 file up to 96kHz (~440MB) — the highest resolution this
    // library ships originals at (Pro packs distribute at 24/48; masters are
    // archived at 24/96, see CLAUDE.md) — with headroom, while still
    // bounding the worst case. A file that would exceed it even after the
    // time-based cap is rejected outright (clear error through the same
    // getSampleError()/OscStrip "! " path every other load failure uses)
    // rather than silently handed back truncated to whatever fits: content
    // this far outside real SFX-library material reads as corrupt/hostile
    // rather than something worth a partial decode.
    constexpr juce::int64 maxDecodedBytes = (juce::int64) 512 * 1024 * 1024;
    const juce::int64 maxSamplesByBytes = maxDecodedBytes
        / (juce::int64) (numChannels * (int) sizeof (float));

    const juce::int64 requested = juce::jmin (maxSamplesByTime, reader->lengthInSamples);
    if (requested > maxSamplesByBytes)
        return { nullptr, "File exceeds the maximum decodable size: " + file.getFileName() };

    const int n = (int) requested;

    data->audio.setSize (numChannels, n);
    if (! reader->read (&data->audio, 0, n, 0, true, numChannels > 1))
        return { nullptr, "Failed to read audio data: " + file.getFileName() };

    if (cancelled (shouldCancel))
        return { nullptr, {} };

    const auto mono = decimateToMono (data->audio, data->sourceSampleRate);
    data->ampCurve = analyzeAmplitude (mono, shouldCancel);
    data->pitchCurve = analyzePitch (mono, shouldCancel);
    data->hopSeconds = (double) hopSamples / analysisRate;

    return { std::move (data), {} };
}

} // namespace spa::dsp
