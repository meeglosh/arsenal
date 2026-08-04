#include "SPASynthProcessor.h"
#include "dsp/WavetableLoader.h"
#include "params/Randomizer.h"
#include "ui/SPASynthEditor.h"

namespace spa
{

namespace
{
    constexpr const char* wavetableStateType = "WAVETABLES";
    constexpr const char* sampleStateType = "SAMPLES";

    juce::Identifier slotPathProperty (int slot)
    {
        return { "slot" + juce::String (slot) };
    }

    const juce::Identifier wildnessProperty { "randomWildness" };
    const juce::Identifier lockMaskProperty { "randomLockMask" };
}

SPASynthProcessor::SPASynthProcessor()
    : juce::AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", params::createLayout())
{
    raw.masterGain = apvts.getRawParameterValue (params::id::masterGain);
    raw.glideMode = apvts.getRawParameterValue (params::id::glideMode);
    raw.glideTime = apvts.getRawParameterValue (params::id::glideTime);
    raw.voiceMode = apvts.getRawParameterValue (params::id::voiceMode);
    raw.notePriority = apvts.getRawParameterValue (params::id::notePriority);
    raw.unisonVoices = apvts.getRawParameterValue (params::id::unisonVoices);
    raw.unisonDetune = apvts.getRawParameterValue (params::id::unisonDetune);
    raw.unisonWidth = apvts.getRawParameterValue (params::id::unisonWidth);
    raw.ampAttack = apvts.getRawParameterValue (params::id::ampAttack);
    raw.ampDecay = apvts.getRawParameterValue (params::id::ampDecay);
    raw.ampSustain = apvts.getRawParameterValue (params::id::ampSustain);
    raw.ampRelease = apvts.getRawParameterValue (params::id::ampRelease);
    raw.oversampling = apvts.getRawParameterValue (params::id::oversampling);
    raw.filter1Enable = apvts.getRawParameterValue (params::id::filter1Enable);
    raw.filterType = apvts.getRawParameterValue (params::id::filter1Type);
    raw.filterKeytrack = apvts.getRawParameterValue (params::id::filter1Keytrack);
    raw.filter2Enable = apvts.getRawParameterValue (params::id::filter2Enable);
    raw.filter2Type = apvts.getRawParameterValue (params::id::filter2Type);
    raw.filter2Keytrack = apvts.getRawParameterValue (params::id::filter2Keytrack);
    raw.filterRouting = apvts.getRawParameterValue (params::id::filterRouting);

    for (int s = 0; s < params::numOscSlots; ++s)
    {
        auto& rs = raw.slots[(size_t) s];
        const auto pid = [s] (const char* key) { return params::id::oscSlot (s, key); };
        rs.enable      = apvts.getRawParameterValue (pid (params::id::osc::enable));
        rs.mode        = apvts.getRawParameterValue (pid (params::id::osc::mode));
        rs.phase       = apvts.getRawParameterValue (pid (params::id::osc::phase));
        rs.phaseMode   = apvts.getRawParameterValue (pid (params::id::osc::phaseMode));
        rs.unisonCount = apvts.getRawParameterValue (pid (params::id::osc::unisonCount));
        rs.sampleStart = apvts.getRawParameterValue (pid (params::id::osc::sampleStart));
        rs.loop        = apvts.getRawParameterValue (pid (params::id::osc::loop));
        rs.loopStart   = apvts.getRawParameterValue (pid (params::id::osc::loopStart));
        rs.loopEnd     = apvts.getRawParameterValue (pid (params::id::osc::loopEnd));
        rs.keytrack    = apvts.getRawParameterValue (pid (params::id::osc::keytrack));
        rs.rootNote    = apvts.getRawParameterValue (pid (params::id::osc::rootNote));
        rs.grainPitch  = apvts.getRawParameterValue (pid (params::id::osc::grainPitch));
        rs.analogShape = apvts.getRawParameterValue (pid (params::id::osc::analogShape));
        rs.fmRatio     = apvts.getRawParameterValue (pid (params::id::osc::fmRatio));
        rs.noiseColor  = apvts.getRawParameterValue (pid (params::id::osc::noiseColor));
    }

    for (int i = 0; i < params::numLFOs; ++i)
    {
        auto& rl = raw.lfos[(size_t) i];
        const auto pid = [i] (const char* key) { return params::id::lfoParam (i, key); };
        rl.shape    = apvts.getRawParameterValue (pid (params::id::lfo::shape));
        rl.rate     = apvts.getRawParameterValue (pid (params::id::lfo::rate));
        rl.sync     = apvts.getRawParameterValue (pid (params::id::lfo::sync));
        rl.division = apvts.getRawParameterValue (pid (params::id::lfo::division));
        rl.phase    = apvts.getRawParameterValue (pid (params::id::lfo::phase));
        rl.retrig   = apvts.getRawParameterValue (pid (params::id::lfo::retrig));
        rl.unipolar = apvts.getRawParameterValue (pid (params::id::lfo::unipolar));
    }

    for (int m = 0; m < params::numMacros; ++m)
        raw.macros[(size_t) m] = apvts.getRawParameterValue (params::id::macro (m));

    for (int r = 0; r < params::numModRoutes; ++r)
    {
        auto& rr = raw.routes[(size_t) r];
        rr.source = apvts.getRawParameterValue (params::id::routeParam (r, params::id::route::source));
        rr.dest   = apvts.getRawParameterValue (params::id::routeParam (r, params::id::route::dest));
        rr.depth  = apvts.getRawParameterValue (params::id::routeParam (r, params::id::route::depth));
    }

    {
        namespace ch = params::id::chaos;
        raw.chaos.enable         = apvts.getRawParameterValue (ch::enable);
        raw.chaos.pitchOn        = apvts.getRawParameterValue (ch::pitchOn);
        raw.chaos.pitchAmount    = apvts.getRawParameterValue (ch::pitchAmount);
        raw.chaos.phaseOn        = apvts.getRawParameterValue (ch::phaseOn);
        raw.chaos.phaseAmount    = apvts.getRawParameterValue (ch::phaseAmount);
        raw.chaos.positionOn     = apvts.getRawParameterValue (ch::positionOn);
        raw.chaos.positionAmount = apvts.getRawParameterValue (ch::positionAmount);
        raw.chaos.ampOn          = apvts.getRawParameterValue (ch::ampOn);
        raw.chaos.ampAmount      = apvts.getRawParameterValue (ch::ampAmount);
        raw.chaos.satOn          = apvts.getRawParameterValue (ch::satOn);
        raw.chaos.saturation     = apvts.getRawParameterValue (ch::saturation);
        raw.chaos.distOn         = apvts.getRawParameterValue (ch::distOn);
        raw.chaos.distortion     = apvts.getRawParameterValue (ch::distortion);
    }

    {
        namespace arpid = params::id::arp;
        raw.arp.enable   = apvts.getRawParameterValue (arpid::enable);
        raw.arp.mode     = apvts.getRawParameterValue (arpid::mode);
        raw.arp.division = apvts.getRawParameterValue (arpid::division);
        raw.arp.octaves  = apvts.getRawParameterValue (arpid::octaves);
        raw.arp.gate     = apvts.getRawParameterValue (arpid::gate);
        raw.arp.swing    = apvts.getRawParameterValue (arpid::swing);
        raw.arp.latch    = apvts.getRawParameterValue (arpid::latch);
        raw.arp.phrase   = apvts.getRawParameterValue (arpid::phrase);
        raw.arp.velMode  = apvts.getRawParameterValue (arpid::velMode);
        raw.arp.chance   = apvts.getRawParameterValue (arpid::chance);
        raw.arp.stutter  = apvts.getRawParameterValue (arpid::stutter);
        raw.arp.jump     = apvts.getRawParameterValue (arpid::jump);
        raw.arp.humanize = apvts.getRawParameterValue (arpid::humanize);
    }

    raw.dests.reserve ((size_t) params::numModDests());
    for (const auto& dest : params::modDestinations())
        raw.dests.push_back (apvts.getRawParameterValue (dest.def->id));

    {
        namespace fx = params::id::fx;
        auto& rf = raw.fx;
        rf.distEnable     = apvts.getRawParameterValue (fx::distEnable);
        rf.distType       = apvts.getRawParameterValue (fx::distType);
        rf.distDrive      = apvts.getRawParameterValue (fx::distDrive);
        rf.distTone       = apvts.getRawParameterValue (fx::distTone);
        rf.distMix        = apvts.getRawParameterValue (fx::distMix);
        rf.chorusEnable   = apvts.getRawParameterValue (fx::chorusEnable);
        rf.chorusRate     = apvts.getRawParameterValue (fx::chorusRate);
        rf.chorusDepth    = apvts.getRawParameterValue (fx::chorusDepth);
        rf.chorusFeedback = apvts.getRawParameterValue (fx::chorusFeedback);
        rf.chorusMix      = apvts.getRawParameterValue (fx::chorusMix);
        rf.delayEnable    = apvts.getRawParameterValue (fx::delayEnable);
        rf.delaySync      = apvts.getRawParameterValue (fx::delaySync);
        rf.delayTime      = apvts.getRawParameterValue (fx::delayTime);
        rf.delayDivision  = apvts.getRawParameterValue (fx::delayDivision);
        rf.delayFeedback  = apvts.getRawParameterValue (fx::delayFeedback);
        rf.delayPingPong  = apvts.getRawParameterValue (fx::delayPingPong);
        rf.delayMix       = apvts.getRawParameterValue (fx::delayMix);
        rf.reverbEnable   = apvts.getRawParameterValue (fx::reverbEnable);
        rf.reverbMode     = apvts.getRawParameterValue (fx::reverbMode);
        rf.reverbPreDelay = apvts.getRawParameterValue (fx::reverbPreDelay);
        rf.reverbSize     = apvts.getRawParameterValue (fx::reverbSize);
        rf.reverbDecay    = apvts.getRawParameterValue (fx::reverbDecay);
        rf.reverbDamping  = apvts.getRawParameterValue (fx::reverbDamping);
        rf.reverbModDepth = apvts.getRawParameterValue (fx::reverbModDepth);
        rf.reverbLowCut   = apvts.getRawParameterValue (fx::reverbLowCut);
        rf.reverbHighCut  = apvts.getRawParameterValue (fx::reverbHighCut);
        rf.reverbWidth    = apvts.getRawParameterValue (fx::reverbWidth);
        rf.reverbMix      = apvts.getRawParameterValue (fx::reverbMix);
        rf.eqEnable       = apvts.getRawParameterValue (fx::eqEnable);
        rf.eqCharacter    = apvts.getRawParameterValue (fx::eqCharacter);
        for (int b = 0; b < 8; ++b)
        {
            auto& bp = rf.eqBands[(size_t) b];
            bp.enable = apvts.getRawParameterValue (params::id::eqBand (b, fx::eqband::enable));
            bp.type   = apvts.getRawParameterValue (params::id::eqBand (b, fx::eqband::type));
            bp.freq   = apvts.getRawParameterValue (params::id::eqBand (b, fx::eqband::freq));
            bp.gain   = apvts.getRawParameterValue (params::id::eqBand (b, fx::eqband::gain));
            bp.q      = apvts.getRawParameterValue (params::id::eqBand (b, fx::eqband::q));
        }

        rf.modEnable      = apvts.getRawParameterValue (fx::modEnable);
        rf.modType        = apvts.getRawParameterValue (fx::modType);
        rf.modRate        = apvts.getRawParameterValue (fx::modRate);
        rf.modSync        = apvts.getRawParameterValue (fx::modSync);
        rf.modDivision    = apvts.getRawParameterValue (fx::modDivision);
        rf.modDepth       = apvts.getRawParameterValue (fx::modDepth);
        rf.modFeedback    = apvts.getRawParameterValue (fx::modFeedback);
        rf.modStages      = apvts.getRawParameterValue (fx::modStages);
        rf.modCentre      = apvts.getRawParameterValue (fx::modCentre);
        rf.modManual      = apvts.getRawParameterValue (fx::modManual);
        rf.modWidth       = apvts.getRawParameterValue (fx::modWidth);
        rf.modMix         = apvts.getRawParameterValue (fx::modMix);

        rf.tremEnable     = apvts.getRawParameterValue (fx::tremEnable);
        rf.tremRate       = apvts.getRawParameterValue (fx::tremRate);
        rf.tremSync       = apvts.getRawParameterValue (fx::tremSync);
        rf.tremDivision   = apvts.getRawParameterValue (fx::tremDivision);
        rf.tremDepth      = apvts.getRawParameterValue (fx::tremDepth);
        rf.tremShape      = apvts.getRawParameterValue (fx::tremShape);
        rf.tremStereo     = apvts.getRawParameterValue (fx::tremStereo);
        rf.tremMix        = apvts.getRawParameterValue (fx::tremMix);
        rf.vibEnable      = apvts.getRawParameterValue (fx::vibEnable);
        rf.vibRate        = apvts.getRawParameterValue (fx::vibRate);
        rf.vibSync        = apvts.getRawParameterValue (fx::vibSync);
        rf.vibDivision    = apvts.getRawParameterValue (fx::vibDivision);
        rf.vibDepth       = apvts.getRawParameterValue (fx::vibDepth);
        rf.vibMix         = apvts.getRawParameterValue (fx::vibMix);

        rf.limEnable      = apvts.getRawParameterValue (fx::limEnable);
        rf.limDrive       = apvts.getRawParameterValue (fx::limDrive);
        rf.limCeiling     = apvts.getRawParameterValue (fx::limCeiling);
        rf.limRelease     = apvts.getRawParameterValue (fx::limRelease);
        rf.limAutoRelease = apvts.getRawParameterValue (fx::limAutoRelease);
        rf.limCharacter   = apvts.getRawParameterValue (fx::limCharacter);
        rf.limStereoLink  = apvts.getRawParameterValue (fx::limStereoLink);
        rf.limTruePeak    = apvts.getRawParameterValue (fx::limTruePeak);
        rf.limLookahead   = apvts.getRawParameterValue (fx::limLookahead);
        rf.limAutoGain    = apvts.getRawParameterValue (fx::limAutoGain);

        rf.convEnable     = apvts.getRawParameterValue (fx::convEnable);
        rf.convMix        = apvts.getRawParameterValue (fx::convMix);
        rf.convWidth      = apvts.getRawParameterValue (fx::convWidth);
        rf.convPreDelay   = apvts.getRawParameterValue (fx::convPreDelay);
        rf.convDecay      = apvts.getRawParameterValue (fx::convDecay);
        rf.convDamping    = apvts.getRawParameterValue (fx::convDamping);
    }

    factoryTable = std::make_shared<const dsp::Wavetable> (dsp::Wavetable::createBasicShapes());
    for (int s = 0; s < params::numOscSlots; ++s)
    {
        slotTables[(size_t) s].current = factoryTable;
        slotTables[(size_t) s].live.store (factoryTable.get());
    }

    shared.telemetry = &telemetry;
    shared.glide = &glideState;
    midiLearn = std::make_unique<MidiLearnManager> (apvts);

    synth.addSound (new dsp::SPASynthSound());
    for (int i = 0; i < numVoices; ++i)
        synth.addVoice (new dsp::SPASynthVoice (shared));

    presetManager = std::make_unique<library::PresetManager> (
        [this] { return buildStateTree (false); },   // presets carry no MIDI map
        [this] (const juce::ValueTree& state) { restoreStateTree (state); },
        library::defaultPresetsRoot());

    // Auto-discover the library and make sure factory presets exist — no
    // user setup required when content sits in a standard install location.
    juce::MessageManager::callAsync ([weak = juce::WeakReference<SPASynthProcessor> (this)]
    {
        if (weak != nullptr)
            weak->refreshLibrary();
    });

    startTimer (150);  // purges retired wavetables; applies oversampling changes
}

SPASynthProcessor::~SPASynthProcessor()
{
    stopTimer();
}

void SPASynthProcessor::timerCallback()
{
    // Apply a pending oversampling-factor change: rebuild the engine at the new
    // rate under the callback lock so no processBlock touches it mid-rebuild.
    if (const int pf = pendingOsFactor.load (std::memory_order_relaxed);
        pf != currentOsFactor)
    {
        const juce::ScopedLock sl (getCallbackLock());
        rebuildOversampling (pf);
    }

    // Report latency: the limiter's lookahead (engine samples -> host) plus the
    // oversampler's own near-zero IIR latency.
    const int lat = desiredLatency.load (std::memory_order_relaxed) / juce::jmax (1, currentOsFactor)
                  + osLatencyHost;
    if (lat != getLatencySamples())
        setLatencySamples (lat);

    // Convolution IR shaping (decay/damping) reshapes + reloads the IR; do it
    // here (message thread), debounced to the timer, only when the values move.
    if (raw.fx.convDecay != nullptr)
        fxChain.setConvolutionShaping (raw.fx.convDecay->load(), raw.fx.convDamping->load());

    // Anything retired more than one timer period ago can no longer be in
    // use by the audio thread (it re-reads `live` every block).
    retiredTables.clear();
    retiredSamples.clear();
}

void SPASynthProcessor::installSample (int slot, std::shared_ptr<const dsp::SampleData> sample,
                                      juce::String path, juce::String error)
{
    auto& ss = slotSamples[(size_t) slot];
    ss.error = std::move (error);

    if (sample != nullptr)
    {
        if (ss.current != nullptr)
            retiredSamples.push_back (std::move (ss.current));
        ss.current = std::move (sample);
        ss.live.store (ss.current.get());
        ss.path = std::move (path);
    }

    sendChangeMessage();
}

void SPASynthProcessor::loadSampleFromFile (int slot, const juce::File& file)
{
    // Count the in-flight load (and broadcast) so the UI can show a loading
    // state; the decrement lives in the completion lambda — NOT in
    // installSample — so direct installs never underflow the counter.
    // requestSerial (message thread) makes the newest request win: rapid
    // quick-swap auditioning fires many background loads that finish out of
    // order, and a slow earlier analysis must not stomp the latest pick.
    auto& ss = slotSamples[(size_t) slot];
    const int serial = ++ss.requestSerial;
    ss.pendingLoads.fetch_add (1);
    sendChangeMessage();

    // Capture a weak ref, not raw `this` — the host can destroy the processor
    // while this background load is in flight (the loader retries with sleeps
    // up to ~360ms). The Thread::launch lambda below never touches `this`
    // after the load returns; only the callAsync completion may, and it
    // null-checks the weak ref first. That check-then-use is race-free because
    // JUCE::WeakReference nulls out (and the processor's destructor runs) on
    // the message thread, the same thread callAsync runs on — so there's no
    // concurrent check-vs-delete to race against, even though WeakReference
    // itself isn't thread-safe in general.
    juce::WeakReference<SPASynthProcessor> weak (this);
    juce::Thread::launch ([weak, slot, file, serial]
    {
        auto result = dsp::loadSampleFromFile (file);

        juce::MessageManager::callAsync ([weak, slot, serial, loaded = std::move (result),
                                          path = file.getFullPathName()]() mutable
        {
            if (weak == nullptr)
                return;   // processor was destroyed while this load was in flight
            auto& s = weak->slotSamples[(size_t) slot];
            s.pendingLoads.fetch_sub (1);
            if (serial != s.requestSerial)
                return;   // superseded by a newer request — drop this stale result
            weak->installSample (slot, std::move (loaded.sample), path, loaded.error);
        });
    });
}

juce::Array<juce::File> SPASynthProcessor::getPackSiblings (int slot) const
{
    const auto file = getSampleFile (slot);
    const auto root = library::findLibraryRoot();
    if (! root.isDirectory() || ! file.isAChildOf (root))
        return {};

    auto wavs = file.getParentDirectory()
                    .findChildFiles (juce::File::findFiles, false, "*.wav;*.WAV");
    std::sort (wavs.begin(), wavs.end(),
               [] (const juce::File& a, const juce::File& b)
               { return a.getFileName().compareIgnoreCase (b.getFileName()) < 0; });
    return wavs;
}

juce::String SPASynthProcessor::getSampleName (int slot) const
{
    const auto& current = slotSamples[(size_t) slot].current;
    return current != nullptr ? current->name : juce::String();
}

juce::String SPASynthProcessor::getSampleError (int slot) const
{
    return slotSamples[(size_t) slot].error;
}

float SPASynthProcessor::getRandomWildness() const
{
    return (float) (double) apvts.state.getProperty (wildnessProperty, 0.5);
}

void SPASynthProcessor::setRandomWildness (float wildness)
{
    apvts.state.setProperty (wildnessProperty, (double) wildness, nullptr);
}

bool SPASynthProcessor::isLockGroupLocked (int group) const
{
    const auto mask = (juce::uint32) (int) apvts.state.getProperty (lockMaskProperty, 0);
    return (mask & (1u << group)) != 0;
}

void SPASynthProcessor::setLockGroupLocked (int group, bool locked)
{
    auto mask = (juce::uint32) (int) apvts.state.getProperty (lockMaskProperty, 0);
    mask = locked ? (mask | (1u << group)) : (mask & ~(1u << group));
    apvts.state.setProperty (lockMaskProperty, (int) mask, nullptr);
}

void SPASynthProcessor::randomizeAll()
{
    auto& rng = juce::Random::getSystemRandom();
    const auto wildness = getRandomWildness();
    const auto lockedMask = (juce::uint32) (int) apvts.state.getProperty (lockMaskProperty, 0);

    params::randomizeAll (apvts, wildness, lockedMask, rng);

    // --- Musicality post-pass ------------------------------------------------
    const auto setNorm = [this] (const juce::String& id, float norm)
    {
        if (auto* param = apvts.getParameter (id))
            param->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, norm));
    };
    const auto realValue = [this] (const juce::String& id)
    {
        auto* param = apvts.getParameter (id);
        return param != nullptr ? param->convertFrom0to1 (param->getValue()) : 0.0f;
    };

    const bool oscsUnlocked = (lockedMask & (1u << (int) params::LockGroup::oscillators)) == 0;
    const bool filterUnlocked = (lockedMask & (1u << (int) params::LockGroup::filter)) == 0;

    if (oscsUnlocked)
    {
        namespace osc = params::id::osc;
        for (int s = 0; s < params::numOscSlots; ++s)
        {
            // A slot with no sample loaded can't be in a sample mode (the
            // synthesis engines are always valid).
            const auto mode = (params::OscMode) (int) realValue (
                params::id::oscSlot (s, osc::mode));
            if (slotSamples[(size_t) s].current == nullptr
                && (mode == params::OscMode::sample || mode == params::OscMode::granular))
                setNorm (params::id::oscSlot (s, osc::mode), 0.0f);

            // Keep loop points ordered with a usable window.
            const auto loopStart = realValue (params::id::oscSlot (s, osc::loopStart));
            const auto loopEnd = realValue (params::id::oscSlot (s, osc::loopEnd));
            if (loopEnd < loopStart + 0.05f)
                setNorm (params::id::oscSlot (s, osc::loopEnd),
                         juce::jlimit (0.0f, 1.0f, loopStart + 0.2f));
        }
    }

    if (filterUnlocked)
    {
        // High-passing everything above the note range means silence: pull
        // HP/notch cutoffs back into a musical zone.
        const auto type = (params::FilterType) (int) realValue (params::id::filter1Type);
        const auto cutoff = realValue (params::id::filter1Cutoff);
        const bool subtractive = type == params::FilterType::hp12
                              || type == params::FilterType::hp24
                              || type == params::FilterType::notch12
                              || type == params::FilterType::notch24;

        if (subtractive && cutoff > 2000.0f)
            if (auto* param = apvts.getParameter (params::id::filter1Cutoff))
                param->setValueNotifyingHost (
                    param->convertTo0to1 (150.0f + rng.nextFloat() * 850.0f));

        // Same guard for filter 2 when it rolls enabled + subtractive-high.
        const auto f2Enabled = realValue (params::id::filter2Enable) >= 0.5f;
        const auto f2Type = (params::FilterType) (int) realValue (params::id::filter2Type);
        const auto f2Cutoff = realValue (params::id::filter2Cutoff);
        const bool f2Subtractive = f2Type == params::FilterType::hp12
                                || f2Type == params::FilterType::hp24
                                || f2Type == params::FilterType::notch12
                                || f2Type == params::FilterType::notch24;
        if (f2Enabled && f2Subtractive && f2Cutoff > 2000.0f)
            if (auto* param = apvts.getParameter (params::id::filter2Cutoff))
                param->setValueNotifyingHost (
                    param->convertTo0to1 (150.0f + rng.nextFloat() * 850.0f));
    }

    // FX chain order joins RANDOMIZE ALL (respecting the FX lock), but the
    // limiter keeps its current slot so it stays where the user put it (last by
    // default) rather than being shuffled into the middle of the chain.
    const bool fxUnlocked = (lockedMask & (1u << (int) params::LockGroup::fx)) == 0;
    if (fxUnlocked)
    {
        auto order = getFxOrder();
        const int limiterId = (int) dsp::FXChain::Module::limiter;
        const int limiterPos = order.indexOf (limiterId);
        juce::Array<int> others;
        for (int id : order) if (id != limiterId) others.add (id);
        for (int i = others.size() - 1; i > 0; --i)
            std::swap (others.getReference (i), others.getReference (rng.nextInt (i + 1)));

        juce::Array<int> shuffled;
        int oi = 0;
        for (int pos = 0; pos < order.size(); ++pos)
            shuffled.add (pos == limiterPos ? limiterId : others[oi++]);
        setFxOrder (shuffled);
    }

    sendChangeMessage();
}

void SPASynthProcessor::installTable (int slot, std::shared_ptr<const dsp::Wavetable> table,
                                     juce::String path, juce::String error)
{
    auto& st = slotTables[(size_t) slot];
    st.error = std::move (error);

    if (table != nullptr)
    {
        retiredTables.push_back (std::move (st.current));
        st.current = std::move (table);
        st.live.store (st.current.get());
        st.path = std::move (path);
    }

    sendChangeMessage();
}

void SPASynthProcessor::loadWavetableFromFile (int slot, const juce::File& file)
{
    // Same loading-state + latest-wins bookkeeping as loadSampleFromFile
    // (requestSerial rationale: see that function).
    auto& st = slotTables[(size_t) slot];
    const int serial = ++st.requestSerial;
    st.pendingLoads.fetch_add (1);
    sendChangeMessage();

    // Weak-ref treatment mirrors loadSampleFromFile — see the comment there.
    juce::WeakReference<SPASynthProcessor> weak (this);
    juce::Thread::launch ([weak, slot, file, serial]
    {
        auto result = dsp::loadWavetableFromFile (file);

        juce::MessageManager::callAsync ([weak, slot, serial, loaded = std::move (result),
                                          path = file.getFullPathName()]() mutable
        {
            if (weak == nullptr)
                return;   // processor was destroyed while this load was in flight
            auto& t = weak->slotTables[(size_t) slot];
            t.pendingLoads.fetch_sub (1);
            if (serial != t.requestSerial)
                return;   // superseded by a newer request — drop this stale result
            weak->installTable (slot, std::move (loaded.table), path, loaded.error);
        });
    });
}

void SPASynthProcessor::setFactoryWavetable (int slot)
{
    installTable (slot, factoryTable, {}, {});
}

juce::String SPASynthProcessor::getWavetableName (int slot) const
{
    const auto& current = slotTables[(size_t) slot].current;
    return current != nullptr ? current->getName() : juce::String();
}

juce::String SPASynthProcessor::getWavetableError (int slot) const
{
    return slotTables[(size_t) slot].error;
}

void SPASynthProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    hostSampleRate = sampleRate;
    hostBlockSize = samplesPerBlock;
    midiClock.prepare (sampleRate);   // tempo detection stays in the host domain

    const int factor = 1 << juce::jlimit (0, 3, (int) raw.oversampling->load());
    currentOsFactor = factor;
    pendingOsFactor.store (factor, std::memory_order_relaxed);
    prepareEngine (sampleRate * factor, samplesPerBlock * factor);

    if (factor > 1)
    {
        oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
            2, (size_t) juce::roundToInt (std::log2 ((double) factor)),
            juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, false, false);
        oversampler->initProcessing ((size_t) samplesPerBlock);
        oversampler->reset();
        osLatencyHost = (int) std::ceil (oversampler->getLatencyInSamples());
    }
    else
    {
        oversampler.reset();
        osLatencyHost = 0;
    }
}

// All rate-dependent engine setup, at the (possibly oversampled) engine rate.
void SPASynthProcessor::prepareEngine (double engineRate, int engineBlock)
{
    currentSampleRate = engineRate;
    synth.setCurrentPlaybackSampleRate (engineRate);
    arp.prepare (engineRate);
    scaledMidi.ensureSize (8192);   // no audio-thread allocation; see Arpeggiator::scratch
    fxChain.prepare (engineRate, engineBlock);

    paraEnv.setSampleRate (engineRate);
    paraEnv.reset();
    paraGateWasOn = false;
    paraEnvBuf.setSize (1, juce::jmax (1, engineBlock), false, false, true);
    masterGain.reset (engineRate, 0.02);
    masterGain.setCurrentAndTargetValue (
        juce::Decibels::decibelsToGain (raw.masterGain->load(), -60.0f));
}

// Swap the oversampling factor (message thread; processing suspended by caller).
void SPASynthProcessor::rebuildOversampling (int factor)
{
    factor = 1 << juce::jlimit (0, 3, (int) std::round (std::log2 ((double) factor)));
    currentOsFactor = factor;
    prepareEngine (hostSampleRate * factor, hostBlockSize * factor);

    if (factor > 1)
    {
        oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
            2, (size_t) juce::roundToInt (std::log2 ((double) factor)),
            juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, false, false);
        oversampler->initProcessing ((size_t) hostBlockSize);
        oversampler->reset();
        osLatencyHost = (int) std::ceil (oversampler->getLatencyInSamples());
    }
    else
    {
        oversampler.reset();
        osLatencyHost = 0;
    }
}

// Paraphonic pre-pass + voices + FX + master, at whatever rate `buffer` is
// sized for. Called on the host buffer directly, or on the oversampled buffer.
void SPASynthProcessor::renderEngine (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // Paraphonic: advance one shared amp envelope from the collective key count
    // and render it per-sample for the voices to read this block. Gated on the
    // key count from the previous block, so voices attack one block after the
    // first key lands (imperceptible, and keeps this a simple pre-pass).
    if (shared.voiceMode == params::VoiceMode::paraphonic)
    {
        const bool anyKey = glideState.keysDown > 0;
        if (anyKey != paraGateWasOn)
        {
            if (anyKey) paraEnv.noteOn(); else paraEnv.noteOff();
            paraGateWasOn = anyKey;
        }
        paraEnv.setParameters ({ raw.ampAttack->load(), raw.ampDecay->load(),
                                 raw.ampSustain->load(), raw.ampRelease->load() });
        const int n = buffer.getNumSamples();
        auto* pe = paraEnvBuf.getWritePointer (0);
        for (int i = 0; i < n; ++i) pe[i] = paraEnv.getNextSample();
        shared.paraEnvBlock = pe;
        shared.paraGateActive = anyKey;
    }
    else
    {
        shared.paraEnvBlock = nullptr;
        shared.paraGateActive = false;
        if (paraGateWasOn) { paraEnv.reset(); paraGateWasOn = false; }
    }

    synth.renderNextBlock (buffer, midi, 0, buffer.getNumSamples());

    updateFXParams();
    fxChain.process (buffer, fxParams);

    masterGain.setTargetValue (juce::Decibels::decibelsToGain (raw.masterGain->load(), -60.0f));
    masterGain.applyGain (buffer, buffer.getNumSamples());
}

void SPASynthProcessor::setInternalBpm (double bpm)
{
    bpm = juce::jlimit (20.0, 300.0, bpm);
    internalBpm.store (bpm, std::memory_order_relaxed);
    apvts.state.setProperty ("standaloneBpm", bpm, nullptr);
}

void SPASynthProcessor::setTempoSyncMode (int mode)
{
    tempoSyncMode.store (mode, std::memory_order_relaxed);
    apvts.state.setProperty ("tempoSyncMode", mode, nullptr);
}

void SPASynthProcessor::setFxOrder (const juce::Array<int>& moduleIds)
{
    if (moduleIds.size() != dsp::FXChain::numModules)
        return;
    dsp::FXChain::Module ord[dsp::FXChain::numModules];
    for (int i = 0; i < dsp::FXChain::numModules; ++i)
        ord[i] = (dsp::FXChain::Module) moduleIds[i];
    const auto packed = dsp::FXChain::packOrder (ord);
    fxOrderPacked.store (packed, std::memory_order_relaxed);
    apvts.state.setProperty ("fxOrder", (juce::int64) packed, nullptr);
}

juce::Array<int> SPASynthProcessor::getFxOrder() const
{
    dsp::FXChain::Module ord[dsp::FXChain::numModules];
    dsp::FXChain::unpackOrder (fxOrderPacked.load (std::memory_order_relaxed), ord);
    juce::Array<int> ids;
    for (auto m : ord)
        ids.add ((int) m);
    return ids;
}

void SPASynthProcessor::loadConvolutionIR (const juce::File& file)
{
    convIrPath = file.existsAsFile() ? file.getFullPathName() : juce::String();
    fxChain.loadConvolutionIR (file);
    const auto libraryRoot = library::findLibraryRoot();
    apvts.state.setProperty ("convIR",
        convIrPath.isEmpty() ? juce::String() : library::toPortable (file, libraryRoot),
        nullptr);
    sendChangeMessage();   // refresh the IR name in the UI
}

void SPASynthProcessor::updateFXParams()
{
    const auto& rf = raw.fx;
    auto& p = fxParams;

    dsp::FXChain::unpackOrder (fxOrderPacked.load (std::memory_order_relaxed), p.order);

    p.distEnable     = rf.distEnable->load() >= 0.5f;
    p.distType       = (int) rf.distType->load();
    p.distDrive      = rf.distDrive->load();
    p.distToneHz     = rf.distTone->load();
    p.distMix        = rf.distMix->load();
    p.chorusEnable   = rf.chorusEnable->load() >= 0.5f;
    p.chorusRate     = rf.chorusRate->load();
    p.chorusDepth    = rf.chorusDepth->load();
    p.chorusFeedback = rf.chorusFeedback->load();
    p.chorusMix      = rf.chorusMix->load();
    p.delayEnable    = rf.delayEnable->load() >= 0.5f;
    p.delaySync      = rf.delaySync->load() >= 0.5f;
    p.delayTimeMs    = rf.delayTime->load();
    p.delayDivision  = (int) rf.delayDivision->load();
    p.delayFeedback  = rf.delayFeedback->load();
    p.delayPingPong  = rf.delayPingPong->load() >= 0.5f;
    p.delayMix       = rf.delayMix->load();
    p.reverbEnable   = rf.reverbEnable->load() >= 0.5f;
    p.reverbMode     = (int) rf.reverbMode->load();
    p.reverbPreDelay = rf.reverbPreDelay->load();
    p.reverbSize     = rf.reverbSize->load();
    p.reverbDecay    = rf.reverbDecay->load();
    p.reverbDamping  = rf.reverbDamping->load();
    p.reverbModDepth = rf.reverbModDepth->load();
    p.reverbLowCut   = rf.reverbLowCut->load();
    p.reverbHighCut  = rf.reverbHighCut->load();
    p.reverbWidth    = rf.reverbWidth->load();
    p.reverbMix      = rf.reverbMix->load();
    p.eqEnable       = rf.eqEnable->load() >= 0.5f;
    p.eqCharacter    = (int) rf.eqCharacter->load();
    for (int b = 0; b < 8; ++b)
    {
        const auto& bp = rf.eqBands[(size_t) b];
        auto& band = p.eqBands[(size_t) b];
        band.enabled = bp.enable->load() >= 0.5f;
        band.type    = (int) bp.type->load();
        band.freq    = bp.freq->load();
        band.gainDb  = bp.gain->load();
        band.q       = bp.q->load();
    }

    p.modEnable   = rf.modEnable->load() >= 0.5f;
    p.modType     = (int) rf.modType->load();
    p.modRate     = rf.modRate->load();
    p.modSync     = rf.modSync->load() >= 0.5f;
    p.modDivision = (int) rf.modDivision->load();
    p.modDepth    = rf.modDepth->load();
    p.modFeedback = rf.modFeedback->load();
    {
        static constexpr int stageCounts[] = { 2, 4, 6, 8, 12 };
        p.modStages = stageCounts[juce::jlimit (0, 4, (int) rf.modStages->load())];
    }
    p.modCentreHz = rf.modCentre->load();
    p.modManualMs = rf.modManual->load();
    p.modWidth    = rf.modWidth->load();
    p.modMix      = rf.modMix->load();

    p.tremEnable   = rf.tremEnable->load() >= 0.5f;
    p.tremRate     = rf.tremRate->load();
    p.tremSync     = rf.tremSync->load() >= 0.5f;
    p.tremDivision = (int) rf.tremDivision->load();
    p.tremDepth    = rf.tremDepth->load();
    p.tremShape    = (int) rf.tremShape->load();
    p.tremStereo   = rf.tremStereo->load();
    p.tremMix      = rf.tremMix->load();
    p.vibEnable    = rf.vibEnable->load() >= 0.5f;
    p.vibRate      = rf.vibRate->load();
    p.vibSync      = rf.vibSync->load() >= 0.5f;
    p.vibDivision  = (int) rf.vibDivision->load();
    p.vibDepth     = rf.vibDepth->load();
    p.vibMix       = rf.vibMix->load();

    p.limEnable      = rf.limEnable->load() >= 0.5f;
    p.limDrive       = rf.limDrive->load();
    p.limCeiling     = rf.limCeiling->load();
    p.limRelease     = rf.limRelease->load();
    p.limAutoRelease = rf.limAutoRelease->load() >= 0.5f;
    p.limCharacter   = (int) rf.limCharacter->load();
    p.limStereoLink  = rf.limStereoLink->load();
    p.limTruePeak    = rf.limTruePeak->load() >= 0.5f;
    p.limLookahead   = rf.limLookahead->load() >= 0.5f;
    p.limAutoGain    = rf.limAutoGain->load() >= 0.5f;

    p.convEnable   = rf.convEnable->load() >= 0.5f;
    p.convMix      = rf.convMix->load();
    p.convWidth    = rf.convWidth->load();
    p.convPreDelay = rf.convPreDelay->load();
    p.convDecay    = rf.convDecay->load();
    p.convDamping  = rf.convDamping->load();

    desiredLatency.store (fxChain.limiterLatencySamples (p), std::memory_order_relaxed);
    p.bpm            = shared.bpm;
}

bool SPASynthProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono();
}

void SPASynthProcessor::scanMidiControllers (const juce::MidiBuffer& midi)
{
    for (const auto metadata : midi)
    {
        const auto m = metadata.getMessage();

        if (m.isController() && m.getControllerNumber() == 1)
            lastModWheel = (float) m.getControllerValue() / 127.0f;
        else if (m.isChannelPressure())
            lastAftertouch = (float) m.getChannelPressureValue() / 127.0f;
    }

    shared.modWheel = lastModWheel;
    shared.aftertouch = lastAftertouch;
}

void SPASynthProcessor::updateSharedState (int blockLength)
{
    for (int s = 0; s < params::numOscSlots; ++s)
    {
        const auto& rs = raw.slots[(size_t) s];
        auto& slot = shared.slots[(size_t) s];

        slot.enabled     = rs.enable->load() >= 0.5f;
        slot.mode        = (params::OscMode) (int) rs.mode->load();
        slot.table       = slotTables[(size_t) s].live.load();
        slot.sample      = slotSamples[(size_t) s].live.load();
        slot.phase       = rs.phase->load();
        slot.phaseMode   = (params::PhaseMode) (int) rs.phaseMode->load();
        slot.unisonCount = (int) rs.unisonCount->load();
        slot.sampleStart = rs.sampleStart->load();
        slot.loop        = rs.loop->load() >= 0.5f;
        slot.loopStart   = rs.loopStart->load();
        slot.loopEnd     = rs.loopEnd->load();
        slot.keytrack    = rs.keytrack->load() >= 0.5f;
        slot.rootNote    = (int) rs.rootNote->load();
        slot.grainPitch  = rs.grainPitch->load();
        slot.analogShape = (int) rs.analogShape->load();
        slot.fmRatio     = rs.fmRatio->load();
        slot.noiseColor  = (int) rs.noiseColor->load();
    }

    shared.glideMode = (params::GlideMode) (int) raw.glideMode->load();
    shared.glideTimeMs = raw.glideTime->load();
    shared.voiceMode = (params::VoiceMode) (int) raw.voiceMode->load();
    shared.notePriority = (params::NotePriority) (int) raw.notePriority->load();
    shared.unisonVoices = (int) raw.unisonVoices->load();
    shared.unisonDetuneCents = raw.unisonDetune->load();
    shared.unisonWidth = raw.unisonWidth->load();

    shared.filter1Enabled = raw.filter1Enable->load() >= 0.5f;
    shared.filterType = (params::FilterType) (int) raw.filterType->load();
    shared.filterKeytrack = raw.filterKeytrack->load();
    shared.filter2Enabled = raw.filter2Enable->load() >= 0.5f;
    shared.filter2Type = (params::FilterType) (int) raw.filter2Type->load();
    shared.filter2Keytrack = raw.filter2Keytrack->load();
    shared.filterParallel = raw.filterRouting->load() >= 0.5f;

    // Normalized base values for every mod destination.
    const auto& dests = params::modDestinations();
    for (size_t d = 0; d < dests.size(); ++d)
        shared.baseNorm[d] = dests[d].def->range.convertTo0to1 (raw.dests[d]->load());

    // Compact list of routes that actually do something.
    shared.numActiveRoutes = 0;
    for (int r = 0; r < params::numModRoutes; ++r)
    {
        const auto& rr = raw.routes[(size_t) r];
        const auto source = (int) rr.source->load();
        const auto destChoice = (int) rr.dest->load();
        const auto depth = rr.depth->load();

        if (source == 0 || destChoice == 0 || depth == 0.0f)
            continue;

        auto& route = shared.routes[(size_t) shared.numActiveRoutes++];
        route.source = source;
        route.destIndex = destChoice - 1;  // choice 0 is "None"
        route.depth = depth;
    }

    // Transport (resolved once in processBlock: host, internal, or MIDI clock).
    shared.bpm = blockBpm;

    // LFO params + global free-running phases (value at block start; advanced
    // past the block for next time).
    for (int i = 0; i < params::numLFOs; ++i)
    {
        const auto& rl = raw.lfos[(size_t) i];
        auto& lp = shared.lfo[(size_t) i];
        lp.shape       = (params::LFOShape) (int) rl.shape->load();
        lp.rateHz      = rl.rate->load();
        lp.sync        = rl.sync->load() >= 0.5f;
        lp.division    = (int) rl.division->load();
        lp.phaseOffset = rl.phase->load();
        lp.retrig      = rl.retrig->load() >= 0.5f;
        lp.unipolar    = rl.unipolar->load() >= 0.5f;

        const auto inc = (double) dsp::LFO::effectiveRateHz (lp, shared.bpm) / currentSampleRate;
        lfoPhaseAccum[(size_t) i] = std::fmod (lfoPhaseAccum[(size_t) i], 1.0);
        shared.lfoGlobalPhase[(size_t) i] = lfoPhaseAccum[(size_t) i];
        lfoPhaseAccum[(size_t) i] += inc * blockLength;
    }

    for (int m = 0; m < params::numMacros; ++m)
        shared.macros[(size_t) m] = raw.macros[(size_t) m]->load();

    auto& ch = shared.chaos;
    ch.enabled          = raw.chaos.enable->load() >= 0.5f;
    ch.pitchOn          = raw.chaos.pitchOn->load() >= 0.5f;
    ch.pitchAmountCents = raw.chaos.pitchAmount->load();
    ch.phaseOn          = raw.chaos.phaseOn->load() >= 0.5f;
    ch.phaseAmount      = raw.chaos.phaseAmount->load();
    ch.positionOn       = raw.chaos.positionOn->load() >= 0.5f;
    ch.positionAmount   = raw.chaos.positionAmount->load();
    ch.ampOn            = raw.chaos.ampOn->load() >= 0.5f;
    ch.ampAmount        = raw.chaos.ampAmount->load();
    ch.satOn            = raw.chaos.satOn->load() >= 0.5f;
    ch.saturation       = raw.chaos.saturation->load();
    ch.distOn           = raw.chaos.distOn->load() >= 0.5f;
    ch.distortion       = raw.chaos.distortion->load();
}

void SPASynthProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    // Merge the on-screen / computer keyboard's notes into the host MIDI stream
    // before anything consumes it. MidiKeyboardState briefly locks here; that is
    // the standard JUCE idiom for an on-screen keyboard, the lock is held only
    // to splice a couple of queued note events and is uncontended in practice.
    keyboardState.processNextMidiBuffer (midi, 0, buffer.getNumSamples(), true);

    // Panic: from the UI button or an incoming All Sound/Notes Off (CC 120/123).
    // Kill every voice with no tail-off and clear the arp's latched/held chord so
    // a stuck (e.g. latched) note cannot keep sounding.
    bool doPanic = panicRequested.exchange (false, std::memory_order_relaxed);
    for (const auto md : midi)
    {
        const auto m = md.getMessage();
        if (m.isController()
            && (m.getControllerNumber() == 120 || m.getControllerNumber() == 123))
            doPanic = true;
    }
    if (doPanic)
    {
        synth.allNotesOff (0, false);   // channel <= 0 = all voices, no tail-off
        arp.reset();
        keyboardState.allNotesOff (0);
    }

    // Resolve tempo + transport once per block: host playhead if it provides a
    // tempo (plugin), otherwise the standalone internal BPM or external MIDI
    // clock. Everything tempo-synced (arp, delay, LFOs) reads blockBpm below.
    midiClock.process (midi, buffer.getNumSamples());
    blockBpm = 120.0; blockPlaying = true; blockPpq = 0.0;
    bool gotHostTempo = false, gotHostPpq = false;
    if (auto* playHead = getPlayHead())
        if (const auto position = playHead->getPosition())
            if (const auto bpm = position->getBpm())
            {
                blockBpm = *bpm;
                blockPlaying = position->getIsPlaying();
                if (const auto ppq = position->getPpqPosition())
                    { blockPpq = *ppq; gotHostPpq = true; }
                gotHostTempo = true;
            }
    if (! gotHostTempo)   // standalone / host without tempo
    {
        if (tempoSyncMode.load (std::memory_order_relaxed) == 1 && midiClock.hasClock())
        {
            blockBpm = midiClock.bpm();
            blockPlaying = midiClock.isPlaying();
        }
        else
        {
            blockBpm = internalBpm.load (std::memory_order_relaxed);
            blockPlaying = true;   // internal clock free-runs
        }
    }
    currentBpm.store (blockBpm, std::memory_order_relaxed);

    scanMidiControllers (midi);
    midiLearn->processMidi (midi);

    // Below the tempo/CC layer the whole engine runs at the engine rate, which
    // equals the host rate unless oversampling is on. Ask the message thread to
    // rebuild the engine if the factor changed (done under the callback lock).
    pendingOsFactor.store (1 << juce::jlimit (0, 3, (int) raw.oversampling->load()),
                           std::memory_order_relaxed);

    const int factor = currentOsFactor;
    const int hostN = buffer.getNumSamples();
    const int engN = hostN * factor;

    // Scale MIDI into the (possibly oversampled) engine sample domain.
    scaledMidi.clear();
    if (factor > 1)
        for (const auto md : midi)
            scaledMidi.addEvent (md.getMessage(), md.samplePosition * factor);
    juce::MidiBuffer& engMidi = factor > 1 ? scaledMidi : midi;

    // Arpeggiator transforms the note stream ahead of the synth (engine domain).
    {
        dsp::Arpeggiator::Params ap;
        ap.enable       = raw.arp.enable->load() >= 0.5f;
        ap.mode         = (params::ArpMode) (int) raw.arp.mode->load();
        ap.division     = (int) raw.arp.division->load();
        ap.octaves      = (int) raw.arp.octaves->load();
        ap.gate         = raw.arp.gate->load();
        ap.swing        = raw.arp.swing->load();
        ap.latch        = raw.arp.latch->load() >= 0.5f;
        ap.phrase       = (int) raw.arp.phrase->load();
        ap.velocityMode = (int) raw.arp.velMode->load();
        ap.chance       = raw.arp.chance->load();
        ap.stutter      = raw.arp.stutter->load();
        ap.jump         = raw.arp.jump->load();
        ap.humanize     = raw.arp.humanize->load();
        ap.bpm             = blockBpm;
        ap.sampleRate      = currentSampleRate;   // engine rate
        // Follow the host's ppq timeline only when we actually have one AND it is
        // playing; otherwise (standalone/internal clock, or a stopped host) the
        // arp free-runs on its own beat clock. Passing hostPlaying=true with a
        // frozen ppq (as the internal clock did) made it re-fire beat 0 every
        // block, so every note stuck.
        ap.hostPlaying     = gotHostPpq && blockPlaying;
        ap.ppqAtBlockStart = blockPpq;

        arp.process (engMidi, engN, ap);
    }

    updateSharedState (engN);

    if (factor > 1 && oversampler != nullptr)
    {
        // Render the engine at the oversampled rate, then decimate to host rate.
        juce::dsp::AudioBlock<float> hostBlock (buffer);
        auto osBlock = oversampler->processSamplesUp (hostBlock);   // silent -> upsampled
        const int numCh = buffer.getNumChannels();
        float* chans[2] = { osBlock.getChannelPointer (0),
                            numCh > 1 ? osBlock.getChannelPointer (1)
                                      : osBlock.getChannelPointer (0) };
        juce::AudioBuffer<float> osBuf (chans, numCh, (int) osBlock.getNumSamples());
        osBuf.clear();
        renderEngine (osBuf, engMidi);
        oversampler->processSamplesDown (hostBlock);   // anti-alias + decimate
    }
    else
    {
        renderEngine (buffer, engMidi);
    }

    // Block-level telemetry.
    int active = 0;
    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (synth.getVoice (i)->isVoiceActive())
            ++active;
    telemetry.activeVoices.store (active, std::memory_order_relaxed);
    telemetry.peakL.store (buffer.getMagnitude (0, 0, buffer.getNumSamples()),
                           std::memory_order_relaxed);
    telemetry.peakR.store (buffer.getNumChannels() > 1
                               ? buffer.getMagnitude (1, 0, buffer.getNumSamples())
                               : telemetry.peakL.load (std::memory_order_relaxed),
                           std::memory_order_relaxed);

    // Feed the EQ spectrum analyzer: push the master output (mono sum) into the
    // scope ring, write index published last so the UI reads a coherent window.
    {
        const int n = buffer.getNumSamples();
        const auto* l = buffer.getReadPointer (0);
        const auto* r = buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : l;
        int w = telemetry.scopeWrite.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
        {
            telemetry.scope[(size_t) w].store (0.5f * (l[i] + r[i]),
                                               std::memory_order_relaxed);
            w = (w + 1) & (dsp::Telemetry::scopeSize - 1);
        }
        telemetry.scopeWrite.store (w, std::memory_order_release);
    }

    // Scrolling limiter history: one frame per block. When the limiter is off we
    // still scroll the master level (with zero reduction) so the display lives.
    {
        const bool limOn = fxParams.limEnable;
        const float masterPk = juce::jmax (telemetry.peakL.load (std::memory_order_relaxed),
                                           telemetry.peakR.load (std::memory_order_relaxed));
        const float outLvl = limOn ? fxChain.limiterOutputPeak() : masterPk;
        const float grDb   = limOn ? fxChain.limiterGainReductionDb() : 0.0f;
        const int lw = telemetry.limWrite.load (std::memory_order_relaxed);
        telemetry.limOut[(size_t) lw].store (outLvl, std::memory_order_relaxed);
        telemetry.limGrDb[(size_t) lw].store (grDb, std::memory_order_relaxed);
        telemetry.limWrite.store ((lw + 1) % dsp::Telemetry::limiterHistory,
                                  std::memory_order_release);
    }
}

juce::ValueTree SPASynthProcessor::buildStateTree (bool includeMidiMap)
{
    const auto libraryRoot = library::findLibraryRoot();
    auto state = apvts.copyState();

    if (includeMidiMap)
        state.appendChild (midiLearn->toValueTree(), nullptr);

    auto wavetables = state.getOrCreateChildWithName (wavetableStateType, nullptr);
    auto samples = state.getOrCreateChildWithName (sampleStateType, nullptr);
    for (int s = 0; s < params::numOscSlots; ++s)
    {
        const auto& wtPath = slotTables[(size_t) s].path;
        const auto& smpPath = slotSamples[(size_t) s].path;
        wavetables.setProperty (slotPathProperty (s),
                                wtPath.isEmpty() ? juce::String()
                                    : library::toPortable (juce::File (wtPath), libraryRoot),
                                nullptr);
        samples.setProperty (slotPathProperty (s),
                             smpPath.isEmpty() ? juce::String()
                                 : library::toPortable (juce::File (smpPath), libraryRoot),
                             nullptr);
    }

    return state;
}

void SPASynthProcessor::restoreStateTree (const juce::ValueTree& incoming)
{
    if (! incoming.hasType (apvts.state.getType()))
        return;

    auto state = incoming.createCopy();
    const auto libraryRoot = library::findLibraryRoot();

    // Wavetable/sample paths ride along in the state tree but are not
    // parameters.
    auto wavetables = state.getChildWithName (wavetableStateType);
    if (wavetables.isValid())
        state.removeChild (wavetables, nullptr);
    auto samples = state.getChildWithName (sampleStateType);
    if (samples.isValid())
        state.removeChild (samples, nullptr);

    // MIDI map: restore when present (host sessions); presets omit it and
    // leave the current hardware mapping untouched.
    auto midiMap = state.getChildWithName (MidiLearnManager::mapTreeType);
    if (midiMap.isValid())
    {
        state.removeChild (midiMap, nullptr);
        midiLearn->restoreFromValueTree (midiMap);
    }

    apvts.replaceState (state);

    // Standalone tempo settings ride in the state tree (not parameters).
    internalBpm.store ((double) apvts.state.getProperty ("standaloneBpm", 120.0),
                       std::memory_order_relaxed);
    tempoSyncMode.store ((int) apvts.state.getProperty ("tempoSyncMode", 0),
                         std::memory_order_relaxed);
    fxOrderPacked.store ((juce::uint64) (juce::int64) apvts.state.getProperty (
                             "fxOrder", (juce::int64) dsp::FXChain::defaultOrderPacked()),
                         std::memory_order_relaxed);

    const auto convIR = apvts.state.getProperty ("convIR").toString();
    convIrPath = convIR.isEmpty() ? juce::String()
                                  : library::fromPortable (convIR, libraryRoot).getFullPathName();
    // Weak-ref treatment mirrors loadSampleFromFile — see the comment there.
    juce::MessageManager::callAsync ([weak = juce::WeakReference<SPASynthProcessor> (this),
                                      f = juce::File (convIrPath)]
    {
        if (weak != nullptr)
            weak->fxChain.loadConvolutionIR (f);
    });

    for (int s = 0; s < params::numOscSlots; ++s)
    {
        const auto wtPath = wavetables.isValid()
                          ? wavetables.getProperty (slotPathProperty (s)).toString()
                          : juce::String();
        const auto smpPath = samples.isValid()
                           ? samples.getProperty (slotPathProperty (s)).toString()
                           : juce::String();

        // Weak-ref treatment mirrors loadSampleFromFile — see the comment there.
        // (setStateInformation can hand the host a destroyed processor before
        // this deferred callAsync runs, e.g. during a fast preset/plugin
        // teardown sequence.)
        juce::MessageManager::callAsync ([weak = juce::WeakReference<SPASynthProcessor> (this),
                                          s, wtPath, smpPath, libraryRoot]
        {
            if (weak == nullptr)
                return;

            if (wtPath.isEmpty())
                weak->setFactoryWavetable (s);
            else
                weak->loadWavetableFromFile (s, library::fromPortable (wtPath, libraryRoot));

            if (smpPath.isNotEmpty())
                weak->loadSampleFromFile (s, library::fromPortable (smpPath, libraryRoot));
        });
    }
}

bool SPASynthProcessor::refreshLibrary()
{
    const auto root = library::findLibraryRoot();
    if (! root.isDirectory())
        return false;

    const auto packs = library::scanLibrary (root);
    presetManager->generateFactoryPresets (packs, root);
    presetManager->rescan();
    return true;
}

void SPASynthProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = buildStateTree().createXml())
        copyXmlToBinary (*xml, destData);
}

void SPASynthProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        restoreStateTree (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SPASynthProcessor::createEditor()
{
    return new SPASynthEditor (*this);
}

} // namespace spa

// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new spa::SPASynthProcessor();
}
