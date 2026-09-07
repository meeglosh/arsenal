#include "Arpeggiator.h"
#include <cmath>

namespace spa::dsp
{

// Pattern position from a step count. stepCounter follows the host's ppq on
// transport sync and is NEGATIVE before bar 1 (Logic's count-in / pre-roll),
// and C++ `%` keeps the dividend's sign, so a plain `stepCounter % length`
// produced a negative index into the pattern arrays -- an out-of-bounds read
// that crashed Logic whenever the arp ran during a count-in. Always wrap into
// [0, length).
static int wrapStep (int step, int length) noexcept
{
    length = juce::jmax (1, length);
    const auto r = step % length;
    return r < 0 ? r + length : r;
}

void Arpeggiator::prepare (double sampleRate)
{
    currentSampleRate = sampleRate;
    scratch.ensureSize (8192);
    reset();
}

void Arpeggiator::reset()
{
    numHeld = 0;
    numActive = 0;
    numPending = 0;
    arrivalCounter = 0;
    stepCounter = 0;
    walkIndex = 0;
    beatClock = 0.0;
    lastHostPpq = -1.0e9;
    latchedChordDown = false;
}

void Arpeggiator::addHeld (juce::uint8 note, juce::uint8 velocity)
{
    for (int i = 0; i < numHeld; ++i)
        if (held[i].note == note)
        {
            held[i].velocity = velocity;
            return;
        }

    if (numHeld < maxHeld)
        held[numHeld++] = { note, velocity, arrivalCounter++ };
}

void Arpeggiator::removeHeld (juce::uint8 note)
{
    for (int i = 0; i < numHeld; ++i)
    {
        if (held[i].note == note)
        {
            for (int j = i; j < numHeld - 1; ++j)
                held[j] = held[j + 1];
            --numHeld;
            return;
        }
    }
}

void Arpeggiator::releaseAllActive (juce::MidiBuffer& out, int samplePos)
{
    for (int i = 0; i < numActive; ++i)
        out.addEvent (juce::MidiMessage::noteOff (1, active[i].note), samplePos);
    numActive = 0;
    numPending = 0;   // unfired ratchet repeats die with their step
}

int Arpeggiator::pickNoteIndex (const Params& p, int sequenceLength)
{
    switch (p.mode)
    {
        case params::ArpMode::random:
            return random.nextInt (sequenceLength);

        case params::ArpMode::randomWalk:
        {
            walkIndex += random.nextBool() ? 1 : -1;
            walkIndex = (walkIndex % sequenceLength + sequenceLength) % sequenceLength;
            return walkIndex;
        }

        case params::ArpMode::up: case params::ArpMode::down:
        case params::ArpMode::upDown: case params::ArpMode::downUp:
        case params::ArpMode::upDownInclusive: case params::ArpMode::converge:
        case params::ArpMode::diverge: case params::ArpMode::asPlayed:
        case params::ArpMode::chord: case params::ArpMode::phrase:
        default:
            return wrapStep (stepCounter, sequenceLength);
    }
}

void Arpeggiator::triggerStep (juce::MidiBuffer& out, int samplePos, const Params& p,
                               double stepBeat, double beatsPerStep)
{
    if (numHeld == 0)
        return;

    // Chance: a skipped step is a rest — the pattern position still advances.
    if (p.chance < 0.999f && random.nextFloat() >= p.chance)
    {
        ++stepCounter;
        return;
    }

    // Sorted-by-pitch view of the held notes (insertion sort into a small
    // local array — numHeld <= 32).
    int sorted[maxHeld];
    for (int i = 0; i < numHeld; ++i)
        sorted[i] = i;
    for (int i = 1; i < numHeld; ++i)
        for (int j = i; j > 0 && held[sorted[j]].note < held[sorted[j - 1]].note; --j)
            std::swap (sorted[j], sorted[j - 1]);

    // Stutter: the whole step ratchets into 2-4 equal repeats. Jump: the
    // step (repeats included) lands an octave away. Both roll once per step.
    const auto repeats = (p.stutter > 0.0f && random.nextFloat() < p.stutter)
                       ? 2 + random.nextInt (3) : 1;
    const auto subBeats = beatsPerStep / (double) repeats;
    const auto jumpSemis = (p.jump > 0.0f && random.nextFloat() < p.jump)
                         ? (random.nextBool() ? 12 : -12) : 0;

    const auto velocityFor = [&] (juce::uint8 played, int step, int patternLen) -> juce::uint8
    {
        if (p.velocityMode == 1)
            return 100;
        if (p.velocityMode == 2)
            return wrapStep (step, patternLen) == 0 ? 127 : 88;
        return played;
    };

    // Humanize rolls per hit, so ratchet repeats breathe individually.
    const auto humanized = [&] (juce::uint8 v) -> juce::uint8
    {
        if (p.humanize <= 0.0f)
            return v;
        const auto delta = (int) std::lround ((random.nextFloat() * 2.0f - 1.0f)
                                              * p.humanize * 48.0f);
        return (juce::uint8) juce::jlimit (1, 127, (int) v + delta);
    };

    const auto emit = [&] (int midiNote, juce::uint8 velocity)
    {
        const auto note = (juce::uint8) juce::jlimit (0, 127, midiNote + jumpSemis);
        if (numActive < maxActive)
        {
            out.addEvent (juce::MidiMessage::noteOn (1, note, humanized (velocity)),
                          samplePos);
            active[numActive++] = { note, stepBeat + (double) p.gate * subBeats * 0.98 };
        }

        // Later repeats usually land past this block; schedule them by beat.
        for (int r = 1; r < repeats && numPending < maxPending; ++r)
            pending[numPending++] = { note, humanized (velocity),
                                      stepBeat + (double) r * subBeats,
                                      stepBeat + ((double) r + (double) p.gate * 0.98)
                                          * subBeats };
    };

    if (p.mode == params::ArpMode::chord)
    {
        for (int i = 0; i < numHeld; ++i)
            emit (held[i].note, velocityFor (held[i].velocity, stepCounter, 2));
        ++stepCounter;
        return;
    }

    if (p.mode == params::ArpMode::phrase)
    {
        const auto& phrase = params::arpPhrases()[(size_t) juce::jlimit (
            0, (int) params::arpPhrases().size() - 1, p.phrase)];
        const auto step = wrapStep (stepCounter, phrase.length * juce::jmax (1, p.octaves));
        const auto interval = phrase.intervals[step % phrase.length];
        const auto octave = 12 * (step / phrase.length);
        const auto base = held[sorted[0]];
        emit (base.note + interval + octave,
              velocityFor (base.velocity, step, phrase.length));
        ++stepCounter;
        return;
    }

    // Build the pattern order over held notes x octaves.
    const auto n = numHeld;
    const auto octaves = juce::jmax (1, p.octaves);
    const auto spanLength = n * octaves;

    const auto noteAt = [&] (int position) -> const Held&
    {
        return held[sorted[wrapStep (position, n)]];
    };
    const auto pitchAt = [&] (int position)
    {
        return (int) noteAt (position).note + 12 * (position / n);
    };

    int sequencePosition = 0;
    int sequenceLength = spanLength;

    switch (p.mode)
    {
        case params::ArpMode::up:
            sequencePosition = pickNoteIndex (p, spanLength);
            break;

        case params::ArpMode::down:
            sequencePosition = spanLength - 1 - wrapStep (stepCounter, spanLength);
            break;

        case params::ArpMode::upDown:
        {
            sequenceLength = juce::jmax (1, 2 * spanLength - 2);
            const auto step = pickNoteIndex (p, sequenceLength);
            sequencePosition = step < spanLength ? step : 2 * spanLength - 2 - step;
            break;
        }

        case params::ArpMode::downUp:
        {
            sequenceLength = juce::jmax (1, 2 * spanLength - 2);
            const auto step = pickNoteIndex (p, sequenceLength);
            const auto up = step < spanLength ? step : 2 * spanLength - 2 - step;
            sequencePosition = spanLength - 1 - up;
            break;
        }

        case params::ArpMode::upDownInclusive:
        {
            sequenceLength = 2 * spanLength;
            const auto step = pickNoteIndex (p, sequenceLength);
            sequencePosition = step < spanLength ? step : 2 * spanLength - 1 - step;
            break;
        }

        case params::ArpMode::converge:
        {
            const auto step = pickNoteIndex (p, spanLength);
            sequencePosition = (step % 2 == 0) ? step / 2
                                               : spanLength - 1 - step / 2;
            break;
        }

        case params::ArpMode::diverge:
        {
            const auto step = pickNoteIndex (p, spanLength);
            const auto centre = spanLength / 2;
            sequencePosition = (step % 2 == 0) ? centre + step / 2
                                               : centre - 1 - step / 2;
            sequencePosition = ((sequencePosition % spanLength) + spanLength) % spanLength;
            break;
        }

        case params::ArpMode::asPlayed:
        {
            // Arrival order instead of pitch order.
            int byArrival[maxHeld];
            for (int i = 0; i < n; ++i)
                byArrival[i] = i;
            for (int i = 1; i < n; ++i)
                for (int j = i; j > 0 && held[byArrival[j]].arrivalOrder
                                         < held[byArrival[j - 1]].arrivalOrder; --j)
                    std::swap (byArrival[j], byArrival[j - 1]);

            const auto step = pickNoteIndex (p, spanLength);
            const auto& h = held[byArrival[wrapStep (step, n)]];
            emit ((int) h.note + 12 * (step / n),
                  velocityFor (h.velocity, step, spanLength));
            ++stepCounter;
            return;
        }

        case params::ArpMode::random:
        case params::ArpMode::randomWalk:
            sequencePosition = pickNoteIndex (p, spanLength);
            break;

        case params::ArpMode::chord:
        case params::ArpMode::phrase:
        default:
            sequencePosition = wrapStep (stepCounter, spanLength);
            break;
    }

    const auto& source = noteAt (sequencePosition);
    emit (pitchAt (sequencePosition),
          velocityFor (source.velocity, stepCounter, sequenceLength));
    ++stepCounter;
}

void Arpeggiator::process (juce::MidiBuffer& midi, int numSamples, const Params& p)
{
    if (! p.enable)
    {
        if (wasEnabled)
        {
            // Arp turned off: release anything still sounding, keep the
            // original buffer flowing.
            scratch.clear();
            releaseAllActive (scratch, 0);
            scratch.addEvents (midi, 0, numSamples, 0);
            midi.swapWith (scratch);
            reset();
        }
        wasEnabled = false;
        return;
    }
    wasEnabled = true;

    scratch.clear();
    auto& out = scratch;

    // --- Absorb incoming note events, pass everything else through ----------
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();

        if (message.isNoteOn())
        {
            if (p.latch && ! latchedChordDown)
                numHeld = 0;   // new chord replaces the latched one
            latchedChordDown = true;
            addHeld ((juce::uint8) message.getNoteNumber(),
                     (juce::uint8) juce::jmax (1, (int) (message.getVelocity())));
        }
        else if (message.isNoteOff())
        {
            if (! p.latch)
                removeHeld ((juce::uint8) message.getNoteNumber());
            // With latch: keep the notes; just track physical key state.
        }
        else
        {
            out.addEvent (message, metadata.samplePosition);
        }
    }

    // Latch bookkeeping: when no physical keys remain down, the next chord
    // starts fresh. (Approximation: any noteOff this block may have lifted
    // the last key; precise per-key tracking below.)
    if (p.latch)
    {
        bool anyOffThisBlock = false;
        for (const auto metadata : midi)
            if (metadata.getMessage().isNoteOff())
                anyOffThisBlock = true;
        if (anyOffThisBlock)
            latchedChordDown = false;
    }

    if (numSamples <= 0)
    {
        // Zero-sample flush block (some hosts send these): nothing to schedule,
        // and jlimit (0, numSamples - 1, ...) below would be an invalid range
        // (min > max). Held-note/latch state above is already up to date.
        midi.swapWith (out);
        return;
    }

    // --- Beat clock ----------------------------------------------------------
    const auto beatsPerStep = (double) params::lfoDivisionBeats (p.division);
    const auto samplesPerBeat = currentSampleRate * 60.0 / juce::jmax (1.0, p.bpm);
    const auto blockBeats = (double) numSamples / samplesPerBeat;

    double blockStartBeat;
    // A host can transiently report a non-finite ppq (tempo-map edits, corrupt
    // sessions); NaN beats would make the step-scan loop's exit condition below
    // never true, hanging the audio thread. Fall back to the free-running
    // internal beat clock in that case, same as when hostPlaying is false.
    if (p.hostPlaying && std::isfinite (p.ppqAtBlockStart))
    {
        // Follow the host; resync on jumps (loop points, relocations).
        if (std::abs (p.ppqAtBlockStart - lastHostPpq) > blockBeats * 4.0 + 0.25)
        {
            stepCounter = (int) std::floor (p.ppqAtBlockStart / beatsPerStep + 1.0e-6);
            numPending = 0;   // scheduled repeats are stale after a relocation
        }
        blockStartBeat = p.ppqAtBlockStart;
        lastHostPpq = p.ppqAtBlockStart + blockBeats;
        beatClock = blockStartBeat + blockBeats;
    }
    else
    {
        blockStartBeat = beatClock;
        beatClock += blockBeats;
    }
    const auto blockEndBeat = blockStartBeat + blockBeats;

    // --- Note-offs due inside this block --------------------------------------
    for (int i = numActive - 1; i >= 0; --i)
    {
        if (active[i].offBeat < blockEndBeat)
        {
            const auto beatOffset = juce::jmax (0.0, active[i].offBeat - blockStartBeat);
            const auto pos = juce::jlimit (0, numSamples - 1,
                                           (int) (beatOffset * samplesPerBeat));
            out.addEvent (juce::MidiMessage::noteOff (1, active[i].note), pos);
            active[i] = active[--numActive];
        }
    }

    // --- Ratchet repeats due inside this block ---------------------------------
    for (int i = numPending - 1; i >= 0; --i)
    {
        if (pending[i].onBeat < blockEndBeat)
        {
            const auto beatOffset = juce::jmax (0.0, pending[i].onBeat - blockStartBeat);
            const auto pos = juce::jlimit (0, numSamples - 1,
                                           (int) (beatOffset * samplesPerBeat));
            if (numActive < maxActive)
            {
                out.addEvent (juce::MidiMessage::noteOn (1, pending[i].note,
                                                         pending[i].velocity), pos);
                active[numActive++] = { pending[i].note, pending[i].offBeat };
            }
            pending[i] = pending[--numPending];
        }
    }

    if (numHeld == 0 && numActive == 0 && numPending == 0)
    {
        midi.swapWith (out);
        return;
    }

    // --- Step triggers inside this block (with swing on odd steps) ------------
    // Start one step early: a swung (odd) step's delayed trigger can land inside
    // this block even though its un-swung base beat sits just before
    // blockStartBeat. The `trigBeat < blockStartBeat` guard below drops it if it
    // truly precedes the block, and a step already fired last block has
    // trigBeat < blockStartBeat here too, so this can never double-fire. Without
    // the -1, swung steps that straddled a block boundary were skipped entirely
    // and swing sounded broken (dropped notes).
    const auto firstStep = (int) std::ceil (blockStartBeat / beatsPerStep - 1.0e-9) - 1;
    for (int k = firstStep; ; ++k)
    {
        auto trigBeat = (double) k * beatsPerStep;
        if (k % 2 != 0)
            trigBeat += (double) p.swing * beatsPerStep;

        if (trigBeat >= blockEndBeat)
            break;
        if (trigBeat < blockStartBeat)
            continue;

        const auto pos = juce::jlimit (0, numSamples - 1,
                                       (int) ((trigBeat - blockStartBeat) * samplesPerBeat));
        triggerStep (out, pos, p, trigBeat, beatsPerStep);
    }

    midi.swapWith (out);
}

} // namespace spa::dsp
