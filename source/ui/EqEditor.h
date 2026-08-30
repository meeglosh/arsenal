#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "Theme.h"
#include "Controls.h"
#include "../dsp/ParametricEQ.h"
#include "../dsp/Telemetry.h"
#include "../params/ParameterRegistry.h"
#include <array>
#include <cmath>

namespace spa::ui
{

// Pro-Q-style interactive EQ: a log-frequency response graph with a live FFT
// spectrum analyzer behind it, 8 draggable band nodes (X = freq, Y = gain,
// wheel = Q), double-click to add/remove a band, plus an on/off toggle and a
// character selector. Nodes write straight to the APVTS band params; the curve
// is drawn by the same magnitude function the DSP uses, so it never lies.
class EqEditor : public juce::Component,
                 public juce::SettableTooltipClient,
                 private juce::Timer
{
public:
    EqEditor (juce::AudioProcessorValueTreeState& state, const dsp::Telemetry& tel,
              std::function<double()> sampleRateFn)
        : apvts (state), telemetry (tel), getSampleRate (std::move (sampleRateFn)),
          onToggle (state, params::id::fx::eqEnable, "EQ"),
          character (state, params::id::fx::eqCharacter)
    {
        refreshSampleRate();
        addAndMakeVisible (onToggle);
        addAndMakeVisible (character);
        setWantsKeyboardFocus (false);
        // setWantsKeyboardFocus alone doesn't stop a click/drag from grabbing
        // focus (JUCE grabs it unconditionally on mouseDown via a separate
        // flag) -- this editor handles node clicks/drags directly, so it
        // needs the actual fix too, not just the controls inside it.
        setMouseClickGrabsKeyboardFocus (false);
        modName = (juce::SystemStats::getOperatingSystemType() & juce::SystemStats::MacOSX)
                      ? "Cmd" : "Ctrl";
        setTooltip ("Double-click to add or remove a band. Drag a node to move it; "
                    + modName + "-drag vertically or use the mouse wheel to set its Q.");
        startTimerHz (30);
    }

    ~EqEditor() override { stopTimer(); }

    void resized() override
    {
        auto r = getLocalBounds();
        auto top = r.removeFromTop (24);
        onToggle.setBounds (top.removeFromLeft (70).reduced (4, 2));
        character.setBounds (top.removeFromRight (130).reduced (4, 2));
        graph = r.reduced (8, 6).toFloat();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = currentTheme();
        const bool on = value (params::id::fx::eqEnable) >= 0.5f;

        // Faceplate restyle: no display-well fill/border behind the graph —
        // the grid + spectrum + curve draw straight on the faceplate surface.
        drawGrid (g, t);
        drawSpectrum (g, t);

        const auto bands = readBands();
        const auto colour = on ? t.accent : t.textSecondary.withAlpha (0.5f);

        // Response curve + fill.
        juce::Path curve;
        constexpr int steps = 220;
        for (int i = 0; i <= steps; ++i)
        {
            const float x = graph.getX() + graph.getWidth() * (float) i / steps;
            const float db = dsp::ParametricEQ::magnitudeDb (bands, xToFreq (x), sampleRate);
            const float y = dbToY (juce::jlimit (-dbRange, dbRange, db));
            if (i == 0) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
        }
        auto fill = curve;
        fill.lineTo (graph.getRight(), dbToY (0.0f));
        fill.lineTo (graph.getX(), dbToY (0.0f));
        fill.closeSubPath();
        g.setColour (colour.withAlpha (0.14f));
        g.fillPath (fill);
        draw::glowStroke (g, curve, colour, 1.8f);

        // Band nodes.
        for (int b = 0; b < numBands; ++b)
        {
            if (! bandEnabled (b)) continue;
            const auto c = nodeCentre (b);
            const bool hot = (b == dragBand || b == hoverBand || b == selectedBand);
            const float rad = hot ? nodeRadius + 2.0f : nodeRadius;
            g.setColour (t.display.withAlpha (0.9f));
            g.fillEllipse (c.x - rad - 1.0f, c.y - rad - 1.0f, (rad + 1.0f) * 2.0f, (rad + 1.0f) * 2.0f);
            g.setColour (on ? t.accent : t.textSecondary);
            g.fillEllipse (c.x - rad, c.y - rad, rad * 2.0f, rad * 2.0f);
            if (b == selectedBand)   // selection ring
            {
                g.setColour (t.textPrimary);
                g.drawEllipse (c.x - rad - 2.0f, c.y - rad - 2.0f,
                               (rad + 2.0f) * 2.0f, (rad + 2.0f) * 2.0f, 1.5f);
            }
            g.setColour (t.background);
            g.setFont (juce::Font (juce::FontOptions (10.0f)));
            g.drawText (juce::String (b + 1),
                        juce::Rectangle<float> (c.x - rad, c.y - rad, rad * 2.0f, rad * 2.0f),
                        juce::Justification::centred);
        }

        // Readout for the selected node (or the one under the pointer): frequency,
        // gain, and Q, so the Q wheel has a visible target.
        const int info = hoverBand >= 0 ? hoverBand : selectedBand;
        if (info >= 0 && bandEnabled (info))
        {
            const float f = rawBand (info, params::id::fx::eqband::freq);
            const float gainDb = rawBand (info, params::id::fx::eqband::gain);
            const float q = rawBand (info, params::id::fx::eqband::q);
            const int type = (int) rawBand (info, params::id::fx::eqband::type);
            const bool gainType = type == 0 || type == 1 || type == 2;
            juce::String txt = "B" + juce::String (info + 1) + "   "
                             + (f >= 1000.0f ? juce::String (f / 1000.0f, 2) + " kHz"
                                             : juce::String (juce::roundToInt (f)) + " Hz");
            if (gainType) txt += "   " + juce::String (gainDb, 1) + " dB";
            txt += "   Q " + juce::String (q, 2);
            g.setColour (t.textSecondary);
            g.setFont (juce::Font (juce::FontOptions (11.0f)));
            g.drawText (txt, graph.reduced (8.0f, 5.0f).removeFromTop (14.0f),
                        juce::Justification::topLeft);
        }

        // Subtle usage hint, top-right (full detail is in the tooltip).
        g.setColour (t.textSecondary.withAlpha (0.45f));
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        g.drawText ("double-click: add / remove    " + modName + "-drag or wheel: Q",
                    graph.reduced (8.0f, 5.0f).removeFromTop (13.0f),
                    juce::Justification::topRight);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        // Click selects and grabs the node under the pointer (no accidental
        // creation); clicking empty space deselects. Nodes are added/removed by
        // double-click.
        const int b = bandAt (e.position);
        selectedBand = b;
        dragBand = b;
        qDragActive = false;
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragBand < 0) return;
        if (e.mods.isCommandDown())   // Cmd/Ctrl-drag = Q (Pro-Q style), vertical
        {
            if (! qDragActive)        // anchor when the Q gesture begins
            {
                qDragActive = true;
                qRefY = e.position.y;
                qRefQ = rawBand (dragBand, params::id::fx::eqband::q);
            }
            const float dy = qRefY - e.position.y;   // up = narrower (higher Q)
            setBandRaw (dragBand, params::id::fx::eqband::q,
                        juce::jlimit (0.1f, 18.0f, qRefQ * std::exp (dy * 0.012f)));
        }
        else
        {
            qDragActive = false;
            applyDrag (e.position);   // freq (x) + gain (y)
        }
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        dragBand = -1;
        qDragActive = false;
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        // Double-click a node to remove it, or empty graph space to add one.
        const int b = bandAt (e.position);
        if (b >= 0)
        {
            setBand (b, params::id::fx::eqband::enable, 0.0f);
            if (selectedBand == b) selectedBand = -1;
        }
        else
        {
            const int n = createBandAt (e.position);
            if (n >= 0) selectedBand = n;
        }
        repaint();
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int b = bandAt (e.position);
        if (b != hoverBand) { hoverBand = b; repaint(); }
        setMouseCursor (b >= 0 ? juce::MouseCursor::DraggingHandCursor
                               : juce::MouseCursor::NormalCursor);
    }

    void mouseExit (const juce::MouseEvent&) override { hoverBand = -1; repaint(); }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        // Wheel sets Q: the hovered node if the pointer is over one, otherwise
        // the currently selected node (so you can dial Q after picking a node).
        int b = bandAt (e.position);
        if (b < 0) b = selectedBand;
        if (b < 0 || ! bandEnabled (b)) return;
        const float q = rawBand (b, params::id::fx::eqband::q);
        setBandRaw (b, params::id::fx::eqband::q,
                    juce::jlimit (0.1f, 18.0f, q * (1.0f + w.deltaY * 0.6f)));
        repaint();
    }

    void timerCallback() override
    {
        refreshSampleRate();
        if (! isShowing()) return;
        computeSpectrum();
        repaint();
    }

private:
    static constexpr int numBands = dsp::ParametricEQ::numBands;
    static constexpr float minF = 20.0f, maxF = 20000.0f, dbRange = 24.0f, nodeRadius = 6.0f;

    float value (const juce::String& id) const
    {
        if (auto* v = apvts.getRawParameterValue (id)) return v->load();
        return 0.0f;
    }
    float rawBand (int b, const char* key) const
    {
        return value (params::id::eqBand (b, key));
    }
    bool bandEnabled (int b) const { return rawBand (b, params::id::fx::eqband::enable) >= 0.5f; }

    void setBandRaw (int b, const char* key, float realValue)
    {
        const auto id = params::id::eqBand (b, key);
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (realValue));
    }
    void setBand (int b, const char* key, float realValue) { setBandRaw (b, key, realValue); }

    std::array<dsp::ParametricEQ::Band, numBands> readBands() const
    {
        std::array<dsp::ParametricEQ::Band, numBands> bands {};
        for (int b = 0; b < numBands; ++b)
        {
            auto& bd = bands[(size_t) b];
            bd.enabled = bandEnabled (b);
            bd.type    = (int) rawBand (b, params::id::fx::eqband::type);
            bd.freq    = rawBand (b, params::id::fx::eqband::freq);
            bd.gainDb  = rawBand (b, params::id::fx::eqband::gain);
            bd.q       = rawBand (b, params::id::fx::eqband::q);
        }
        return bands;
    }

    float freqToX (float f) const
    {
        return graph.getX() + graph.getWidth()
             * std::log (f / minF) / std::log (maxF / minF);
    }
    float xToFreq (float x) const
    {
        return minF * std::pow (maxF / minF,
                                (x - graph.getX()) / juce::jmax (1.0f, graph.getWidth()));
    }
    float dbToY (float db) const
    {
        return graph.getY() + graph.getHeight() * (0.5f - db / (2.0f * dbRange));
    }
    float yToDb (float y) const
    {
        return (0.5f - (y - graph.getY()) / juce::jmax (1.0f, graph.getHeight())) * 2.0f * dbRange;
    }

    juce::Point<float> nodeCentre (int b) const
    {
        const int type = (int) rawBand (b, params::id::fx::eqband::type);
        const bool gainType = type == 0 || type == 1 || type == 2;   // bell/shelves
        const float gain = gainType ? rawBand (b, params::id::fx::eqband::gain) : 0.0f;
        return { freqToX (rawBand (b, params::id::fx::eqband::freq)),
                 dbToY (juce::jlimit (-dbRange, dbRange, gain)) };
    }

    int bandAt (juce::Point<float> p) const
    {
        for (int b = 0; b < numBands; ++b)
        {
            if (! bandEnabled (b)) continue;
            if (nodeCentre (b).getDistanceFrom (p) <= nodeRadius + 4.0f) return b;
        }
        return -1;
    }

    void applyDrag (juce::Point<float> p)
    {
        setBandRaw (dragBand, params::id::fx::eqband::freq,
                    juce::jlimit (minF, maxF, xToFreq (p.x)));
        const int type = (int) rawBand (dragBand, params::id::fx::eqband::type);
        if (type == 0 || type == 1 || type == 2)   // bell/shelves have gain
            setBandRaw (dragBand, params::id::fx::eqband::gain,
                        juce::jlimit (-dbRange, dbRange, yToDb (p.y)));
    }

    // Enable the first free band at the click point (Bell), or -1 if all 8 are
    // in use or the click is outside the graph.
    int createBandAt (juce::Point<float> p)
    {
        if (! graph.contains (p)) return -1;
        for (int i = 0; i < numBands; ++i)
            if (! bandEnabled (i))
            {
                setBand (i, params::id::fx::eqband::type, 0.0f /* Bell */);
                setBandRaw (i, params::id::fx::eqband::freq, juce::jlimit (minF, maxF, xToFreq (p.x)));
                setBandRaw (i, params::id::fx::eqband::gain,
                            juce::jlimit (-dbRange, dbRange, yToDb (p.y)));
                setBand (i, params::id::fx::eqband::enable, 1.0f);
                return i;
            }
        return -1;
    }

    void drawGrid (juce::Graphics& g, const Theme& t) const
    {
        g.setColour (t.outline.withAlpha (0.6f));
        for (float f : { 100.0f, 1000.0f, 10000.0f })
        {
            const float x = freqToX (f);
            g.drawVerticalLine ((int) x, graph.getY(), graph.getBottom());
        }
        for (float db : { 12.0f, 0.0f, -12.0f })
        {
            const float y = dbToY (db);
            g.setColour (t.outline.withAlpha (db == 0.0f ? 0.8f : 0.4f));
            g.drawHorizontalLine ((int) y, graph.getX(), graph.getRight());
        }
        g.setColour (t.textSecondary.withAlpha (0.6f));
        g.setFont (juce::Font (juce::FontOptions (9.0f)));
        g.drawText ("100", juce::Rectangle<float> (freqToX (100.0f) - 14.0f, graph.getBottom() - 11.0f, 28.0f, 10.0f), juce::Justification::centred);
        g.drawText ("1k",  juce::Rectangle<float> (freqToX (1000.0f) - 14.0f, graph.getBottom() - 11.0f, 28.0f, 10.0f), juce::Justification::centred);
        g.drawText ("10k", juce::Rectangle<float> (freqToX (10000.0f) - 14.0f, graph.getBottom() - 11.0f, 28.0f, 10.0f), juce::Justification::centred);
    }

    void drawSpectrum (juce::Graphics& g, const Theme& t) const
    {
        juce::Path p;
        bool started = false;
        const int bins = dsp::Telemetry::scopeSize / 2;
        for (int i = 1; i < bins; ++i)
        {
            const float freq = (float) i * (float) sampleRate / (float) dsp::Telemetry::scopeSize;
            if (freq < minF || freq > maxF) continue;
            const float x = freqToX (freq);
            // -80..0 dB mapped into the graph height.
            const float db = juce::jlimit (-80.0f, 0.0f, spectrum[(size_t) i]);
            const float y = graph.getBottom() - (db + 80.0f) / 80.0f * graph.getHeight();
            if (! started) { p.startNewSubPath (x, graph.getBottom()); p.lineTo (x, y); started = true; }
            else p.lineTo (x, y);
        }
        if (started)
        {
            p.lineTo (graph.getRight(), graph.getBottom());
            p.closeSubPath();
            g.setColour (t.accentMod.withAlpha (0.16f));
            g.fillPath (p);
        }
    }

    void computeSpectrum()
    {
        const int size = dsp::Telemetry::scopeSize;
        const int w = telemetry.scopeWrite.load (std::memory_order_acquire);
        for (int i = 0; i < size; ++i)
        {
            const int idx = (w + i) & (size - 1);   // oldest -> newest
            const float win = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi
                                                      * (float) i / (float) (size - 1));
            fftData[(size_t) i] = telemetry.scope[(size_t) idx].load (std::memory_order_relaxed) * win;
        }
        std::fill (fftData.begin() + size, fftData.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (fftData.data());

        const int bins = size / 2;
        const float norm = 2.0f / (float) size;
        for (int i = 0; i < bins; ++i)
        {
            const float mag = fftData[(size_t) i] * norm;
            const float db = juce::Decibels::gainToDecibels (mag + 1.0e-9f);
            // Temporal smoothing; fast attack, slower release for readability.
            float& s = spectrum[(size_t) i];
            s = db > s ? db : s * 0.85f + db * 0.15f;
        }
    }

    void refreshSampleRate()
    {
        const double sr = getSampleRate ? getSampleRate() : 0.0;
        if (sr > 0.0) sampleRate = sr;
    }

    juce::AudioProcessorValueTreeState& apvts;
    const dsp::Telemetry& telemetry;
    std::function<double()> getSampleRate;
    double sampleRate = 48000.0;

    Toggle onToggle;
    Choice character;

    juce::Rectangle<float> graph;
    int dragBand = -1, hoverBand = -1, selectedBand = -1;
    bool qDragActive = false;      // Cmd/Ctrl-drag Q gesture in progress
    float qRefY = 0.0f, qRefQ = 1.0f;
    juce::String modName { "Ctrl" };

    juce::dsp::FFT fft { 11 };   // 2^11 = 2048 = scopeSize
    std::array<float, dsp::Telemetry::scopeSize * 2> fftData {};
    std::array<float, dsp::Telemetry::scopeSize / 2> spectrum {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EqEditor)
};

} // namespace spa::ui
