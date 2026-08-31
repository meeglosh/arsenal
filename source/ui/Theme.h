#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace spa::ui
{

// Runtime design tokens. Every colour, font and metric the UI uses comes
// through here so the whole look can be restyled without touching component
// code.
//
// Direction (faceplate restyle): one continuous charcoal-graphite surface —
// no per-module cards, no LED-screen display wells. Modules are separated by
// dark recessed seams and soft horizontal shadow bands painted directly on
// the surface (see ContentComponent::paint), not by their own panel fills.
// `panel` intentionally equals `background` so anything that still fills
// with it (transient overlays: popups, the preset drawer, call-outs) reads
// as the same surface rather than a card. Flat knobs, hairline rules and
// both teal accents (#51D0BF, user-tintable) are unchanged from the prior
// direction.
struct Theme
{
    juce::Colour background;      // window / continuous faceplate surface
    juce::Colour panel;           // transient overlay fills (popups, drawer) — == background
    juce::Colour display;         // recessed fields (combo/chip backgrounds); NOT used by scopes/wells
    juce::Colour header;          // top bar / footer rail
    juce::Colour seam;            // recessed vertical grooves between modules, tab hover fill,
                                   // call-out edges -- NOT the OutputMeter lane, see meterLane
    juce::Colour meterLane;       // OutputMeter's unlit bar lane -- deliberately lighter than
                                   // seam (iteration 3: seam darkened into near-display-well
                                   // territory, which read as a dead/black meter; split off so
                                   // the groove could go darker without dragging the meter down)
    juce::Colour textPrimary;
    juce::Colour textSecondary;
    juce::Colour accent;          // audio signal, primary actions
    juce::Colour accentMod;       // modulation (LFO/env/matrix/chaos)
    juce::Colour outline;         // hairlines
    juce::Colour knobFace;
    juce::Colour knobTrack;

    static Theme dark()
    {
        // Flat charcoal-graphite faceplate register. (Iteration 2, 2026-08-30:
        // darkened toward Mike's design spec — background sampled directly off
        // the spec mock, the rest offset-preserved from the old background so
        // every control keeps its original relative contrast.)
        Theme t;
        t.background    = juce::Colour (0xff181d20);
        t.panel         = t.background;
        t.display       = juce::Colour (0xff0d1318);
        t.header        = juce::Colour (0xff13171a);
        t.seam          = t.background.darker (0.9f);
        t.meterLane     = t.background.darker (0.45f);   // the old seam tone, kept for the meter
        t.textPrimary   = juce::Colour (0xffe7ecef);
        t.textSecondary = juce::Colour (0xff8b989f);
        t.accent        = juce::Colour (0xff51d0bf);
        t.accentMod     = juce::Colour (0xff51d0bf);
        t.outline       = juce::Colour (0xff2a3337);
        t.knobFace      = juce::Colour (0xff232b31);
        t.knobTrack     = juce::Colour (0xff2f393e);
        return t;
    }

};

// Process-wide active theme (shared by all editor instances). Components
// read colours at paint time, so a change only requires a repaint +
// LookAndFeel palette refresh.
const Theme& currentTheme();

// User accent overrides (persisted machine preference): the audio accent
// (orange by default) and the modulation accent (cyan by default).
void setAccentColors (juce::Colour audio, juce::Colour mod);
void resetAccentColors();

namespace metrics
{
    inline constexpr int baseWidth = 1380;
    inline constexpr int baseHeight = 900;
    inline constexpr int brandBandHeight = 34;   // centred wordmark strip
    inline constexpr int headerHeight = 54;
    inline constexpr int lockRowHeight = 26;
    inline constexpr int footerHeight = 24;
    inline constexpr int keyboardStripHeight = 96;   // on-screen keyboard when shown
    inline constexpr int unit = 8;
    inline constexpr float cornerRadius = 7.0f;  // softer, elevated panels

    // Section-title row (draw::sectionHeader) reserved from the top of every
    // module panel's bounds. Shared so any site that needs to know where the
    // header ends and content begins -- OscStrip's headerNameRect click/popup
    // hit-test chief among them (iteration 3 restyle bug: it used to hardcode
    // its own copy of this, and every resized() below independently re-trimmed
    // it too) -- can't drift out of sync with what sectionHeader() actually
    // paints. Grew from 20 (iteration 2) for more air around the title, per
    // Mike's spec mock.
    inline constexpr int sectionHeaderHeight = 32;
    inline constexpr int sectionHeaderTopInset = 6;    // air above the title text
    inline constexpr int sectionHeaderLeftInset = 12;  // air to the left of the title text

    inline juce::Font titleFont()   { return juce::Font (juce::FontOptions (17.0f, juce::Font::bold)); }
    inline juce::Font sectionFont()
    {
        return juce::Font (juce::FontOptions (12.0f, juce::Font::bold))
                   .withExtraKerningFactor (0.06f);
    }
    inline juce::Font labelFont()   { return juce::Font (juce::FontOptions (11.0f)); }
    inline juce::Font smallFont()
    {
        return juce::Font (juce::FontOptions (9.5f)).withExtraKerningFactor (0.05f);
    }
    inline juce::Font wordmarkFont()
    {
        // The big tracked wordmark: A R S E N A L
        return juce::Font (juce::FontOptions (21.0f, juce::Font::plain))
                   .withExtraKerningFactor (0.42f);
    }
    inline juce::Font brandSubFont()
    {
        return juce::Font (juce::FontOptions (8.5f)).withExtraKerningFactor (0.30f);
    }
}

// Shared painting helpers so every module reads as one system.
namespace draw
{
    // Flat module panel: fill + hairline.
    void panel (juce::Graphics&, juce::Rectangle<float>);

    // MiniFreak-style section header: SMALL CAPS title, thin rule to the
    // right, optional right-aligned readout. Returns the content area below.
    juce::Rectangle<int> sectionHeader (juce::Graphics&, juce::Rectangle<int> bounds,
                                        const juce::String& title,
                                        const juce::String& readout = {},
                                        juce::Colour titleColour = {});

    // Display well behind scopes/curves. centreLine draws a faint
    // zero/centre reference line (useful for bipolar scopes, pointless for
    // plain list/text containers that happen to reuse this helper).
    void displayWell (juce::Graphics&, juce::Rectangle<float>, bool centreLine = true);

    // Curve stroke with a soft under-glow, the reference look for scopes.
    void glowStroke (juce::Graphics&, const juce::Path&, juce::Colour, float thickness = 1.8f);

    // Eased cast-shadow gradient shared by every recessed edge on the
    // faceplate (ContentComponent::paint's row-overhang shadows,
    // SPASynthLookAndFeel::drawTabAreaBehindFrontButton's tab-strip recess):
    // darkest (alpha = startAlpha) at `from`, fading through three eased
    // stops -- steep near the edge, long soft tail -- to fully transparent
    // at `to`, rather than a flat linear ramp, so the band never reads as a
    // visible strip. Pass `from`/`to` flipped to fade the shadow in the
    // other direction (e.g. upward into a recess instead of downward off an
    // overhang). Both call sites must use the same startAlpha so the two
    // shadow languages can never drift apart again.
    juce::ColourGradient easedShadowGradient (juce::Point<float> from, juce::Point<float> to,
                                              float startAlpha);
}

} // namespace spa::ui
