#include "FXChain.h"

namespace spa::dsp
{

void FXChain::prepare (double newSampleRate, int maxBlockSize)
{
    sampleRate = newSampleRate;

    const juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, 2 };

    for (auto& f : toneFilters)
    {
        f.prepare ({ sampleRate, (juce::uint32) maxBlockSize, 1 });
        f.setType (juce::dsp::FirstOrderTPTFilterType::lowpass);
    }

    chorus.prepare (spec);
    modEffect.prepare (sampleRate, maxBlockSize);
    tremVibEffect.prepare (sampleRate, maxBlockSize);
    limiterEffect.prepare (sampleRate, maxBlockSize);
    convolution.prepare (spec);
    convScratch.setSize (2, maxBlockSize, false, false, true);
    for (auto& b : convPreBuf) { b.setSize (1, (int) (0.2 * sampleRate) + 8); b.clear(); }
    convPreWrite = 0;
    // Re-shape/reload the IR at the new rate so it survives sample-rate and
    // oversampling changes (prepare resets the convolution engine).
    if (haveRawIR) reshapeConvolutionIR();

    delayBuffer.setSize (2, (int) (sampleRate * 4.0) + 8);
    delayBuffer.clear();
    delayWritePos = 0;
    delaySamplesSmoothed.reset (sampleRate, 0.1);

    reverb.prepare (sampleRate, maxBlockSize);

    eq.prepare (sampleRate, maxBlockSize);

    reset();
}

void FXChain::reset()
{
    for (auto& f : toneFilters)
        f.reset();
    chorus.reset();
    modEffect.reset();
    tremVibEffect.reset();
    limiterEffect.reset();
    convolution.reset();
    delayBuffer.clear();
    reverb.reset();
    eq.reset();
}

double FXChain::tailSeconds (const Params& p) const
{
    double tail = 0.0;

    if (p.delayEnable)
    {
        const auto time = p.delaySync
                        ? params::lfoDivisionBeats (p.delayDivision) * 60.0 / p.bpm
                        : (double) p.delayTimeMs * 0.001;
        // Feedback ring-out to roughly -60 dB.
        const auto repeats = p.delayFeedback > 0.01f
                           ? std::log (0.001) / std::log ((double) p.delayFeedback)
                           : 1.0;
        tail = juce::jlimit (0.0, 12.0, time * repeats);
    }

    if (p.reverbEnable)
        tail = juce::jmax (tail, 0.5 + (double) p.reverbDecay);

    if (p.convEnable)
        // Pre-delay gap + the (reshaped) IR's own length; hosts truncate the
        // tail on bounce/freeze otherwise, clipping the reverb-style ring-out.
        tail = juce::jmax (tail, (double) p.convPreDelay * 0.001
                                + irLengthSeconds.load (std::memory_order_relaxed));

    return tail;
}

void FXChain::process (juce::AudioBuffer<float>& buffer, const Params& params)
{
    for (const auto module : params.order)
    {
        switch (module)
        {
            case Module::distortion: if (params.distEnable)   processDistortion (buffer, params); break;
            case Module::chorus:     if (params.chorusEnable) processChorus (buffer, params); break;
            case Module::delay:      if (params.delayEnable)  processDelay (buffer, params); break;
            case Module::reverb:     if (params.reverbEnable) processReverb (buffer, params); break;
            case Module::eq:         if (params.eqEnable)     processEQ (buffer, params); break;
            case Module::mod:        if (params.modEnable)    processMod (buffer, params); break;
            case Module::tremVib:    if (params.tremEnable || params.vibEnable)
                                                            { processTremVib (buffer, params); } break;
            case Module::limiter:    if (params.limEnable)    processLimiter (buffer, params); break;
            case Module::convolve:   if (params.convEnable && convIrLoaded.load (std::memory_order_relaxed))
                                                            { processConvolve (buffer, params); } break;
        }
    }
}

void FXChain::processDistortion (juce::AudioBuffer<float>& buffer, const Params& p)
{
    const auto driveGain = 1.0f + 15.0f * p.distDrive;

    for (auto& f : toneFilters)
        f.setCutoffFrequency (p.distToneHz);

    for (int ch = 0; ch < juce::jmin (2, buffer.getNumChannels()); ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        auto& tone = toneFilters[(size_t) ch];

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto dry = data[i];
            const auto x = dry * driveGain;

            float wet;
            switch (p.distType)
            {
                case 1:  wet = juce::jlimit (-1.0f, 1.0f, x); break;              // Hard
                case 2:  wet = std::sin (x * 1.2f); break;                        // Fold
                default: wet = std::tanh (x); break;                              // Soft
            }

            wet = tone.processSample (0, wet / std::sqrt (driveGain));
            data[i] = dry + (wet - dry) * p.distMix;
        }
    }
}

void FXChain::processChorus (juce::AudioBuffer<float>& buffer, const Params& p)
{
    chorus.setRate (p.chorusRate);
    chorus.setDepth (p.chorusDepth);
    chorus.setCentreDelay (7.0f);
    chorus.setFeedback (p.chorusFeedback);
    chorus.setMix (p.chorusMix);

    juce::dsp::AudioBlock<float> block (buffer);
    chorus.process (juce::dsp::ProcessContextReplacing<float> (block));
}

void FXChain::processDelay (juce::AudioBuffer<float>& buffer, const Params& p)
{
    const auto timeSeconds = p.delaySync
                           ? params::lfoDivisionBeats (p.delayDivision) * 60.0 / p.bpm
                           : (double) p.delayTimeMs * 0.001;
    const auto targetSamples = (float) juce::jlimit (
        32.0, (double) delayBuffer.getNumSamples() - 8.0, timeSeconds * sampleRate);
    delaySamplesSmoothed.setTargetValue (targetSamples);

    const auto bufLen = delayBuffer.getNumSamples();
    auto* bufL = delayBuffer.getWritePointer (0);
    auto* bufR = delayBuffer.getWritePointer (1);
    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : left;

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        const auto delaySamples = delaySamplesSmoothed.getNextValue();

        auto readPos = (double) delayWritePos - (double) delaySamples;
        while (readPos < 0.0)
            readPos += (double) bufLen;

        const auto r0 = (int) readPos;
        const auto r1 = (r0 + 1) % bufLen;
        const auto frac = (float) (readPos - (double) r0);

        const auto outL = bufL[r0] + frac * (bufL[r1] - bufL[r0]);
        const auto outR = bufR[r0] + frac * (bufR[r1] - bufR[r0]);

        // Ping-pong crosses the feedback paths.
        bufL[delayWritePos] = left[i] + (p.delayPingPong ? outR : outL) * p.delayFeedback;
        bufR[delayWritePos] = right[i] + (p.delayPingPong ? outL : outR) * p.delayFeedback;

        left[i] += outL * p.delayMix;
        right[i] += outR * p.delayMix;

        delayWritePos = (delayWritePos + 1) % bufLen;
    }
}

void FXChain::processReverb (juce::AudioBuffer<float>& buffer, const Params& p)
{
    // 4-line FDN engine (FDNReverb) with mode voicings. MIX is a true dry/wet
    // dial handled inside the engine as an equal-power (sin/cos) crossfade:
    // unity dry at 0, full wet (pure reverb) at 1, perceived level roughly
    // constant since the tail and the dry signal are decorrelated.
    FDNReverb::Params rp;
    rp.mode = p.reverbMode;
    rp.preDelayMs = p.reverbPreDelay;
    rp.size = p.reverbSize;
    rp.decaySec = p.reverbDecay;
    rp.hfDamp = p.reverbDamping;
    rp.modDepth = p.reverbModDepth;
    rp.lowCutHz = p.reverbLowCut;
    rp.highCutHz = p.reverbHighCut;
    rp.width = p.reverbWidth;
    rp.mix = p.reverbMix;
    reverb.process (buffer, rp);
}

void FXChain::processMod (juce::AudioBuffer<float>& buffer, const Params& p)
{
    ModEffect::Params mp;
    mp.type    = p.modType == 1 ? ModEffect::Type::flanger : ModEffect::Type::phaser;
    mp.rateHz  = p.modSync
               ? (float) (p.bpm / 60.0
                          / juce::jmax (0.01, (double) params::lfoDivisionBeats (p.modDivision)))
               : p.modRate;
    mp.depth    = p.modDepth;
    mp.feedback = p.modFeedback;
    mp.stages   = p.modStages;
    mp.centreHz = p.modCentreHz;
    mp.manualMs = p.modManualMs;
    mp.spread   = p.modWidth;
    mp.mix      = p.modMix;
    modEffect.process (buffer, mp);
}

void FXChain::processTremVib (juce::AudioBuffer<float>& buffer, const Params& p)
{
    const auto syncHz = [&] (int div)
    {
        return (float) (p.bpm / 60.0
                        / juce::jmax (0.01, (double) params::lfoDivisionBeats (div)));
    };

    TremVib::Params tp;
    tp.tremOn     = p.tremEnable;
    tp.tremRateHz = p.tremSync ? syncHz (p.tremDivision) : p.tremRate;
    tp.tremDepth  = p.tremDepth;
    tp.tremShape  = p.tremShape;
    tp.tremStereo = p.tremStereo;
    tp.tremMix    = p.tremMix;
    tp.vibOn      = p.vibEnable;
    tp.vibRateHz  = p.vibSync ? syncHz (p.vibDivision) : p.vibRate;
    tp.vibDepth   = p.vibDepth;
    tp.vibMix     = p.vibMix;
    tremVibEffect.process (buffer, tp);
}

void FXChain::processLimiter (juce::AudioBuffer<float>& buffer, const Params& p)
{
    Limiter::Params lp;
    lp.enable      = p.limEnable;
    lp.driveDb     = p.limDrive;
    lp.ceilingDb   = p.limCeiling;
    lp.releaseMs   = p.limRelease;
    lp.autoRelease = p.limAutoRelease;
    lp.character   = p.limCharacter;
    lp.stereoLink  = p.limStereoLink;
    lp.truePeak    = p.limTruePeak;
    lp.lookahead   = p.limLookahead;
    lp.autoGain    = p.limAutoGain;
    limiterEffect.process (buffer, lp);
}

int FXChain::limiterLatencySamples (const Params& p) const
{
    Limiter::Params lp;
    lp.enable    = p.limEnable;
    lp.lookahead = p.limLookahead;
    return limiterEffect.latencySamples (lp);
}

void FXChain::processConvolve (juce::AudioBuffer<float>& buffer, const Params& p)
{
    const int n = buffer.getNumSamples();
    const int numCh = juce::jmin (2, buffer.getNumChannels());

    // Wet copy through the convolution, then blend with the dry (mix + width).
    for (int ch = 0; ch < 2; ++ch)
        convScratch.copyFrom (ch, 0, buffer, juce::jmin (ch, numCh - 1), 0, n);
    auto block = juce::dsp::AudioBlock<float> (convScratch).getSubBlock (0, (size_t) n);
    convolution.process (juce::dsp::ProcessContextReplacing<float> (block));

    // Wet pre-delay (gap before the reverb).
    const int rs = convPreBuf[0].getNumSamples();
    const int pd = juce::jlimit (0, rs - 1, (int) (p.convPreDelay * 0.001f * (float) sampleRate));
    if (pd > 0 && rs > 1)
        for (int i = 0; i < n; ++i)
        {
            const int w = convPreWrite;
            const int rd = (w - pd + rs) % rs;
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* ring = convPreBuf[(size_t) ch].getWritePointer (0);
                ring[w] = convScratch.getSample (ch, i);
                convScratch.setSample (ch, i, ring[rd]);
            }
            convPreWrite = (w + 1) % rs;
        }

    const float mix = juce::jlimit (0.0f, 1.0f, p.convMix);
    const float width = juce::jlimit (0.0f, 1.0f, p.convWidth);
    for (int i = 0; i < n; ++i)
    {
        float wL = convScratch.getSample (0, i);
        float wR = convScratch.getSample (1, i);
        if (numCh > 1)   // stereo width via mid/side on the wet
        {
            const float mid = 0.5f * (wL + wR);
            const float side = 0.5f * (wL - wR) * width;
            wL = mid + side; wR = mid - side;
        }
        buffer.setSample (0, i, buffer.getSample (0, i) * (1.0f - mix) + wL * mix);
        if (numCh > 1)
            buffer.setSample (1, i, buffer.getSample (1, i) * (1.0f - mix) + wR * mix);
    }
}

void FXChain::loadConvolutionIR (const juce::File& irFile)
{
    haveRawIR = false;
    convIrLoaded.store (false, std::memory_order_relaxed);
    if (! irFile.existsAsFile()) return;
    if (convFormats.getNumKnownFormats() == 0) convFormats.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (convFormats.createReaderFor (irFile));
    if (reader == nullptr || reader->lengthInSamples <= 0) return;

    const int maxSamples = (int) (reader->sampleRate * 10.0);   // cap the IR at 10 s
    const int n = (int) juce::jmin ((juce::int64) maxSamples, reader->lengthInSamples);
    rawIR.setSize ((int) juce::jmin ((juce::uint32) 2, reader->numChannels), n);
    reader->read (&rawIR, 0, n, 0, true, true);
    rawIRSampleRate = reader->sampleRate;
    haveRawIR = true;
    reshapeConvolutionIR();
}

void FXChain::setConvolutionShaping (float decay, float damping)
{
    if (juce::approximatelyEqual (decay, convDecayApplied)
        && juce::approximatelyEqual (damping, convDampingApplied))
        return;
    convDecayApplied = decay;
    convDampingApplied = damping;
    if (haveRawIR) reshapeConvolutionIR();
}

// Builds the shaped IR (decay envelope + HF damping) from the raw IR and loads
// it; also refreshes the display envelope. Message thread only.
void FXChain::reshapeConvolutionIR()
{
    if (! haveRawIR || rawIR.getNumSamples() == 0)
    {
        convIrLoaded.store (false, std::memory_order_relaxed);
        return;
    }

    const int n = rawIR.getNumSamples();
    const int ch = rawIR.getNumChannels();
    const double sr = rawIRSampleRate > 0.0 ? rawIRSampleRate : sampleRate;
    irLengthSeconds.store ((double) n / sr, std::memory_order_relaxed);

    const float decay = juce::jlimit (0.05f, 1.0f, convDecayApplied);
    const float damp  = juce::jlimit (0.0f, 1.0f, convDampingApplied);
    const float kDecay = 6.9f / (decay * (float) juce::jmax (1, n));   // -60 dB at decay*len
    const float cutoff = juce::jmap (damp, 0.0f, 1.0f, 20000.0f, 800.0f);
    const float lpCoef = damp > 0.001f
        ? 1.0f - std::exp (-juce::MathConstants<float>::twoPi * cutoff / (float) sr)
        : 1.0f;

    juce::AudioBuffer<float> shaped (ch, n);
    for (auto& e : irEnvelope) e = 0.0f;

    for (int c = 0; c < ch; ++c)
    {
        const float* src = rawIR.getReadPointer (c);
        float* dst = shaped.getWritePointer (c);
        float lp = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            float v = src[i];
            if (damp > 0.001f) { lp += lpCoef * (v - lp); v = lp; }
            dst[i] = v * std::exp (-kDecay * (float) i);
        }
    }

    for (int i = 0; i < n; ++i)
    {
        const int b = juce::jlimit (0, convEnvPoints - 1, i * convEnvPoints / juce::jmax (1, n));
        float a = std::abs (shaped.getSample (0, i));
        if (ch > 1) a = juce::jmax (a, std::abs (shaped.getSample (1, i)));
        irEnvelope[(size_t) b] = juce::jmax (irEnvelope[(size_t) b], a);
    }

    convolution.loadImpulseResponse (std::move (shaped), sr,
                                     juce::dsp::Convolution::Stereo::yes,
                                     juce::dsp::Convolution::Trim::no,
                                     juce::dsp::Convolution::Normalise::yes);
    convIrLoaded.store (true, std::memory_order_relaxed);
}

void FXChain::processEQ (juce::AudioBuffer<float>& buffer, const Params& p)
{
    eq.setCharacter (p.eqCharacter);
    eq.updateBands (p.eqBands);
    eq.process (buffer);
}

} // namespace spa::dsp
