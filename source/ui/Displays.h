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
                    private juce::ChangeListener
{
public:
    WaveDisplay (SPASynthProcessor&, int slot);
    ~WaveDisplay() override;

private:
    void paintDisplay (juce::Graphics&, juce::Rectangle<float>) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    SPASynthProcessor& processor;
    const int slot;
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
