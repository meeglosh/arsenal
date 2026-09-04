#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>

namespace spa
{

// MIDI Learn: binds hardware CCs to plugin parameters.
//
// Threading: the audio thread calls processMidi() every block (lock-free —
// the CC map is an array of atomics); everything else is message-thread.
// Broadcasts a change whenever an assignment is made, cleared, or restored,
// and when a pending learn captures its CC.
//
// processMidi() itself never calls juce::AudioProcessorParameter::
// setValueNotifyingHost() — that method is message-thread-only (it takes a
// CriticalSection lock and calls straight into host/listener code, which may
// allocate, lock, or re-enter). Instead the audio thread stashes the latest
// normalized value per CC in a lock-free array (pendingCcValue), and a Timer
// (message thread) drains it, coalesced to the latest value per CC, and
// replays it as a real setValueNotifyingHost() call — the same call a mouse
// drag on the control would make. See processMidi()'s definition for detail.
class MidiLearnManager : public juce::ChangeBroadcaster,
                          private juce::Timer
{
public:
    explicit MidiLearnManager (juce::AudioProcessorValueTreeState&);

    // --- Message thread ------------------------------------------------------
    void armLearn (const juce::String& paramID);
    void cancelLearn();
    bool isArmed() const { return armedParamIndex.load() >= 0; }
    juce::String getArmedParamID() const;

    int getAssignedCC (const juce::String& paramID) const;   // -1 = none
    void clearAssignment (const juce::String& paramID);
    void clearAll();

    juce::ValueTree toValueTree() const;                     // type "MIDIMAP"
    void restoreFromValueTree (const juce::ValueTree&);

    static constexpr const char* mapTreeType = "MIDIMAP";

    // --- Audio thread --------------------------------------------------------
    // Captures a pending learn and stashes mapped CCs' values for the Timer
    // to apply on the message thread (see the class comment above).
    void processMidi (const juce::MidiBuffer&);

private:
    int indexOfParam (const juce::String& paramID) const;

    // --- Message thread: replays stashed CC values as real parameter
    // changes (setValueNotifyingHost), coalesced to one call per CC per tick.
    void timerCallback() override;

    juce::AudioProcessorValueTreeState& apvts;
    std::vector<juce::RangedAudioParameter*> parametersByIndex;

    std::array<std::atomic<int>, 128> ccToParam;   // param index or -1
    std::atomic<int> armedParamIndex { -1 };

    // Audio-thread -> message-thread handoff for mapped CC values. -1.0f =
    // no pending update for that CC; otherwise the latest normalized (0..1)
    // value seen since the Timer last drained it. Plain atomic stores/
    // exchanges only — no lock, no allocation, safe from the audio thread.
    std::array<std::atomic<float>, 128> pendingCcValue;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiLearnManager)
};

} // namespace spa
