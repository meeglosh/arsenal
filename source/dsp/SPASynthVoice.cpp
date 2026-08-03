#include "SPASynthVoice.h"

namespace spa::dsp
{

namespace
{
    // Named handles into the dense mod-dest index space, resolved once.
    struct DestLookup
    {
        struct Slot
        {
            int position, coarse, fine, level, pan, unisonDetune, unisonBlend, unisonWidth;
            int grainSize, grainDensity, grainPos, grainSpray;
            int pulseWidth, fmIndex, pluckDamp;
        };
        std::array<Slot, params::numOscSlots> slots {};
        int cutoff, resonance, drive, filterEnvAmt, filterMix;
        int f2Cutoff, f2Resonance, f2Drive, f2EnvAmt, f2Mix;
        int ampA, ampD, ampS, ampR;
        int env2A, env2D, env2S, env2R;
        int env3A, env3D, env3S, env3R;
        int chaosDepth, chaosRate, chaosMix;

        static const DestLookup& get()
        {
            static const DestLookup lookup = []
            {
                jassert (params::numModDests() <= params::maxModDests);

                DestLookup l {};
                namespace id = params::id;

                for (int s = 0; s < params::numOscSlots; ++s)
                {
                    auto& slot = l.slots[(size_t) s];
                    slot.position     = params::modDestIndex (id::oscSlot (s, id::osc::position));
                    slot.coarse       = params::modDestIndex (id::oscSlot (s, id::osc::coarse));
                    slot.fine         = params::modDestIndex (id::oscSlot (s, id::osc::fine));
                    slot.level        = params::modDestIndex (id::oscSlot (s, id::osc::level));
                    slot.pan          = params::modDestIndex (id::oscSlot (s, id::osc::pan));
                    slot.unisonDetune = params::modDestIndex (id::oscSlot (s, id::osc::unisonDetune));
                    slot.unisonBlend  = params::modDestIndex (id::oscSlot (s, id::osc::unisonBlend));
                    slot.unisonWidth  = params::modDestIndex (id::oscSlot (s, id::osc::unisonWidth));
                    slot.grainSize    = params::modDestIndex (id::oscSlot (s, id::osc::grainSize));
                    slot.grainDensity = params::modDestIndex (id::oscSlot (s, id::osc::grainDensity));
                    slot.grainPos     = params::modDestIndex (id::oscSlot (s, id::osc::grainPos));
                    slot.grainSpray   = params::modDestIndex (id::oscSlot (s, id::osc::grainSpray));
                    slot.pulseWidth   = params::modDestIndex (id::oscSlot (s, id::osc::pulseWidth));
                    slot.fmIndex      = params::modDestIndex (id::oscSlot (s, id::osc::fmIndex));
                    slot.pluckDamp    = params::modDestIndex (id::oscSlot (s, id::osc::pluckDamp));
                }

                l.cutoff       = params::modDestIndex (id::filter1Cutoff);
                l.resonance    = params::modDestIndex (id::filter1Resonance);
                l.drive        = params::modDestIndex (id::filter1Drive);
                l.filterEnvAmt = params::modDestIndex (id::filter1EnvAmount);
                l.filterMix    = params::modDestIndex (id::filter1Mix);
                l.f2Cutoff     = params::modDestIndex (id::filter2Cutoff);
                l.f2Resonance  = params::modDestIndex (id::filter2Resonance);
                l.f2Drive      = params::modDestIndex (id::filter2Drive);
                l.f2EnvAmt     = params::modDestIndex (id::filter2EnvAmount);
                l.f2Mix        = params::modDestIndex (id::filter2Mix);

                l.ampA = params::modDestIndex (id::ampAttack);
                l.ampD = params::modDestIndex (id::ampDecay);
                l.ampS = params::modDestIndex (id::ampSustain);
                l.ampR = params::modDestIndex (id::ampRelease);

                l.env2A = params::modDestIndex (id::envParam (2, "attack"));
                l.env2D = params::modDestIndex (id::envParam (2, "decay"));
                l.env2S = params::modDestIndex (id::envParam (2, "sustain"));
                l.env2R = params::modDestIndex (id::envParam (2, "release"));

                l.env3A = params::modDestIndex (id::envParam (3, "attack"));
                l.env3D = params::modDestIndex (id::envParam (3, "decay"));
                l.env3S = params::modDestIndex (id::envParam (3, "sustain"));
                l.env3R = params::modDestIndex (id::envParam (3, "release"));

                l.chaosDepth = params::modDestIndex (id::chaos::depth);
                l.chaosRate  = params::modDestIndex (id::chaos::rate);
                l.chaosMix   = params::modDestIndex (id::chaos::mix);

                return l;
            }();

            return lookup;
        }
    };

    const juce::NormalisableRange<float>& destRange (int destIndex)
    {
        return params::modDestinations()[(size_t) destIndex].def->range;
    }

    float denorm (const float* values, int destIndex)
    {
        return destRange (destIndex).convertFrom0to1 (values[destIndex]);
    }
}

SPASynthVoice::SPASynthVoice (const SharedState& sharedState)
    : shared (sharedState)
{
    DestLookup::get();  // resolve indices before the audio thread needs them
}

bool SPASynthVoice::canPlaySound (juce::SynthesiserSound* sound)
{
    return dynamic_cast<SPASynthSound*> (sound) != nullptr;
}

void SPASynthVoice::setCurrentPlaybackSampleRate (double newRate)
{
    juce::SynthesiserVoice::setCurrentPlaybackSampleRate (newRate);

    if (newRate > 0.0)
    {
        for (auto& osc : oscs)
            osc.prepare (newRate);
        for (auto& osc : analogOscs)
            osc.prepare (newRate);
        for (auto& osc : fmOscs)
            osc.prepare (newRate);
        for (auto& gen : noiseGens)
            gen.prepare();
        for (auto& pluck : plucks)
            pluck.prepare (newRate);
        for (auto& lfo : lfos)
            lfo.prepare (newRate);
        filter.prepare (newRate);
        filter2.prepare (newRate);
        ampEnv.setSampleRate (newRate);
        env2.setSampleRate (newRate);
        env3.setSampleRate (newRate);
    }
}

float SPASynthVoice::baseValue (int destIndex) const
{
    return denorm (shared.baseNorm.data(), destIndex);
}

void SPASynthVoice::startNote (int midiNoteNumber, float noteVelocity,
                              juce::SynthesiserSound*, int currentPitchWheelPosition)
{
    const auto& lookup = DestLookup::get();

    currentNote = midiNoteNumber;
    velocity = noteVelocity;
    pitchBendSemitones = 2.0f * ((float) currentPitchWheelPosition - 8192.0f) / 8192.0f;

    // Portamento: glide from the last struck note (legato mode only when the
    // previous key was still held). The synthesiser hook updates lastNote
    // after this returns, so it still points at the note before this one.
    glideTarget = (float) midiNoteNumber;
    glideNote = glideTarget;
    glideRate = 0.0f;
    if (shared.glide != nullptr
        && shared.glideMode != params::GlideMode::off
        && shared.glide->lastNote >= 0.0f
        && shared.glideTimeMs > 0.0f
        && std::abs (shared.glide->lastNote - glideTarget) > 0.01f
        && (shared.glideMode == params::GlideMode::always
            || shared.glide->previousKeyHeld))
    {
        glideNote = shared.glide->lastNote;
        glideRate = (glideTarget - glideNote) / (shared.glideTimeMs * 0.001f);
    }

    // Voice-stack unison (Unison mode): spread this voice's pitch and pan by
    // its slot in the burst. pending* were set by the synthesiser right before
    // this startNote call.
    unisonDetuneSemis = 0.0f;
    unisonPanL = unisonPanR = 1.0f;
    paraSawGate = false;
    if (shared.voiceMode == params::VoiceMode::unison && shared.pendingUnisonCount > 1)
    {
        const int i = shared.pendingUnisonIndex, n = shared.pendingUnisonCount;
        const float spread = (float) i / (float) (n - 1) * 2.0f - 1.0f;   // -1..1
        unisonDetuneSemis = spread * shared.unisonDetuneCents / 100.0f;
        const float pan = juce::jlimit (-1.0f, 1.0f, spread * shared.unisonWidth);
        const float ang = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        unisonPanL = std::cos (ang) * juce::MathConstants<float>::sqrt2;
        unisonPanR = std::sin (ang) * juce::MathConstants<float>::sqrt2;
    }

    if (shared.telemetry != nullptr)
        noteSerial = shared.telemetry->noteSerial.fetch_add (1) + 1;

    for (int s = 0; s < params::numOscSlots; ++s)
    {
        const auto& stat = shared.slots[(size_t) s];
        oscs[(size_t) s].noteOn (stat.phaseMode, stat.phase, &random);
        samplePlayers[(size_t) s].noteOn (stat.sample, stat.sampleStart);
        granularPlayers[(size_t) s].noteOn();
        analogOscs[(size_t) s].noteOn();
        fmOscs[(size_t) s].noteOn();

        if (stat.mode == params::OscMode::pluck)
        {
            const auto hz = 440.0f * std::exp2 ((glideNote - 69.0f) / 12.0f);
            plucks[(size_t) s].noteOn (hz, random);
        }

        lastGrainPos[(size_t) s] = baseValue (lookup.slots[(size_t) s].grainPos);
    }

    for (int i = 0; i < params::numLFOs; ++i)
        lfos[(size_t) i].noteOn (shared.lfo[(size_t) i]);

    chaosGen.prepare (random);
    chaosDepth = baseValue (lookup.chaosDepth);
    chaosRate = baseValue (lookup.chaosRate);
    chaosMix = baseValue (lookup.chaosMix);

    filter.reset();
    filter2.reset();

    // Envelopes start from base (unmodulated) values; the first chunk refresh
    // applies modulation.
    ampEnv.setParameters ({ baseValue (lookup.ampA), baseValue (lookup.ampD),
                            baseValue (lookup.ampS), baseValue (lookup.ampR) });
    env2.setParameters ({ baseValue (lookup.env2A), baseValue (lookup.env2D),
                          baseValue (lookup.env2S), baseValue (lookup.env2R) });
    env3.setParameters ({ baseValue (lookup.env3A), baseValue (lookup.env3D),
                          baseValue (lookup.env3S), baseValue (lookup.env3R) });

    ampEnv.noteOn();
    env2.noteOn();
    env3.noteOn();
    ampEnvLast = 0.0f;
}

void SPASynthVoice::stopNote (float, bool allowTailOff)
{
    if (allowTailOff)
    {
        ampEnv.noteOff();
        env2.noteOff();
        env3.noteOff();
    }
    else
    {
        ampEnv.reset();
        env2.reset();
        env3.reset();
        clearCurrentNote();
    }
}

void SPASynthVoice::pitchWheelMoved (int newPitchWheelValue)
{
    pitchBendSemitones = 2.0f * ((float) newPitchWheelValue - 8192.0f) / 8192.0f;
}

void SPASynthVoice::computeChunk (int blockOffset, int chunkLen)
{
    const auto& lookup = DestLookup::get();
    const auto sampleRate = getSampleRate();

    // --- Portamento: ramp the sounding pitch toward the struck note ---------
    if (! juce::exactlyEqual (glideNote, glideTarget))
    {
        glideNote += glideRate * (float) chunkLen / (float) sampleRate;
        if ((glideRate >= 0.0f && glideNote >= glideTarget)
            || (glideRate <= 0.0f && glideNote <= glideTarget))
            glideNote = glideTarget;
    }

    // --- Organic Chaos walkers ----------------------------------------------
    // Advanced with last chunk's effective depth/rate/mix (chaos feeds the
    // matrix that can modulate those — one-chunk latency breaks the cycle).
    const auto& ch = shared.chaos;
    const auto chaosActive = ch.enabled && chaosMix > 0.0f && chaosDepth > 0.0f;
    const auto chaosScale = chaosActive ? chaosDepth * chaosMix : 0.0f;

    if (chaosActive)
        chaosGen.process (chaosRate, (double) chunkLen / sampleRate, random);

    // --- Modulation sources -------------------------------------------------
    float src[params::numModSources] {};
    src[(int) params::ModSource::env1] = ampEnvLast;
    src[(int) params::ModSource::chaos] = chaosGen.value (ChaosGenerator::matrixSource)
                                        * chaosScale;

    // Advance env2/3 through the chunk; their end value drives this chunk.
    float env2Value = 0.0f, env3Value = 0.0f;
    for (int i = 0; i < chunkLen; ++i)
    {
        env2Value = env2.getNextSample();
        env3Value = env3.getNextSample();
    }
    src[(int) params::ModSource::env2] = env2Value;
    src[(int) params::ModSource::env3] = env3Value;

    for (int i = 0; i < params::numLFOs; ++i)
    {
        const auto& p = shared.lfo[(size_t) i];
        const auto inc = (double) LFO::effectiveRateHz (p, shared.bpm) / sampleRate;
        const auto globalPhase = shared.lfoGlobalPhase[(size_t) i] + inc * blockOffset;
        src[(int) params::ModSource::lfo1 + i] =
            lfos[(size_t) i].processChunk (p, chunkLen, shared.bpm, globalPhase, random);
    }

    for (int m = 0; m < params::numMacros; ++m)
        src[(int) params::ModSource::macro1 + m] = shared.macros[(size_t) m];

    src[(int) params::ModSource::velocity]   = velocity;
    src[(int) params::ModSource::modWheel]   = shared.modWheel;
    src[(int) params::ModSource::aftertouch] = shared.aftertouch;

    // SFX followers: stream the offline analysis curves at each slot's
    // current playback position.
    for (int s = 0; s < params::numOscSlots; ++s)
    {
        const auto& stat = shared.slots[(size_t) s];
        if (! stat.enabled || stat.sample == nullptr
            || stat.mode == params::OscMode::wavetable)
            continue;

        const auto seconds = stat.mode == params::OscMode::sample
            ? samplePlayers[(size_t) s].positionSeconds (stat.sample)
            : (double) lastGrainPos[(size_t) s] * stat.sample->lengthSeconds();

        src[params::sfxFollowerBase + 2 * s]     = stat.sample->ampAt (seconds);
        src[params::sfxFollowerBase + 2 * s + 1] = stat.sample->pitchAt (seconds);
    }

    // --- Apply routes in normalized space -----------------------------------
    float eff[params::maxModDests];
    std::copy (shared.baseNorm.begin(),
               shared.baseNorm.begin() + params::numModDests(), eff);

    for (int r = 0; r < shared.numActiveRoutes; ++r)
    {
        const auto& route = shared.routes[(size_t) r];
        eff[route.destIndex] += src[route.source] * route.depth;
    }

    for (int d = 0; d < params::numModDests(); ++d)
        eff[d] = juce::jlimit (0.0f, 1.0f, eff[d]);

    // Cache effective chaos controls for next chunk's walker advance.
    chaosDepth = denorm (eff, lookup.chaosDepth);
    chaosRate = denorm (eff, lookup.chaosRate);
    chaosMix = denorm (eff, lookup.chaosMix);

    // Voice-wide chaos drives applied in the render loop.
    chaosAmpGain = chaosActive && ch.ampOn
                 ? 1.0f + chaosGen.value (ChaosGenerator::amp) * ch.ampAmount * 0.5f * chaosScale
                 : 1.0f;
    satDrive = chaosActive && ch.satOn
             ? ch.saturation * chaosScale
               * (0.6f + 0.4f * chaosGen.value (ChaosGenerator::saturation))
             : 0.0f;
    distDrive = chaosActive && ch.distOn
              ? ch.distortion * chaosScale
                * (0.6f + 0.4f * chaosGen.value (ChaosGenerator::distortion))
              : 0.0f;

    // --- Configure DSP from effective values --------------------------------
    for (int s = 0; s < params::numOscSlots; ++s)
    {
        const auto& stat = shared.slots[(size_t) s];
        if (! stat.enabled)
            continue;

        const auto& d = lookup.slots[(size_t) s];

        const auto chaosPitch = chaosActive && ch.pitchOn
                              ? chaosGen.slotPitch (s) * ch.pitchAmountCents * 0.01f * chaosScale
                              : 0.0f;
        const auto chaosPos = chaosActive && ch.positionOn
                            ? chaosGen.slotPosition (s) * ch.positionAmount * chaosScale
                            : 0.0f;
        const auto chaosPhase = chaosActive && ch.phaseOn
                              ? chaosGen.slotPhase (s) * ch.phaseAmount * chaosScale
                              : 0.0f;

        const auto pitchOffset = denorm (eff, d.coarse)
                               + denorm (eff, d.fine) * 0.01f
                               + pitchBendSemitones
                               + chaosPitch
                               + unisonDetuneSemis;

        if (stat.mode == params::OscMode::wavetable)
        {
            const auto note = glideNote + pitchOffset;

            oscs[(size_t) s].updateBlock ({
                stat.table,
                440.0f * std::exp2 ((note - 69.0f) / 12.0f),
                juce::jlimit (0.0f, 1.0f, denorm (eff, d.position) + chaosPos),
                stat.unisonCount,
                denorm (eff, d.unisonDetune),
                denorm (eff, d.unisonBlend),
                denorm (eff, d.unisonWidth),
                denorm (eff, d.pan),
                chaosPhase,
            });
        }
        else
        {
            // Sample/granular: keytrack transposes relative to the root note.
            const auto keySemis = stat.keytrack
                                ? glideNote - (float) stat.rootNote : 0.0f;

            // Equal-power pan for the stereo sample paths.
            const auto pan = juce::jlimit (-1.0f, 1.0f, denorm (eff, d.pan));
            const auto panAngle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
            slotPanL[(size_t) s] = std::cos (panAngle) * juce::MathConstants<float>::sqrt2;
            slotPanR[(size_t) s] = std::sin (panAngle) * juce::MathConstants<float>::sqrt2;

            if (stat.mode == params::OscMode::sample)
            {
                const auto ratio = std::exp2 ((keySemis + pitchOffset) / 12.0f);
                sampleParams[(size_t) s] = {
                    stat.sample,
                    (double) ratio * (stat.sample != nullptr
                                          ? stat.sample->sourceSampleRate / sampleRate : 1.0),
                    stat.loop,
                    (double) stat.loopStart,
                    (double) stat.loopEnd,
                };
            }
            else if (stat.mode == params::OscMode::analog
                  || stat.mode == params::OscMode::fm
                  || stat.mode == params::OscMode::noise
                  || stat.mode == params::OscMode::pluck)
            {
                const auto note = glideNote + pitchOffset;
                const auto hz = 440.0f * std::exp2 ((note - 69.0f) / 12.0f);

                slotPulseWidth[(size_t) s] = denorm (eff, d.pulseWidth);
                slotFMIndex[(size_t) s] = denorm (eff, d.fmIndex);
                slotPluckDamp[(size_t) s] = denorm (eff, d.pluckDamp);

                if (stat.mode == params::OscMode::analog)
                    analogOscs[(size_t) s].setFrequency (hz);
                else if (stat.mode == params::OscMode::fm)
                    fmOscs[(size_t) s].setFrequency (hz, stat.fmRatio);
                else if (stat.mode == params::OscMode::pluck)
                    plucks[(size_t) s].setFrequency (hz);
            }
            else
            {
                const auto grainPos = juce::jlimit (0.0f, 1.0f,
                                                    denorm (eff, d.grainPos) + chaosPos);
                lastGrainPos[(size_t) s] = grainPos;

                granularParams[(size_t) s] = {
                    stat.sample,
                    denorm (eff, d.grainSize) * 0.001f,  // ms -> s
                    denorm (eff, d.grainDensity),
                    grainPos,
                    denorm (eff, d.grainSpray),
                    (double) std::exp2 ((keySemis + stat.grainPitch + pitchOffset) / 12.0f),
                    sampleRate,
                };
            }
        }

        slotGains[(size_t) s] = juce::Decibels::decibelsToGain (denorm (eff, d.level), -60.0f);
    }

    // Cutoff with keytracking (relative to C3) and the hardwired Env 2
    // amount (+/- 4 octaves at full depth) - the classic filter-envelope
    // convenience found on every comparable synth.
    const auto envAmt = denorm (eff, lookup.filterEnvAmt);
    const auto keytrackOctaves = shared.filterKeytrack
                               * (glideNote - 60.0f) / 12.0f;
    const auto envOctaves = envAmt * env2Value * 4.0f;
    const auto finalCutoff = juce::jlimit (20.0f, 20000.0f,
        denorm (eff, lookup.cutoff) * std::exp2 (keytrackOctaves + envOctaves));

    filterMixValue = denorm (eff, lookup.filterMix);

    if (shared.filter1Enabled)
        filter.setParams (shared.filterType,
                          finalCutoff,
                          denorm (eff, lookup.resonance),
                          denorm (eff, lookup.drive));

    if (shared.filter2Enabled)
    {
        const auto f2EnvAmt = denorm (eff, lookup.f2EnvAmt);
        const auto f2Keytrack = shared.filter2Keytrack
                              * (glideNote - 60.0f) / 12.0f;
        const auto f2Cutoff = juce::jlimit (20.0f, 20000.0f,
            denorm (eff, lookup.f2Cutoff)
            * std::exp2 (f2Keytrack + f2EnvAmt * env2Value * 4.0f));

        filter2MixValue = denorm (eff, lookup.f2Mix);
        filter2.setParams (shared.filter2Type,
                           f2Cutoff,
                           denorm (eff, lookup.f2Resonance),
                           denorm (eff, lookup.f2Drive));
    }

    ampEnv.setParameters ({ denorm (eff, lookup.ampA), denorm (eff, lookup.ampD),
                            denorm (eff, lookup.ampS), denorm (eff, lookup.ampR) });
    env2.setParameters ({ denorm (eff, lookup.env2A), denorm (eff, lookup.env2D),
                          denorm (eff, lookup.env2S), denorm (eff, lookup.env2R) });
    env3.setParameters ({ denorm (eff, lookup.env3A), denorm (eff, lookup.env3D),
                          denorm (eff, lookup.env3S), denorm (eff, lookup.env3R) });

    // --- Telemetry: the newest active voice narrates its effective state ----
    if (auto* tel = shared.telemetry)
    {
        if (noteSerial >= tel->writerSerial.load (std::memory_order_relaxed))
        {
            tel->writerSerial.store (noteSerial, std::memory_order_relaxed);

            for (int s = 0; s < params::numOscSlots; ++s)
            {
                const auto& stat = shared.slots[(size_t) s];
                const auto slotChaosPos = chaosActive && ch.positionOn
                    ? chaosGen.slotPosition (s) * ch.positionAmount * chaosScale : 0.0f;
                float pos = 0.0f;
                if (stat.mode == params::OscMode::wavetable)
                    pos = juce::jlimit (0.0f, 1.0f,
                                        denorm (eff, lookup.slots[(size_t) s].position)
                                        + slotChaosPos);
                else if (stat.mode == params::OscMode::sample)
                    pos = stat.sample != nullptr
                        ? (float) juce::jlimit (0.0, 1.0,
                              samplePlayers[(size_t) s].positionSeconds (stat.sample)
                              / juce::jmax (0.001, stat.sample->lengthSeconds()))
                        : 0.0f;
                else
                {
                    pos = lastGrainPos[(size_t) s];

                    // Publish the live grain cloud for the animated playheads.
                    float gp[Telemetry::maxVizGrains], ga[Telemetry::maxVizGrains];
                    const int gn = granularPlayers[(size_t) s].snapshotGrains (
                        stat.sample, gp, ga, Telemetry::maxVizGrains);
                    auto& gv = tel->grainViz[(size_t) s];
                    for (int i = 0; i < gn; ++i)
                    {
                        gv.pos[(size_t) i].store (gp[i], std::memory_order_relaxed);
                        gv.amp[(size_t) i].store (ga[i], std::memory_order_relaxed);
                    }
                    gv.count.store (gn, std::memory_order_relaxed);
                }
                tel->slotPosition[(size_t) s].store (pos, std::memory_order_relaxed);
            }

            tel->filterCutoffHz.store (finalCutoff, std::memory_order_relaxed);
            tel->filterResonance.store (denorm (eff, lookup.resonance), std::memory_order_relaxed);
            if (shared.filter2Enabled)
            {
                const auto f2KeyOct = shared.filter2Keytrack * (glideNote - 60.0f) / 12.0f;
                tel->filter2CutoffHz.store (juce::jlimit (20.0f, 20000.0f,
                    denorm (eff, lookup.f2Cutoff)
                    * std::exp2 (f2KeyOct + denorm (eff, lookup.f2EnvAmt) * env2Value * 4.0f)),
                    std::memory_order_relaxed);
                tel->filter2Resonance.store (denorm (eff, lookup.f2Resonance),
                                             std::memory_order_relaxed);
            }

            tel->envValue[0].store (ampEnvLast, std::memory_order_relaxed);
            tel->envValue[1].store (src[(int) params::ModSource::env2], std::memory_order_relaxed);
            tel->envValue[2].store (src[(int) params::ModSource::env3], std::memory_order_relaxed);

            for (int i = 0; i < params::numLFOs; ++i)
            {
                tel->lfoValue[(size_t) i].store (src[(int) params::ModSource::lfo1 + i],
                                                 std::memory_order_relaxed);
                tel->lfoPhase[(size_t) i].store (lfos[(size_t) i].getLastBasePhase(),
                                                 std::memory_order_relaxed);
            }

            tel->chaosValue.store (src[(int) params::ModSource::chaos], std::memory_order_relaxed);
        }
    }
}

void SPASynthVoice::renderNextBlock (juce::AudioBuffer<float>& outputBuffer,
                                    int startSample, int numSamples)
{
    if (! ampEnv.isActive())
        return;

    auto* left = outputBuffer.getWritePointer (0, startSample);
    auto* right = outputBuffer.getNumChannels() > 1
                ? outputBuffer.getWritePointer (1, startSample) : nullptr;

    int rendered = 0;
    while (rendered < numSamples)
    {
        const auto chunkLen = juce::jmin (chunkSize, numSamples - rendered);
        computeChunk (startSample + rendered, chunkLen);

        for (int i = 0; i < chunkLen; ++i)
        {
            float sumL = 0.0f, sumR = 0.0f;

            for (int s = 0; s < params::numOscSlots; ++s)
            {
                const auto& stat = shared.slots[(size_t) s];
                if (! stat.enabled)
                    continue;

                float l = 0.0f, r = 0.0f;

                switch (stat.mode)
                {
                    case params::OscMode::wavetable:
                    {
                        const auto smp = oscs[(size_t) s].getNextSample();
                        l = smp.left;
                        r = smp.right;
                        break;
                    }
                    case params::OscMode::sample:
                    {
                        const auto smp = samplePlayers[(size_t) s]
                                             .getNextSample (sampleParams[(size_t) s]);
                        l = smp.left * slotPanL[(size_t) s];
                        r = smp.right * slotPanR[(size_t) s];
                        break;
                    }
                    case params::OscMode::granular:
                    {
                        const auto smp = granularPlayers[(size_t) s]
                                             .getNextSample (granularParams[(size_t) s], random);
                        l = smp.left * slotPanL[(size_t) s];
                        r = smp.right * slotPanR[(size_t) s];
                        break;
                    }
                    case params::OscMode::analog:
                    {
                        const auto v = analogOscs[(size_t) s].getNextSample (
                            (AnalogOscillator::Shape) stat.analogShape,
                            slotPulseWidth[(size_t) s]);
                        l = v * slotPanL[(size_t) s];
                        r = v * slotPanR[(size_t) s];
                        break;
                    }
                    case params::OscMode::fm:
                    {
                        const auto v = fmOscs[(size_t) s].getNextSample (
                            slotFMIndex[(size_t) s]);
                        l = v * slotPanL[(size_t) s];
                        r = v * slotPanR[(size_t) s];
                        break;
                    }
                    case params::OscMode::noise:
                    {
                        const auto v = noiseGens[(size_t) s].getNextSample (
                            (NoiseGenerator::Color) stat.noiseColor, random);
                        l = v * slotPanL[(size_t) s];
                        r = v * slotPanR[(size_t) s];
                        break;
                    }
                    case params::OscMode::pluck:
                    {
                        const auto v = plucks[(size_t) s].getNextSample (
                            slotPluckDamp[(size_t) s]);
                        l = v * slotPanL[(size_t) s];
                        r = v * slotPanR[(size_t) s];
                        break;
                    }
                }

                sumL += l * slotGains[(size_t) s];
                sumR += r * slotGains[(size_t) s];
            }

            // Paraphonic voices follow the processor's shared amp envelope; all
            // others use their own. ampEnv still advances so its release governs
            // when non-paraphonic voices free themselves.
            const auto ownEnv = ampEnv.getNextSample();
            const bool para = shared.voiceMode == params::VoiceMode::paraphonic
                              && shared.paraEnvBlock != nullptr;
            const auto envValue = para
                ? shared.paraEnvBlock[startSample + rendered + i]
                : ownEnv;
            if (para && shared.paraGateActive) paraSawGate = true;
            ampEnvLast = envValue;
            const auto gain = envValue * (0.2f + 0.8f * velocity) * chaosAmpGain;

            auto outL = shared.filter1Enabled
                      ? sumL + (filter.processSample (0, sumL) - sumL) * filterMixValue
                      : sumL;
            auto outR = right == nullptr ? 0.0f
                      : shared.filter1Enabled
                          ? sumR + (filter.processSample (1, sumR) - sumR) * filterMixValue
                          : sumR;

            if (shared.filter2Enabled)
            {
                if (shared.filterParallel)
                {
                    // Parallel: both filters see the raw mix; halved sum.
                    const auto pL = sumL + (filter2.processSample (0, sumL) - sumL)
                                           * filter2MixValue;
                    const auto pR = right != nullptr
                                  ? sumR + (filter2.processSample (1, sumR) - sumR)
                                           * filter2MixValue
                                  : 0.0f;
                    outL = 0.5f * (outL + pL);
                    outR = 0.5f * (outR + pR);
                }
                else
                {
                    // Series: filter 2 shapes filter 1's output.
                    outL = outL + (filter2.processSample (0, outL) - outL) * filter2MixValue;
                    if (right != nullptr)
                        outR = outR + (filter2.processSample (1, outR) - outR)
                                      * filter2MixValue;
                }
            }

            // Chaos saturation: warm tanh, level-compensated.
            if (satDrive > 0.001f)
            {
                const auto k = 1.0f + 5.0f * satDrive;
                const auto norm = 1.0f / std::tanh (k * 0.7f);
                outL = std::tanh (outL * k) * 0.7f * norm;
                outR = std::tanh (outR * k) * 0.7f * norm;
            }

            // Chaos distortion: harder cubic clip with a touch of asymmetry
            // for edgier, even-harmonic character.
            if (distDrive > 0.001f)
            {
                const auto k = 1.0f + 12.0f * distDrive;
                auto shape = [k] (float x)
                {
                    auto v = juce::jlimit (-1.0f, 1.0f, x * k * 0.5f + 0.08f * x * x);
                    return (1.5f * v - 0.5f * v * v * v) / (0.5f * k + 0.5f);
                };
                outL = shape (outL);
                outR = shape (outR);
            }

            const auto n = rendered + i;
            left[n] += outL * gain * unisonPanL;
            if (right != nullptr)
                right[n] += outR * gain * unisonPanR;

            // A paraphonic voice frees itself only once the shared gate has
            // released and its level has reached zero; otherwise the voice's own
            // amp envelope decides.
            const bool finished = para
                ? (paraSawGate && ! shared.paraGateActive && envValue < 1.0e-4f)
                : ! ampEnv.isActive();
            if (finished)
            {
                clearCurrentNote();
                return;
            }
        }

        rendered += chunkLen;
    }
}

} // namespace spa::dsp
