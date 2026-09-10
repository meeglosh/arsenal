#pragma once

#include "Theme.h"
#include "../params/ParameterRegistry.h"
#include "../dsp/Telemetry.h"

namespace spa
{
class SPASynthProcessor;

namespace ui
{

// Base for the module scopes: watches a set of parameters and repaints (at a
// throttled rate) when any of them move. When given a telemetry pointer it
// also animates continuously while voices are sounding, so the scopes show
// the *modulated* state, not just knob positions.
class DisplayComponent : public juce::Component,
                         private juce::Timer,
                         private juce::AudioProcessorValueTreeState::Listener
{
public:
    DisplayComponent (juce::AudioProcessorValueTreeState&, juce::StringArray paramIDs,
                      const dsp::Telemetry* telemetry = nullptr);
    ~DisplayComponent() override;

    void paint (juce::Graphics&) final;
    void markDirty() { dirty.store (true); }

protected:
    virtual void paintDisplay (juce::Graphics&, juce::Rectangle<float>) = 0;

    bool isLive() const;   // voices currently sounding

    juce::AudioProcessorValueTreeState& apvts;
    const dsp::Telemetry* telemetry = nullptr;

    float value (const juce::String& paramID) const;   // real-world value

private:
    void parameterChanged (const juce::String&, float) override { dirty.store (true); }
    void timerCallback() override;

    juce::StringArray watched;
    std::atomic<bool> dirty { true };
};

// Oscillator scope: wavetable frame at the (live, modulated) position, or the
// loaded sample's waveform with a moving playhead.
class WaveDisplay : public DisplayComponent,
                    public juce::SettableTooltipClient,
                    private juce::ChangeListener
{
public:
    WaveDisplay (SPASynthProcessor&, int slot);
    ~WaveDisplay() override;

    // Zoom/pan view state for the sample/granular waveform (normalized 0..1
    // fractions of the file, same domain as sampleStart/loopStart/loopEnd).
    // UI-only -- never serialized, never touches DSP; resets on a new
    // content load. Exposed for tests (waveDisplayZoomTest).
    float getViewStart() const  { return viewStart; }
    float getViewLength() const { return viewLength; }
    static constexpr float minViewLength = 1.0f / 64.0f;

    // Maps a normalized (0..1) file position to/from an x coordinate within
    // `area` (the same reduced bounds paintDisplay draws into -- see
    // waveArea()), honoring the current zoom/pan. Shared by paint and the
    // gesture handlers, and by tests, so overlays can never drift from what
    // was actually drawn.
    float normToX (float norm, juce::Rectangle<float> area) const;
    float xToNorm (float x, juce::Rectangle<float> area) const;
    juce::Rectangle<float> waveArea() const;

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scaleFactor) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    void paintDisplay (juce::Graphics&, juce::Rectangle<float>) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void zoomAt (float normCursor, float factor);

    SPASynthProcessor& processor;
    const int slot;

    float viewStart = 0.0f;    // 0..1, left edge of the visible window
    float viewLength = 1.0f;   // minViewLength..1, width of the visible window
    float dragStartViewStart = 0.0f;
    float dragAnchorX = 0.0f;
    const void* lastSample = nullptr;   // identity check -> reset view on a new load
};

// ADSR curve with translucent fill and a live output-level bar.
class EnvDisplay : public DisplayComponent
{
public:
    EnvDisplay (SPASynthProcessor&, juce::String idPrefix, int envIndex);

private:
    void paintDisplay (juce::Graphics&, juce::Rectangle<float>) override;
    juce::String prefix;
    const int env;
};

// One cycle of the LFO shape with a live playhead dot.
class LFODisplay : public DisplayComponent
{
public:
    LFODisplay (SPASynthProcessor&, int lfoIndex);

private:
    void paintDisplay (juce::Graphics&, juce::Rectangle<float>) override;
    const int lfo;
};

// Approximate filter magnitude response; follows the modulated cutoff live.
class FilterDisplay : public DisplayComponent
{
public:
    FilterDisplay (SPASynthProcessor&, int filterIndex);

private:
    void paintDisplay (juce::Graphics&, juce::Rectangle<float>) override;
    const int index;
};

// A representative chaos walk with a live output dot.
class ChaosDisplay : public DisplayComponent
{
public:
    explicit ChaosDisplay (SPASynthProcessor&);

private:
    void paintDisplay (juce::Graphics&, juce::Rectangle<float>) override;

    // Preallocated snapshot of the telemetry trace ring, read once per paint.
    std::array<float, dsp::Telemetry::chaosTraceSize> traceSnapshot {};
};

// FX scopes: one class, five characters.
class FXDisplay : public DisplayComponent
{
public:
    enum class Kind { distortion, chorus, delay, reverb, eq };

    FXDisplay (juce::AudioProcessorValueTreeState&, Kind);

private:
    void paintDisplay (juce::Graphics&, juce::Rectangle<float>) override;
    static juce::StringArray watchedFor (Kind);

    const Kind kind;
};

// Stereo output peak meter for the header, fed by telemetry.
class OutputMeter : public juce::Component,
                    private juce::Timer
{
public:
    explicit OutputMeter (const dsp::Telemetry&);

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    const dsp::Telemetry& telemetry;
    float levelL = 0.0f, levelR = 0.0f;
};

} // namespace ui
} // namespace spa
