#pragma once

#include "Controls.h"
#include "Displays.h"
#include "SectionPanel.h"

namespace spa
{
class SPASynthProcessor;

namespace ui
{

// One oscillator column: scope on top, curated mode-aware controls below —
// the panel reshapes itself for Wavetable / Sample / Granular like the
// reference synths do.
class OscStrip : public juce::Component,
                 private juce::AudioProcessorValueTreeState::Listener,
                 private juce::AsyncUpdater,
                 private juce::ChangeListener
{
public:
    OscStrip (SPASynthProcessor&, int slot);
    ~OscStrip() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void parameterChanged (const juce::String&, float) override { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override { triggerAsyncUpdate(); }
    void chooseContent();
    params::OscMode currentMode() const;
    juce::String contentName() const;

    // In-pack quick-swap affordance in the header (Sample/Granular modes):
    // the sample name plus a caret opens a dropdown of the whole pack. Shown
    // only when the loaded sample is in a pack.
    juce::Rectangle<int> headerNameRect() const;
    bool sampleSwapAvailable() const;
    void paintSampleSwapper (juce::Graphics&);
    void openSampleMenu();

    SPASynthProcessor& processor;
    const int slot;

    WaveDisplay display;
    Toggle enable;
    Choice mode;
    juce::TextButton loadButton { "LOAD" };
    juce::TextButton factoryButton { "INIT" };

    std::vector<std::unique_ptr<Knob>> commonKnobs;      // coarse/fine/level/pan
    std::vector<std::unique_ptr<Knob>> wavetableKnobs;
    std::vector<std::unique_ptr<Knob>> sampleKnobs;
    std::vector<std::unique_ptr<Knob>> granularKnobs;
    std::vector<std::unique_ptr<Knob>> analogKnobs, fmKnobs, pluckKnobs;
    std::unique_ptr<Choice> phaseMode, analogShape, noiseColor;
    std::unique_ptr<Toggle> loop, keytrackSample, keytrackGranular;

    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OscStrip)
};

// Filter: response curve + type + cutoff/res/drive.
class FilterPanel : public juce::Component
{
public:
    FilterPanel (SPASynthProcessor&, int filterIndex);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    const int index;
    FilterDisplay display;
    Choice type;
    Knob cutoff, resonance, drive;
    Knob keytrack, envAmount, mix;
    std::unique_ptr<Toggle> enable;
    std::unique_ptr<Choice> routing;     // filter 2 only
};

// One ADSR page: curve + four knobs.
class EnvPanel : public juce::Component
{
public:
    EnvPanel (SPASynthProcessor&, const juce::String& idPrefix, int envIndex);
    void resized() override;

private:
    EnvDisplay display;
    Knob attack, decay, sustain, release;
};

// One LFO page: shape scope + controls.
class LFOPanel : public juce::Component
{
public:
    LFOPanel (SPASynthProcessor&, int lfoIndex);
    void resized() override;

private:
    LFODisplay display;
    Choice shape, division;
    Knob rate, phase;
    Toggle sync, retrig, unipolar;
};

// Organic Chaos: walker scope + master knobs + per-target drift strip.
class ChaosPanel : public juce::Component
{
public:
    explicit ChaosPanel (SPASynthProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    ChaosDisplay display;
    Toggle enable;
    Knob depth, rate, mix;

    struct Drift
    {
        std::unique_ptr<Toggle> on;
        std::unique_ptr<Knob> amount;
    };
    std::array<Drift, 6> drifts;   // pitch, phase, position, amp, sat, dist
};

// Arpeggiator: compact four-row layout (toggles+mode+rate / phrase+vel /
// octave-gate-swing knobs / chance-stutter-jump-humanize knobs).
class ArpPanel : public juce::Component
{
public:
    explicit ArpPanel (juce::AudioProcessorValueTreeState&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    Toggle enable, latch;
    Choice mode, division, phrase, velMode;
    Knob octaves, gate, swing;
    Knob chance, stutter, jump, humanize;
};

// One FX tab: character scope on top, the section's registry controls below.
class FXPanel : public juce::Component
{
public:
    FXPanel (juce::AudioProcessorValueTreeState&, FXDisplay::Kind,
             params::Section, const juce::String& title);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::String panelTitle;
    FXDisplay display;
    SectionPanel controls;   // bare: frame + header drawn by this panel
};

} // namespace ui
} // namespace spa
