#include "Displays.h"
#include "../SPASynthProcessor.h"
#include "../dsp/FXChain.h"
#include "../dsp/ParametricEQ.h"

namespace spa::ui
{

// ========================== DisplayComponent ===============================

DisplayComponent::DisplayComponent (juce::AudioProcessorValueTreeState& state,
                                    juce::StringArray paramIDs,
                                    const dsp::Telemetry* tel)
    : apvts (state), telemetry (tel), watched (std::move (paramIDs))
{
    setInterceptsMouseClicks (false, false);
    for (const auto& id : watched)
        apvts.addParameterListener (id, this);
    startTimerHz (24);
}

DisplayComponent::~DisplayComponent()
{
    for (const auto& id : watched)
        apvts.removeParameterListener (id, this);
}

bool DisplayComponent::isLive() const
{
    return telemetry != nullptr
        && telemetry->activeVoices.load (std::memory_order_relaxed) > 0;
}

float DisplayComponent::value (const juce::String& paramID) const
{
    auto* param = apvts.getParameter (paramID);
    return param != nullptr ? param->convertFrom0to1 (param->getValue()) : 0.0f;
}

void DisplayComponent::timerCallback()
{
    if (dirty.exchange (false) || isLive())
        repaint();
}

void DisplayComponent::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    draw::displayWell (g, bounds);
    paintDisplay (g, bounds.reduced (3.0f));
}

// A background load is in flight for this slot: sweeping accent bar +
// caption. Callers markDirty() after painting this so the 24 Hz display
// timer keeps the sweep moving until the load lands.
static void paintLoadingOverlay (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto& t = currentTheme();

    const auto phase = (float) (juce::Time::getMillisecondCounter() % 1200u) / 1200.0f;
    const auto barWidth = area.getWidth() * 0.18f;
    const auto x = area.getX() - barWidth + phase * (area.getWidth() + barWidth);

    g.setGradientFill (juce::ColourGradient (
        t.accent.withAlpha (0.0f),  x, 0.0f,
        t.accent.withAlpha (0.35f), x + barWidth, 0.0f, false));
    g.fillRect (juce::Rectangle<float> (x, area.getY(), barWidth, area.getHeight())
                    .getIntersection (area));

    g.setColour (t.textSecondary);
    g.setFont (metrics::labelFont());
    g.drawText ("loading...", area.toNearestInt(), juce::Justification::centred);
}

// ============================ WaveDisplay ==================================

WaveDisplay::WaveDisplay (SPASynthProcessor& p, int slotIndex)
    : DisplayComponent (p.getAPVTS(),
                        { params::id::oscSlot (slotIndex, params::id::osc::mode),
                          params::id::oscSlot (slotIndex, params::id::osc::position),
                          params::id::oscSlot (slotIndex, params::id::osc::grainPos),
                          params::id::oscSlot (slotIndex, params::id::osc::sampleStart),
                          params::id::oscSlot (slotIndex, params::id::osc::loop),
                          params::id::oscSlot (slotIndex, params::id::osc::loopStart),
                          params::id::oscSlot (slotIndex, params::id::osc::loopEnd),
                          params::id::oscSlot (slotIndex, params::id::osc::analogShape),
                          params::id::oscSlot (slotIndex, params::id::osc::pulseWidth),
                          params::id::oscSlot (slotIndex, params::id::osc::fmRatio),
                          params::id::oscSlot (slotIndex, params::id::osc::fmIndex),
                          params::id::oscSlot (slotIndex, params::id::osc::noiseColor),
                          params::id::oscSlot (slotIndex, params::id::osc::pluckDamp) },
                        &p.getTelemetry()),
      processor (p), slot (slotIndex)
{
    processor.addChangeListener (this);
}

WaveDisplay::~WaveDisplay()
{
    processor.removeChangeListener (this);
}

void WaveDisplay::changeListenerCallback (juce::ChangeBroadcaster*)
{
    markDirty();
}

void WaveDisplay::paintDisplay (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto& t = currentTheme();
    const auto mode = (params::OscMode) (int) value (
        params::id::oscSlot (slot, params::id::osc::mode));

    if (mode == params::OscMode::wavetable)
    {
        if (processor.isWavetableLoading (slot))
        {
            paintLoadingOverlay (g, area);
            markDirty();   // keep the 24 Hz timer animating the sweep
            return;
        }

        const auto table = processor.getWavetable (slot);
        if (table == nullptr || table->getNumFrames() == 0)
            return;

        // Live (modulated) position while playing, knob position otherwise.
        const auto position = isLive()
            ? telemetry->slotPosition[(size_t) slot].load (std::memory_order_relaxed)
            : value (params::id::oscSlot (slot, params::id::osc::position));

        const auto framePos = juce::jlimit (0.0f, 1.0f, position)
                            * (float) (table->getNumFrames() - 1);
        const auto frameA = juce::jmin ((int) framePos, table->getNumFrames() - 1);
        const auto frameB = juce::jmin (frameA + 1, table->getNumFrames() - 1);
        const auto frac = framePos - (float) frameA;
        const auto* a = table->getFrame (0, frameA);
        const auto* b = table->getFrame (0, frameB);

        juce::Path wave;
        constexpr int steps = 128;
        for (int i = 0; i <= steps; ++i)
        {
            const auto idx = (int) ((float) i / steps * (dsp::Wavetable::tableSize - 1));
            const auto sample = a[idx] + frac * (b[idx] - a[idx]);
            const auto x = area.getX() + area.getWidth() * (float) i / steps;
            const auto y = area.getCentreY() - sample * area.getHeight() * 0.42f;
            if (i == 0)
                wave.startNewSubPath (x, y);
            else
                wave.lineTo (x, y);
        }

        draw::glowStroke (g, wave, t.accent);
        return;
    }

    // Synthesis engines: draw an ideal-cycle preview.
    if (mode == params::OscMode::analog || mode == params::OscMode::fm
        || mode == params::OscMode::noise || mode == params::OscMode::pluck)
    {
        namespace osc = params::id::osc;
        juce::Random previewRng (17 + slot);
        float brown = 0.0f;

        juce::Path wave;
        constexpr int steps = 160;
        for (int i = 0; i <= steps; ++i)
        {
            const auto ph = (float) i / steps;
            float v = 0.0f;

            if (mode == params::OscMode::analog)
            {
                const auto shape = (int) value (params::id::oscSlot (slot, osc::analogShape));
                const auto pw = value (params::id::oscSlot (slot, osc::pulseWidth));
                switch (shape)
                {
                    case 0:  v = 2.0f * ph - 1.0f; break;
                    case 1:  v = ph < 0.5f ? 1.0f : -1.0f; break;
                    case 2:  v = ph < pw ? 1.0f : -1.0f; break;
                    case 3:  v = 1.0f - 4.0f * std::abs (ph - 0.5f); break;
                    default: v = std::sin (juce::MathConstants<float>::twoPi * ph); break;
                }
            }
            else if (mode == params::OscMode::fm)
            {
                const auto ratio = value (params::id::oscSlot (slot, osc::fmRatio));
                const auto index = value (params::id::oscSlot (slot, osc::fmIndex));
                v = std::sin (juce::MathConstants<float>::twoPi * ph
                              + index * std::sin (juce::MathConstants<float>::twoPi * ph * ratio));
            }
            else if (mode == params::OscMode::noise)
            {
                const auto colour = (int) value (params::id::oscSlot (slot, osc::noiseColor));
                const auto white = previewRng.nextFloat() * 2.0f - 1.0f;
                if (colour == 2)
                {
                    brown = juce::jlimit (-1.0f, 1.0f, brown * 0.9f + white * 0.35f);
                    v = brown;
                }
                else if (colour == 1)
                {
                    brown = brown * 0.5f + white * 0.5f;
                    v = brown;
                }
                else
                    v = white;
            }
            else   // pluck: decaying burst
            {
                const auto damp = value (params::id::oscSlot (slot, osc::pluckDamp));
                const auto white = previewRng.nextFloat() * 2.0f - 1.0f;
                v = white * std::exp (-(3.5f - 2.5f * damp) * ph);
            }

            const auto x = area.getX() + area.getWidth() * ph;
            const auto y = area.getCentreY() - v * area.getHeight() * 0.42f;
            if (i == 0)
                wave.startNewSubPath (x, y);
            else
                wave.lineTo (x, y);
        }

        draw::glowStroke (g, wave, t.accent);
        return;
    }

    // Sample / granular: waveform overview.
    const auto sample = processor.getSample (slot);
    const bool loading = processor.isSampleLoading (slot);
    if (sample == nullptr || sample->lengthSamples() < 2)
    {
        if (loading)
        {
            paintLoadingOverlay (g, area);
            markDirty();   // keep the 24 Hz timer animating the sweep
            return;
        }

        g.setColour (t.textSecondary.withAlpha (0.6f));
        g.setFont (metrics::labelFont());
        g.drawText ("no SFX loaded", area.toNearestInt(), juce::Justification::centred);
        return;
    }

    const auto* audio = sample->audio.getReadPointer (0);
    const auto numSamples = sample->lengthSamples();
    const auto columns = juce::jmax (32, (int) area.getWidth() / 2);

    juce::Path fill;
    fill.startNewSubPath (area.getX(), area.getCentreY());
    const auto columnPeak = [&] (int c)
    {
        const auto start = (int) ((juce::int64) c * numSamples / columns);
        const auto end = (int) ((juce::int64) (c + 1) * numSamples / columns);
        float peak = 0.0f;
        const auto stride = juce::jmax (1, (end - start) / 64);
        for (int i = start; i < end; i += stride)
            peak = juce::jmax (peak, std::abs (audio[i]));
        return peak;
    };

    for (int c = 0; c < columns; ++c)
        fill.lineTo (area.getX() + area.getWidth() * (float) c / (float) (columns - 1),
                     area.getCentreY() - columnPeak (c) * area.getHeight() * 0.46f);
    for (int c = columns - 1; c >= 0; --c)
        fill.lineTo (area.getX() + area.getWidth() * (float) c / (float) (columns - 1),
                     area.getCentreY() + columnPeak (c) * area.getHeight() * 0.46f);
    fill.closeSubPath();

    g.setColour (t.accent.withAlpha (0.55f));
    g.fillPath (fill);

    const auto markerX = [&] (float norm)
    {
        return area.getX() + area.getWidth() * juce::jlimit (0.0f, 1.0f, norm);
    };

    // Sample mode: shade the loop region (while LOOP is on) and mark its
    // edges, so a long SFX's loop points read directly on the waveform
    // (tester request from Paul). sampleStart/loopStart/loopEnd are all
    // normalized 0..1 fractions of the file -- SamplePlayer::getNextSample
    // multiplies loopStartNorm/loopEndNorm by lengthSamples() the same way
    // noteOn() scales startNorm (source/dsp/SamplePlayer.h) -- the same
    // domain markerX already uses for the waveform columns, so no extra
    // mapping is needed. Granular has its own grain-viz below and is left
    // untouched. Drawn under the tick/playhead lines below so those stay
    // crisp on top.
    if (mode == params::OscMode::sample)
    {
        const auto loopOn = value (params::id::oscSlot (slot, params::id::osc::loop)) >= 0.5f;
        if (loopOn)
        {
            const auto lx0 = markerX (value (params::id::oscSlot (slot, params::id::osc::loopStart)));
            const auto lx1 = markerX (value (params::id::oscSlot (slot, params::id::osc::loopEnd)));
            const auto bandX = juce::jmin (lx0, lx1);
            const auto bandW = std::abs (lx1 - lx0);

            g.setColour (t.accentMod.withAlpha (0.14f));
            g.fillRect (juce::Rectangle<float> (bandX, area.getY(), bandW, area.getHeight()));

            g.setColour (t.accentMod.withAlpha (0.85f));
            g.drawLine (lx0, area.getY(), lx0, area.getBottom(), 1.0f);
            g.drawLine (lx1, area.getY(), lx1, area.getBottom(), 1.0f);
        }
    }

    // Granular while sounding: animate the live grain cloud (each grain is a
    // faint playhead scanning the buffer, brightness following its window) so
    // playback reads the way it does in other granular synths, plus a soft
    // centre marker at the grain position.
    if (mode == params::OscMode::granular && isLive() && telemetry != nullptr)
    {
        const auto& gv = telemetry->grainViz[(size_t) slot];
        const auto n = juce::jlimit (0, dsp::Telemetry::maxVizGrains,
                                     gv.count.load (std::memory_order_relaxed));
        for (int i = 0; i < n; ++i)
        {
            const auto gx = markerX (gv.pos[(size_t) i].load (std::memory_order_relaxed));
            const auto gamp = gv.amp[(size_t) i].load (std::memory_order_relaxed);
            g.setColour (t.accent.withAlpha (0.15f + 0.55f * gamp));
            g.drawLine (gx, area.getY(), gx, area.getBottom(), 1.0f);
        }

        const auto cx = markerX (telemetry->slotPosition[(size_t) slot]
                                     .load (std::memory_order_relaxed));
        g.setColour (t.textPrimary.withAlpha (0.55f));
        g.drawLine (cx, area.getY(), cx, area.getBottom(), 1.0f);
    }
    else
    {
        // Sample playhead (live) or, when idle / granular-idle, the knob.
        const auto markerParam = mode == params::OscMode::granular
                               ? params::id::osc::grainPos : params::id::osc::sampleStart;
        const auto live = isLive();
        const auto marker = live
            ? telemetry->slotPosition[(size_t) slot].load (std::memory_order_relaxed)
            : value (params::id::oscSlot (slot, markerParam));

        if (mode == params::OscMode::sample && ! live)
        {
            // Idle sample mode: this is the START point, not a moving
            // playhead -- draw it as a short, subtle top tick (textSecondary)
            // rather than a bright full-height line so it doesn't compete
            // with the crisper loop start/end markers above.
            const auto sx = markerX (marker);
            g.setColour (t.textSecondary.withAlpha (0.9f));
            g.drawLine (sx, area.getY(), sx,
                       area.getY() + juce::jmin (10.0f, area.getHeight() * 0.3f), 1.0f);
        }
        else
        {
            g.setColour (t.textPrimary);
            g.drawLine (markerX (marker), area.getY(), markerX (marker), area.getBottom(), 1.2f);
        }
    }

    // A replacement is still loading: dim the stale waveform so it reads as
    // outgoing, and run the sweep on top.
    if (loading)
    {
        g.setColour (t.background.withAlpha (0.6f));
        g.fillRect (area);
        paintLoadingOverlay (g, area);
        markDirty();   // keep the 24 Hz timer animating the sweep
    }
}

// ============================ EnvDisplay ===================================

EnvDisplay::EnvDisplay (SPASynthProcessor& p, juce::String idPrefix, int envIndex)
    : DisplayComponent (p.getAPVTS(),
                        { idPrefix + ".attack", idPrefix + ".decay",
                          idPrefix + ".sustain", idPrefix + ".release" },
                        &p.getTelemetry()),
      prefix (std::move (idPrefix)), env (envIndex)
{
}

void EnvDisplay::paintDisplay (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto& t = currentTheme();

    // Live level bar on the right edge.
    if (isLive())
    {
        const auto level = juce::jlimit (0.0f, 1.0f,
            telemetry->envValue[(size_t) env].load (std::memory_order_relaxed));
        auto bar = area.removeFromRight (5.0f);
        g.setColour (t.knobTrack.withAlpha (0.6f));
        g.fillRect (bar);
        g.setColour (t.accentMod);
        g.fillRect (bar.removeFromBottom (bar.getHeight() * level));
        area.removeFromRight (3.0f);
    }

    // Perceptual (sqrt) time scaling keeps short envelopes readable.
    const auto a = std::sqrt (value (prefix + ".attack") / 10.0f);
    const auto d = std::sqrt (value (prefix + ".decay") / 10.0f);
    const auto s = value (prefix + ".sustain");
    const auto r = std::sqrt (value (prefix + ".release") / 20.0f);
    const auto total = juce::jmax (0.05f, a + d + 0.25f + r);

    const auto xAt = [&] (float seg) { return area.getX() + area.getWidth() * seg / total; };
    const auto yAt = [&] (float level) { return area.getBottom() - level * area.getHeight() * 0.92f; };

    juce::Path curve;
    curve.startNewSubPath (area.getX(), yAt (0.0f));
    curve.quadraticTo (xAt (a * 0.4f), yAt (0.85f), xAt (a), yAt (1.0f));
    curve.quadraticTo (xAt (a + d * 0.3f), yAt (s + (1.0f - s) * 0.25f), xAt (a + d), yAt (s));
    curve.lineTo (xAt (a + d + 0.25f), yAt (s));
    curve.quadraticTo (xAt (a + d + 0.25f + r * 0.3f), yAt (s * 0.25f), area.getRight(), yAt (0.0f));

    auto fill = curve;
    fill.lineTo (area.getRight(), area.getBottom());
    fill.lineTo (area.getX(), area.getBottom());
    fill.closeSubPath();

    g.setColour (t.accentMod.withAlpha (0.18f));
    g.fillPath (fill);
    draw::glowStroke (g, curve, t.accentMod, 1.6f);
}

// ============================ LFODisplay ===================================

LFODisplay::LFODisplay (SPASynthProcessor& p, int lfoIndex)
    : DisplayComponent (p.getAPVTS(),
                        { params::id::lfoParam (lfoIndex, params::id::lfo::shape),
                          params::id::lfoParam (lfoIndex, params::id::lfo::phase),
                          params::id::lfoParam (lfoIndex, params::id::lfo::unipolar) },
                        &p.getTelemetry()),
      lfo (lfoIndex)
{
}

void LFODisplay::paintDisplay (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto& t = currentTheme();
    const auto shape = (params::LFOShape) (int) value (
        params::id::lfoParam (lfo, params::id::lfo::shape));
    const auto phaseOffset = value (params::id::lfoParam (lfo, params::id::lfo::phase));
    const auto unipolar = value (params::id::lfoParam (lfo, params::id::lfo::unipolar)) >= 0.5f;

    juce::Random shRandom (42 + lfo);  // stable S&H preview
    float shValue = shRandom.nextFloat() * 2.0f - 1.0f;
    int shCycle = 0;

    juce::Path curve;
    constexpr int steps = 160;
    for (int i = 0; i <= steps; ++i)
    {
        const auto raw = (float) i / steps + phaseOffset;
        const auto ph = raw - std::floor (raw);

        float v = 0.0f;
        switch (shape)
        {
            case params::LFOShape::sine:     v = std::sin (juce::MathConstants<float>::twoPi * ph); break;
            case params::LFOShape::triangle: v = 1.0f - 4.0f * std::abs (ph - 0.5f); break;
            case params::LFOShape::sawUp:    v = 2.0f * ph - 1.0f; break;
            case params::LFOShape::sawDown:  v = 1.0f - 2.0f * ph; break;
            case params::LFOShape::square:   v = ph < 0.5f ? 1.0f : -1.0f; break;
            case params::LFOShape::sampleHold:
            {
                const auto cycle = (int) (raw * 6.0f);
                if (cycle != shCycle)
                {
                    shCycle = cycle;
                    shValue = shRandom.nextFloat() * 2.0f - 1.0f;
                }
                v = shValue;
                break;
            }
        }

        if (unipolar)
            v = v * 0.5f + 0.5f;

        const auto x = area.getX() + area.getWidth() * (float) i / steps;
        const auto y = area.getCentreY() - v * area.getHeight() * 0.42f;
        if (i == 0)
            curve.startNewSubPath (x, y);
        else
            curve.lineTo (x, y);
    }

    draw::glowStroke (g, curve, t.accentMod, 1.6f);

    // Live playhead dot at (phase, value).
    if (isLive())
    {
        const auto phase = telemetry->lfoPhase[(size_t) lfo].load (std::memory_order_relaxed);
        const auto v = telemetry->lfoValue[(size_t) lfo].load (std::memory_order_relaxed);
        const auto x = area.getX() + area.getWidth() * juce::jlimit (0.0f, 1.0f, phase);
        const auto y = area.getCentreY() - v * area.getHeight() * 0.42f;
        g.setColour (t.textPrimary);
        g.fillEllipse (x - 3.0f, y - 3.0f, 6.0f, 6.0f);
    }
}

// =========================== FilterDisplay =================================

FilterDisplay::FilterDisplay (SPASynthProcessor& p, int filterIndex)
    : DisplayComponent (p.getAPVTS(),
                        filterIndex == 1
                            ? juce::StringArray { params::id::filter1Type,
                                                  params::id::filter1Cutoff,
                                                  params::id::filter1Resonance }
                            : juce::StringArray { params::id::filter2Type,
                                                  params::id::filter2Cutoff,
                                                  params::id::filter2Resonance,
                                                  params::id::filter2Enable },
                        &p.getTelemetry()),
      index (filterIndex)
{
}

void FilterDisplay::paintDisplay (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto& t = currentTheme();
    const bool second = index == 2;
    const auto enabled = ! second || value (params::id::filter2Enable) >= 0.5f;
    const auto type = (params::FilterType) (int) value (
        second ? params::id::filter2Type : params::id::filter1Type);

    // Live modulated cutoff/res while playing.
    const auto cutoff = isLive() && enabled
        ? (second ? telemetry->filter2CutoffHz : telemetry->filterCutoffHz)
              .load (std::memory_order_relaxed)
        : value (second ? params::id::filter2Cutoff : params::id::filter1Cutoff);
    const auto res = isLive() && enabled
        ? (second ? telemetry->filter2Resonance : telemetry->filterResonance)
              .load (std::memory_order_relaxed)
        : value (second ? params::id::filter2Resonance : params::id::filter1Resonance);
    const auto q = 0.5f + res * 4.5f;

    const bool is24 = type == params::FilterType::lp24 || type == params::FilterType::hp24
                   || type == params::FilterType::bp24 || type == params::FilterType::notch24;

    juce::Path curve;
    constexpr int steps = 140;
    for (int i = 0; i <= steps; ++i)
    {
        const auto freq = 20.0f * std::pow (1000.0f, (float) i / steps);
        const auto w = freq / juce::jmax (20.0f, cutoff);
        const auto w2 = w * w;
        const auto denom = std::sqrt ((1.0f - w2) * (1.0f - w2) + w2 / (q * q));

        float mag;
        switch (type)
        {
            case params::FilterType::lp12: case params::FilterType::lp24:
                mag = 1.0f / denom; break;
            case params::FilterType::hp12: case params::FilterType::hp24:
                mag = w2 / denom; break;
            case params::FilterType::bp12: case params::FilterType::bp24:
                mag = (w / q) / denom; break;
            case params::FilterType::notch12: case params::FilterType::notch24:
            default:
                mag = std::abs (1.0f - w2) / denom; break;
        }

        if (is24)
            mag *= mag;

        const auto dB = juce::jlimit (-36.0f, 18.0f,
                                      juce::Decibels::gainToDecibels (mag, -60.0f));
        const auto x = area.getX() + area.getWidth() * (float) i / steps;
        const auto y = juce::jmap (dB, -36.0f, 18.0f, area.getBottom(), area.getY());
        if (i == 0)
            curve.startNewSubPath (x, y);
        else
            curve.lineTo (x, y);
    }

    draw::glowStroke (g, curve,
                      enabled ? t.accent : t.textSecondary.withAlpha (0.45f), 1.6f);
}

// ============================ ChaosDisplay =================================

ChaosDisplay::ChaosDisplay (SPASynthProcessor& p)
    : DisplayComponent (p.getAPVTS(),
                        { params::id::chaos::depth, params::id::chaos::rate,
                          params::id::chaos::mix, params::id::chaos::enable },
                        &p.getTelemetry())
{
}

void ChaosDisplay::paintDisplay (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto& t = currentTheme();
    const auto enabled = value (params::id::chaos::enable) >= 0.5f;
    const auto depth = value (params::id::chaos::depth);
    const auto mix = value (params::id::chaos::mix);
    const auto rateNorm = apvts.getParameter (params::id::chaos::rate)->getValue();

    // A representative walker trace, deterministic per settings.
    juce::Random rng (1234);
    const auto stepsPerView = 4.0f + rateNorm * 60.0f;
    float walker = 0.0f, target = 0.0f;
    float phase = 1.0f;

    juce::Path curve;
    constexpr int steps = 180;
    for (int i = 0; i <= steps; ++i)
    {
        phase += stepsPerView / steps;
        if (phase >= 1.0f)
        {
            phase -= std::floor (phase);
            target = rng.nextFloat() * 2.0f - 1.0f;
        }
        walker += (target - walker) * juce::jmin (1.0f, stepsPerView / steps * 4.0f);

        const auto amp = enabled ? depth * mix : 0.0f;
        const auto x = area.getX() + area.getWidth() * (float) i / steps;
        const auto y = area.getCentreY() - walker * amp * area.getHeight() * 0.45f;
        if (i == 0)
            curve.startNewSubPath (x, y);
        else
            curve.lineTo (x, y);
    }

    draw::glowStroke (g, curve, enabled ? t.accentMod : t.textSecondary.withAlpha (0.4f), 1.6f);

    // Live chaos output dot on the right edge.
    if (enabled && isLive())
    {
        const auto v = telemetry->chaosValue.load (std::memory_order_relaxed);
        const auto y = area.getCentreY() - v * area.getHeight() * 0.45f;
        g.setColour (t.textPrimary);
        g.fillEllipse (area.getRight() - 7.0f, y - 3.0f, 6.0f, 6.0f);
    }
}

// ============================= FXDisplay ===================================

juce::StringArray FXDisplay::watchedFor (Kind kind)
{
    namespace fx = params::id::fx;
    switch (kind)
    {
        case Kind::distortion: return { fx::distEnable, fx::distType, fx::distDrive, fx::distMix };
        case Kind::chorus:     return { fx::chorusEnable, fx::chorusRate, fx::chorusDepth,
                                        fx::chorusMix };
        case Kind::delay:      return { fx::delayEnable, fx::delayFeedback, fx::delayPingPong,
                                        fx::delayMix };
        case Kind::reverb:     return { fx::reverbEnable, fx::reverbSize, fx::reverbDamping,
                                        fx::reverbMix };
        case Kind::eq:
        {
            juce::StringArray ids { fx::eqEnable, fx::eqCharacter };
            for (int b = 0; b < 8; ++b)
            {
                ids.add (params::id::eqBand (b, fx::eqband::enable));
                ids.add (params::id::eqBand (b, fx::eqband::type));
                ids.add (params::id::eqBand (b, fx::eqband::freq));
                ids.add (params::id::eqBand (b, fx::eqband::gain));
                ids.add (params::id::eqBand (b, fx::eqband::q));
            }
            return ids;
        }
    }
    return {};
}

FXDisplay::FXDisplay (juce::AudioProcessorValueTreeState& state, Kind k)
    : DisplayComponent (state, watchedFor (k)), kind (k)
{
}

void FXDisplay::paintDisplay (juce::Graphics& g, juce::Rectangle<float> area)
{
    namespace fx = params::id::fx;
    const auto& t = currentTheme();

    const auto enabledID = kind == Kind::distortion ? fx::distEnable
                         : kind == Kind::chorus     ? fx::chorusEnable
                         : kind == Kind::delay      ? fx::delayEnable
                         : kind == Kind::reverb     ? fx::reverbEnable : fx::eqEnable;
    const auto colour = value (enabledID) >= 0.5f ? t.accent
                                                  : t.textSecondary.withAlpha (0.45f);

    switch (kind)
    {
        case Kind::distortion:
        {
            // Transfer curve, input -1..1 -> output, with the dry diagonal.
            const auto drive = 1.0f + 15.0f * value (fx::distDrive);
            const auto type = (int) value (fx::distType);
            const auto mix = value (fx::distMix);

            g.setColour (t.outline);
            g.drawLine (area.getX(), area.getBottom(), area.getRight(), area.getY(), 1.0f);

            juce::Path curve;

            if (type == 3)
            {
                // Bit-crush staircase: fewer, wider steps as DRIVE (bit
                // depth reduction) increases, so the display reads as a
                // quantiser rather than a smooth shaper.
                const auto crushDrive = value (fx::distDrive);
                const auto crushLevels = std::pow (2.0f, dsp::FXChain::crushBitsForDrive (crushDrive));
                const auto numSteps = (float) juce::jlimit (3, 64, (int) crushLevels);

                for (int s = 0; s <= (int) numSteps; ++s)
                {
                    const auto in = -1.0f + 2.0f * (float) s / numSteps;
                    const auto quantised = std::round (in * crushLevels) / crushLevels;
                    const auto out = in + (quantised - in) * mix;

                    const auto px = area.getX() + area.getWidth() * (float) s / numSteps;
                    const auto py = area.getCentreY() - out * area.getHeight() * 0.46f;
                    if (s == 0)
                        curve.startNewSubPath (px, py);
                    else
                        curve.lineTo (px, py);

                    if ((float) s < numSteps)
                    {
                        const auto nextPx = area.getX() + area.getWidth() * (float) (s + 1) / numSteps;
                        curve.lineTo (nextPx, py);
                    }
                }
            }
            else
            {
                constexpr int steps = 96;
                for (int i = 0; i <= steps; ++i)
                {
                    const auto in = -1.0f + 2.0f * (float) i / steps;
                    const auto x = in * drive;
                    float wet;
                    switch (type)
                    {
                        case 1:  wet = juce::jlimit (-1.0f, 1.0f, x); break;
                        case 2:  wet = std::sin (x * 1.2f); break;
                        default: wet = std::tanh (x); break;
                    }
                    wet /= std::sqrt (drive);
                    const auto out = in + (wet - in) * mix;

                    const auto px = area.getX() + area.getWidth() * (float) i / steps;
                    const auto py = area.getCentreY() - out * area.getHeight() * 0.46f;
                    if (i == 0)
                        curve.startNewSubPath (px, py);
                    else
                        curve.lineTo (px, py);
                }
            }
            draw::glowStroke (g, curve, colour, 1.6f);
            break;
        }

        case Kind::chorus:
        {
            // Two detuned voices weaving around the dry centre line.
            const auto depth = value (fx::chorusDepth);
            const auto mix = value (fx::chorusMix);

            for (int voice = 0; voice < 2; ++voice)
            {
                juce::Path curve;
                constexpr int steps = 120;
                for (int i = 0; i <= steps; ++i)
                {
                    const auto ph = (float) i / steps * juce::MathConstants<float>::twoPi * 2.0f
                                  + (voice == 0 ? 0.0f : juce::MathConstants<float>::pi * 0.6f);
                    const auto v = std::sin (ph) * depth * (0.25f + 0.75f * mix);
                    const auto x = area.getX() + area.getWidth() * (float) i / steps;
                    const auto y = area.getCentreY() - v * area.getHeight() * 0.42f;
                    if (i == 0)
                        curve.startNewSubPath (x, y);
                    else
                        curve.lineTo (x, y);
                }
                draw::glowStroke (g, curve, voice == 0 ? colour : colour.withAlpha (0.55f), 1.4f);
            }
            break;
        }

        case Kind::delay:
        {
            // Echo taps decaying by feedback; ping-pong alternates sides.
            const auto feedback = value (fx::delayFeedback);
            const auto pingpong = value (fx::delayPingPong) >= 0.5f;
            const auto mix = value (fx::delayMix);

            const auto baseX = area.getX() + 6.0f;
            const auto spacing = (area.getWidth() - 12.0f) / 6.0f;

            // Dry impulse.
            g.setColour (t.textPrimary.withAlpha (0.8f));
            g.fillRect (juce::Rectangle<float> (baseX - 1.5f,
                                                area.getCentreY() - area.getHeight() * 0.45f,
                                                3.0f, area.getHeight() * 0.9f));

            float gain = mix;
            for (int tap = 1; tap <= 6 && gain > 0.02f; ++tap)
            {
                const auto h = area.getHeight() * 0.45f * gain;
                const auto x = baseX + spacing * (float) tap;
                const auto up = ! pingpong || (tap % 2 == 1);
                g.setColour (colour);
                g.fillRect (juce::Rectangle<float> (x - 1.5f,
                                                    up ? area.getCentreY() - h : area.getCentreY(),
                                                    3.0f, h));
                gain *= feedback;
            }

            g.setColour (t.outline.withAlpha (0.7f));
            g.drawHorizontalLine ((int) area.getCentreY(), area.getX(), area.getRight());
            break;
        }

        case Kind::reverb:
        {
            // Decay envelope; size stretches it, damping bows it down.
            const auto size = value (fx::reverbSize);
            const auto damping = value (fx::reverbDamping);
            const auto mix = value (fx::reverbMix);

            juce::Path curve;
            constexpr int steps = 100;
            for (int i = 0; i <= steps; ++i)
            {
                const auto x01 = (float) i / steps;
                const auto decay = 1.2f + (1.0f - size) * 6.0f + damping * 2.0f;
                const auto v = (0.2f + 0.8f * mix) * std::exp (-decay * x01);
                const auto x = area.getX() + area.getWidth() * x01;
                const auto y = area.getBottom() - v * area.getHeight() * 0.92f;
                if (i == 0)
                    curve.startNewSubPath (x, y);
                else
                    curve.lineTo (x, y);
            }

            auto fill = curve;
            fill.lineTo (area.getRight(), area.getBottom());
            fill.lineTo (area.getX(), area.getBottom());
            fill.closeSubPath();
            g.setColour (colour.withAlpha (0.18f));
            g.fillPath (fill);
            draw::glowStroke (g, curve, colour, 1.6f);
            break;
        }

        case Kind::eq:
        {
            // Composite response of the 8 active parametric bands, +/-24 dB,
            // computed by the same function the DSP uses.
            std::array<dsp::ParametricEQ::Band, 8> bands;
            for (int b = 0; b < 8; ++b)
            {
                auto& bd = bands[(size_t) b];
                bd.enabled = value (params::id::eqBand (b, fx::eqband::enable)) >= 0.5f;
                bd.type    = (int) value (params::id::eqBand (b, fx::eqband::type));
                bd.freq    = value (params::id::eqBand (b, fx::eqband::freq));
                bd.gainDb  = value (params::id::eqBand (b, fx::eqband::gain));
                bd.q       = value (params::id::eqBand (b, fx::eqband::q));
            }

            g.setColour (t.outline.withAlpha (0.7f));
            g.drawHorizontalLine ((int) area.getCentreY(), area.getX(), area.getRight());

            juce::Path curve;
            constexpr int steps = 160;
            for (int i = 0; i <= steps; ++i)
            {
                const auto freq = 20.0f * std::pow (1000.0f, (float) i / steps);
                const auto dB = juce::jlimit (-24.0f, 24.0f,
                    dsp::ParametricEQ::magnitudeDb (bands, freq, 48000.0));
                const auto x = area.getX() + area.getWidth() * (float) i / steps;
                const auto y = juce::jmap (dB, -24.0f, 24.0f, area.getBottom(), area.getY());
                if (i == 0)
                    curve.startNewSubPath (x, y);
                else
                    curve.lineTo (x, y);
            }
            draw::glowStroke (g, curve, colour, 1.6f);
            break;
        }
    }
}

// ============================ OutputMeter ==================================

OutputMeter::OutputMeter (const dsp::Telemetry& tel)
    : telemetry (tel)
{
    setInterceptsMouseClicks (false, false);
    startTimerHz (30);
}

void OutputMeter::timerCallback()
{
    const auto attackRelease = [] (float current, float target)
    {
        return target > current ? target : current * 0.82f;
    };
    levelL = attackRelease (levelL, telemetry.peakL.load (std::memory_order_relaxed));
    levelR = attackRelease (levelR, telemetry.peakR.load (std::memory_order_relaxed));
    repaint();
}

void OutputMeter::paint (juce::Graphics& g)
{
    const auto& t = currentTheme();
    auto bounds = getLocalBounds().toFloat();
    const auto barW = (bounds.getWidth() - 2.0f) / 2.0f;

    const auto drawBar = [&] (juce::Rectangle<float> bar, float level)
    {
        // A meter still needs a visible lane, but not the old display-well
        // black -- use meterLane (iteration 3: seam itself darkened enough
        // that it started reading as display-well black again, so the meter
        // now has its own, lighter, token instead of following seam down).
        g.setColour (t.meterLane);
        g.fillRoundedRectangle (bar, 1.5f);

        const auto dB = juce::Decibels::gainToDecibels (level, -60.0f);
        const auto h = juce::jmap (juce::jlimit (-60.0f, 0.0f, dB), -60.0f, 0.0f,
                                   0.0f, bar.getHeight());
        auto fill = bar.removeFromBottom (h);
        g.setColour (dB > -3.0f ? t.accent : t.accentMod);
        g.fillRoundedRectangle (fill, 1.5f);
    };

    drawBar (bounds.removeFromLeft (barW), levelL);
    bounds.removeFromLeft (2.0f);
    drawBar (bounds, levelR);
}

} // namespace spa::ui
