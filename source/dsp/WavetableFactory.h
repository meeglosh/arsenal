#pragma once

#include "Wavetable.h"

namespace spa::dsp
{

// Built-in wavetable "Table" choices for the wavetable oscillator engine.
// Choice 0 (Basic Shapes) is Wavetable::createBasicShapes() itself; every
// other choice is a generated table with enough frames (32-64) that the
// existing POSITION knob morphs through real, audibly distinct character
// rather than just a couple of fixed shapes.
//
// All generation happens on the message thread (or a background thread, at
// construction/parameter-change time) and produces immutable Wavetable
// objects handed to the processor as shared_ptr<const Wavetable> -- the same
// swap-a-pointer, never-mutate-on-the-audio-thread scheme user-loaded
// wavetable files already use (see SPASynthProcessor::installTable).
enum class WavetableTableChoice
{
    basicShapes = 0,
    supersaw,
    pwm,
    formant,
    additive,
    unisonSpread,
    syncSweep,
    bells,
    count,
};

inline constexpr int numWavetableTableChoices = (int) WavetableTableChoice::count;

// Choice names, in enum order -- APPEND-ONLY (rides the osc::table choice
// parameter, which is serialized in presets/sessions).
const juce::StringArray& wavetableTableChoiceNames();

class WavetableFactory
{
public:
    // Builds the table for a given choice index (clamped into range).
    static Wavetable build (int tableChoice);
};

} // namespace spa::dsp
