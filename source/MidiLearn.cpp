#include "MidiLearn.h"

namespace spa
{

MidiLearnManager::MidiLearnManager (juce::AudioProcessorValueTreeState& state)
    : apvts (state)
{
    for (auto& cc : ccToParam)
        cc.store (-1);

    for (auto& pending : pendingCcValue)
        pending.store (-1.0f);

    // Stable index order for the atomics: the processor's parameter list.
    for (auto* param : apvts.processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
            parametersByIndex.push_back (ranged);

    // Fast enough to feel like direct control (a mouse drag on a Knob is no
    // more "sample-accurate" than this), never touches the audio thread.
    startTimerHz (60);
}

int MidiLearnManager::indexOfParam (const juce::String& paramID) const
{
    for (size_t i = 0; i < parametersByIndex.size(); ++i)
        if (parametersByIndex[i]->paramID == paramID)
            return (int) i;

    return -1;
}

void MidiLearnManager::armLearn (const juce::String& paramID)
{
    armedParamIndex.store (indexOfParam (paramID));
    sendChangeMessage();
}

void MidiLearnManager::cancelLearn()
{
    armedParamIndex.store (-1);
    sendChangeMessage();
}

juce::String MidiLearnManager::getArmedParamID() const
{
    const auto index = armedParamIndex.load();
    return index >= 0 ? parametersByIndex[(size_t) index]->paramID : juce::String();
}

int MidiLearnManager::getAssignedCC (const juce::String& paramID) const
{
    const auto index = indexOfParam (paramID);
    for (int cc = 0; cc < 128; ++cc)
        if (ccToParam[(size_t) cc].load() == index && index >= 0)
            return cc;

    return -1;
}

void MidiLearnManager::clearAssignment (const juce::String& paramID)
{
    const auto index = indexOfParam (paramID);
    for (auto& cc : ccToParam)
        if (cc.load() == index)
            cc.store (-1);

    sendChangeMessage();
}

void MidiLearnManager::clearAll()
{
    for (auto& cc : ccToParam)
        cc.store (-1);

    sendChangeMessage();
}

juce::ValueTree MidiLearnManager::toValueTree() const
{
    juce::ValueTree tree (mapTreeType);

    for (int cc = 0; cc < 128; ++cc)
    {
        const auto index = ccToParam[(size_t) cc].load();
        if (index < 0)
            continue;

        juce::ValueTree map ("MAP");
        map.setProperty ("cc", cc, nullptr);
        map.setProperty ("param", parametersByIndex[(size_t) index]->paramID, nullptr);
        tree.appendChild (map, nullptr);
    }

    return tree;
}

void MidiLearnManager::restoreFromValueTree (const juce::ValueTree& tree)
{
    if (! tree.hasType (mapTreeType))
        return;

    for (auto& cc : ccToParam)
        cc.store (-1);

    for (const auto& map : tree)
    {
        const auto cc = (int) map.getProperty ("cc", -1);
        const auto index = indexOfParam (map.getProperty ("param").toString());
        if (cc >= 0 && cc < 128 && index >= 0)
            ccToParam[(size_t) cc].store (index);
    }

    sendChangeMessage();
}

void MidiLearnManager::processMidi (const juce::MidiBuffer& midi)
{
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        if (! message.isController())
            continue;

        const auto cc = message.getControllerNumber();

        // Pending learn captures the first CC it hears.
        const auto armed = armedParamIndex.load();
        if (armed >= 0)
        {
            // One CC per parameter: release any previous binding.
            for (auto& entry : ccToParam)
                if (entry.load() == armed)
                    entry.store (-1);

            ccToParam[(size_t) cc].store (armed);
            armedParamIndex.store (-1);
            sendChangeMessage();   // async post — safe from the audio thread
        }

        const auto target = ccToParam[(size_t) cc].load();
        if (target >= 0)
        {
            // NOT param->setValueNotifyingHost() here — see the class-level
            // comment in MidiLearn.h. setValueNotifyingHost() is documented
            // (juce_AudioProcessorParameter.h) as something the HOST calls on
            // us, on any thread including the audio thread, and our
            // implementation of setValue() must handle that "very
            // efficiently and avoid any kind of locking" — but the reverse
            // direction (plugin -> host, i.e. this call) is a different
            // story: its implementation (juce_AudioProcessorParameter.cpp)
            // takes a CriticalSection (ScopedLock) and then calls every
            // registered AudioProcessorParameter::Listener, including the
            // host wrapper's listener, synchronously and unconditionally.
            // A host is free to do anything in that callback (allocate,
            // take its own locks, marshal to another thread) since it does
            // not expect to be called from a realtime thread. Also, for the
            // plain juce::AudioParameterFloat/Bool/Int/Choice types this
            // registry uses (not the APVTS::Parameter subclass), a bare
            // setValue() would silently NOT update the atomic that
            // apvts.getRawParameterValue() hands the engine — only the
            // setValueNotifyingHost() -> sendValueChangedMessageToListeners()
            // path reaches APVTS's internal ParameterAdapter and flips that
            // atomic. So setValue() alone is not a safe substitute here
            // either: it would leave the engine deaf to the CC. Stash the
            // value instead; the Timer replays it as a real
            // setValueNotifyingHost() call, off the audio thread, coalesced
            // to the latest value per CC (a burst of ticks between two
            // timer fires collapses to one host notification, matching the
            // "notify only the latest value" requirement).
            pendingCcValue[(size_t) cc].store ((float) message.getControllerValue() / 127.0f,
                                                std::memory_order_release);
        }
    }
}

void MidiLearnManager::timerCallback()
{
    for (size_t cc = 0; cc < pendingCcValue.size(); ++cc)
    {
        // Sentinel-consuming exchange: any value stored after this read on
        // the audio thread is a fresh update this Timer will catch next
        // tick, not lost — the -1.0f sentinel can never collide with a
        // real (0..1) CC value.
        const auto value = pendingCcValue[cc].exchange (-1.0f, std::memory_order_acq_rel);
        if (value < 0.0f)
            continue;

        // Re-read the mapping now rather than trust what was true when the
        // audio thread stashed the value: the CC could have been
        // reassigned or cleared in between (message-thread-only actions),
        // and applying a stale value to the wrong parameter would be worse
        // than dropping it.
        const auto target = ccToParam[cc].load();
        if (target < 0)
            continue;

        // The real, message-thread call: updates the parameter's own value,
        // the APVTS raw atomic the engine reads, host automation, and any
        // UI/APVTS listeners — exactly as if the user had dragged the
        // control by hand. No gestures (begin/endChangeGesture): the
        // pre-fix code never sent them either, so this matches existing
        // host-automation-recording behavior rather than changing it.
        parametersByIndex[(size_t) target]->setValueNotifyingHost (value);
    }
}

} // namespace spa
