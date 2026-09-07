// Headless test suite: exercises the real processor and the wavetable
// pipeline without a host.

#include "SPASynthProcessor.h"
#include "dsp/Arpeggiator.h"
#include "dsp/FXChain.h"
#include "dsp/MidiClockSync.h"
#include "dsp/WavetableLoader.h"
#include "library/Library.h"
#include "library/PresetManager.h"
#include "params/ParameterRegistry.h"
#include "params/Randomizer.h"
#include "ui/SPASynthEditor.h"

#include <iostream>
#include <limits>
#include <set>
#include <typeinfo>

namespace
{
    int failures = 0;

    void expect (bool condition, const juce::String& description)
    {
        std::cout << (condition ? "  ok    " : "  FAIL  ") << description << "\n";
        if (! condition)
            ++failures;
    }

    float renderBlocks (spa::SPASynthProcessor& proc,
                        juce::AudioBuffer<float>& buffer,
                        juce::MidiBuffer& midi,
                        int numBlocks)
    {
        float peak = 0.0f;
        for (int i = 0; i < numBlocks; ++i)
        {
            proc.processBlock (buffer, midi);
            midi.clear();
            peak = juce::jmax (peak, buffer.getMagnitude (0, buffer.getNumSamples()));
        }
        return peak;
    }

    void setParam (spa::SPASynthProcessor& proc, const juce::String& id, float realValue)
    {
        auto* param = proc.getAPVTS().getParameter (id);
        jassert (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (realValue));
    }

    // Depth-first search for a component tagged with a given paramID
    // property -- the same "paramID" tag Controls.h/SectionPanel set on
    // every slider/combo/button for MIDI Learn. Lets a test reach the real
    // control inside a live editor tree without needing friend access to
    // panel internals.
    juce::Component* findByParamID (juce::Component& root, const juce::String& paramID)
    {
        if (root.getProperties()["paramID"].toString() == paramID)
            return &root;
        for (auto* child : root.getChildren())
            if (auto* found = findByParamID (*child, paramID))
                return found;
        return nullptr;
    }

    void renderSmokeTest()
    {
        std::cout << "renderSmokeTest\n";

        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 512;

        spa::SPASynthProcessor proc;
        proc.prepareToPlay (sampleRate, blockSize);

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;

        const auto silentPeak = renderBlocks (proc, buffer, midi, 8);
        expect (silentPeak < 1.0e-6f, "silent before any note");

        midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        const auto notePeak = renderBlocks (proc, buffer, midi, 32);
        expect (notePeak > 0.05f, "meaningful output during note");
        expect (notePeak < 2.0f, "output not clipping hot");

        midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
        renderBlocks (proc, buffer, midi, (int) (4.0 * sampleRate / blockSize));
        const auto releasedPeak = renderBlocks (proc, buffer, midi, 8);
        expect (releasedPeak < 1.0e-4f, "decays to silence after release");
    }

    void multiSlotUnisonTest()
    {
        std::cout << "multiSlotUnisonTest\n";

        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 512;

        spa::SPASynthProcessor proc;
        proc.prepareToPlay (sampleRate, blockSize);

        namespace id = spa::params::id;
        setParam (proc, id::oscSlot (1, id::osc::enable), 1.0f);
        setParam (proc, id::oscSlot (1, id::osc::coarse), 12.0f);
        setParam (proc, id::oscSlot (0, id::osc::unisonCount), 5.0f);
        setParam (proc, id::oscSlot (0, id::osc::unisonDetune), 30.0f);
        setParam (proc, id::oscSlot (0, id::osc::unisonWidth), 1.0f);
        setParam (proc, id::oscSlot (0, id::osc::position), 0.66f);

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;

        midi.addEvent (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100), 0);
        const auto notePeak = renderBlocks (proc, buffer, midi, 32);
        expect (notePeak > 0.05f, "two slots + unison produce output");
        expect (notePeak < 2.0f, "unison stack level-compensated");

        // Stereo width: with full width and detune the channels should differ.
        proc.processBlock (buffer, midi);
        float diff = 0.0f;
        for (int i = 0; i < blockSize; ++i)
            diff = juce::jmax (diff, std::abs (buffer.getSample (0, i) - buffer.getSample (1, i)));
        expect (diff > 1.0e-3f, "unison width produces stereo image");
    }

    void wavetableLoaderTest()
    {
        std::cout << "wavetableLoaderTest\n";

        // Build a 4-frame Serum-convention WAV (frames of 2048 samples).
        constexpr int frameSize = spa::dsp::Wavetable::tableSize;
        constexpr int numFrames = 4;

        juce::AudioBuffer<float> buffer (1, frameSize * numFrames);
        for (int f = 0; f < numFrames; ++f)
            for (int i = 0; i < frameSize; ++i)
            {
                // Frame f = f+1'th harmonic sine, so band-limiting is testable.
                const auto phase = juce::MathConstants<double>::twoPi * (f + 1) * i / frameSize;
                buffer.setSample (0, f * frameSize + i, (float) std::sin (phase));
            }

        const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getNonexistentChildFile ("spasynth-wt-test", ".wav");

        {
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
            auto writer = wav.createWriterFor (stream,
                                               juce::AudioFormatWriterOptions()
                                                   .withSampleRate (48000.0)
                                                   .withNumChannels (1)
                                                   .withBitsPerSample (24));
            expect (writer != nullptr, "test WAV writer created");
            if (writer == nullptr)
                return;
            writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
        }

        auto result = spa::dsp::loadWavetableFromFile (file);
        expect (result.table != nullptr, "wavetable loads: " + result.error);

        if (result.table != nullptr)
        {
            expect (result.table->getNumFrames() == numFrames, "detects 4 frames");

            // Frame 0 (pure fundamental) must survive even the last mip level.
            const auto* frame0 = result.table->getFrame (
                spa::dsp::Wavetable::numMipLevels - 1, 0);
            float peak = 0.0f;
            for (int i = 0; i < frameSize; ++i)
                peak = juce::jmax (peak, std::abs (frame0[i]));
            expect (peak > 0.5f, "fundamental survives deepest mip level");
        }

        file.deleteFile();
    }
}

    static void setRouteParams (spa::SPASynthProcessor& proc, int route,
                         spa::params::ModSource source,
                         const juce::String& destParamID, float depth)
    {
        namespace params = spa::params;
        namespace id = spa::params::id;

        // Destination choice index = dense mod-dest index + 1 ("None" is 0).
        const auto destChoice = (float) (params::modDestIndex (destParamID) + 1);

        setParam (proc, id::routeParam (route, id::route::source), (float) (int) source);
        setParam (proc, id::routeParam (route, id::route::dest), destChoice);
        setParam (proc, id::routeParam (route, id::route::depth), depth);
    }

    static void modMatrixMacroTest()
    {
        std::cout << "modMatrixMacroTest\n";

        namespace params = spa::params;
        namespace id = spa::params::id;

        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 512;

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;

        // Baseline: no modulation.
        float basePeak = 0.0f;
        {
            spa::SPASynthProcessor proc;
            proc.prepareToPlay (sampleRate, blockSize);
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            basePeak = renderBlocks (proc, buffer, midi, 32);
        }

        // Macro 1 at full, routed to Osc A level with depth -1 -> much quieter.
        {
            spa::SPASynthProcessor proc;
            proc.prepareToPlay (sampleRate, blockSize);
            setRouteParams (proc, 0, params::ModSource::macro1,
                            id::oscSlot (0, id::osc::level), -1.0f);
            setParam (proc, id::macro (0), 1.0f);

            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            const auto moddedPeak = renderBlocks (proc, buffer, midi, 32);

            expect (basePeak > 0.05f, "baseline note is audible");
            expect (moddedPeak < basePeak * 0.1f,
                    "macro->level route attenuates (base " + juce::String (basePeak)
                    + " vs modded " + juce::String (moddedPeak) + ")");
        }
    }

    static void lfoModulationTest()
    {
        std::cout << "lfoModulationTest\n";

        namespace params = spa::params;
        namespace id = spa::params::id;

        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 512;

        spa::SPASynthProcessor proc;
        proc.prepareToPlay (sampleRate, blockSize);

        // LFO 1: 4 Hz square, routed hard to Osc A level -> output pulses.
        setParam (proc, id::lfoParam (0, id::lfo::shape),
                  (float) (int) params::LFOShape::square);
        setParam (proc, id::lfoParam (0, id::lfo::rate), 4.0f);
        setRouteParams (proc, 0, params::ModSource::lfo1,
                        id::oscSlot (0, id::osc::level), -1.0f);

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);

        // Collect per-block peaks over ~1 second; square LFO should produce
        // loud and near-silent blocks.
        float minPeak = 1.0e9f, maxPeak = 0.0f;
        for (int b = 0; b < (int) (sampleRate / blockSize); ++b)
        {
            proc.processBlock (buffer, midi);
            midi.clear();
            if (b < 8)
                continue;  // let the attack settle
            const auto peak = buffer.getMagnitude (0, blockSize);
            minPeak = juce::jmin (minPeak, peak);
            maxPeak = juce::jmax (maxPeak, peak);
        }

        expect (maxPeak > 0.05f, "LFO-modulated note is audible at peaks");
        expect (minPeak < maxPeak * 0.2f,
                "square LFO->level pulses output (min " + juce::String (minPeak)
                + " vs max " + juce::String (maxPeak) + ")");
    }

    static void velocityRouteTest()
    {
        std::cout << "velocityRouteTest\n";

        namespace params = spa::params;
        namespace id = spa::params::id;

        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 512;

        spa::SPASynthProcessor proc;
        proc.prepareToPlay (sampleRate, blockSize);

        // Velocity opens the filter: base cutoff low, velocity routed up.
        setParam (proc, id::filter1Cutoff, 200.0f);
        setRouteParams (proc, 0, params::ModSource::velocity, id::filter1Cutoff, 1.0f);

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;

        auto brightness = [&] (juce::uint8 vel)
        {
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, vel), 0);
            renderBlocks (proc, buffer, midi, 16);
            // Rough high-frequency content estimate: mean absolute sample-to-
            // sample difference.
            float hf = 0.0f;
            for (int i = 1; i < blockSize; ++i)
                hf += std::abs (buffer.getSample (0, i) - buffer.getSample (0, i - 1));
            midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
            renderBlocks (proc, buffer, midi, (int) (2.0 * sampleRate / blockSize));
            return hf / (float) blockSize;
        };

        const auto soft = brightness (10);
        const auto hard = brightness (127);

        expect (hard > soft * 1.5f,
                "velocity->cutoff makes loud notes brighter (soft " + juce::String (soft)
                + " vs hard " + juce::String (hard) + ")");
    }

    static void chaosMixBypassTest()
    {
        std::cout << "chaosMixBypassTest\n";

        namespace id = spa::params::id;

        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 512;

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;

        // Everything cranked but mix at 0 must be bit-identical to a clean
        // render (phase mode Reset is deterministic).
        auto render = [&] (float mix)
        {
            spa::SPASynthProcessor proc;
            proc.prepareToPlay (sampleRate, blockSize);
            setParam (proc, id::chaos::depth, 1.0f);
            setParam (proc, id::chaos::rate, 10.0f);
            setParam (proc, id::chaos::satOn, 1.0f);
            setParam (proc, id::chaos::distOn, 1.0f);
            setParam (proc, id::chaos::saturation, 1.0f);
            setParam (proc, id::chaos::distortion, 1.0f);
            setParam (proc, id::chaos::mix, mix);

            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            juce::AudioBuffer<float> capture (2, blockSize);
            for (int b = 0; b < 16; ++b)
            {
                proc.processBlock (buffer, midi);
                midi.clear();
            }
            capture.makeCopyOf (buffer);
            return capture;
        };

        spa::SPASynthProcessor clean;
        clean.prepareToPlay (sampleRate, blockSize);
        setParam (clean, id::chaos::enable, 0.0f);
        juce::AudioBuffer<float> cleanBuf (2, blockSize);
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        for (int b = 0; b < 16; ++b)
        {
            clean.processBlock (cleanBuf, midi);
            midi.clear();
        }

        const auto mixed0 = render (0.0f);
        float maxDiff = 0.0f;
        for (int i = 0; i < blockSize; ++i)
            maxDiff = juce::jmax (maxDiff, std::abs (mixed0.getSample (0, i)
                                                     - cleanBuf.getSample (0, i)));
        expect (maxDiff < 1.0e-6f, "chaos mix=0 is bit-transparent (diff "
                                   + juce::String (maxDiff) + ")");

        // Full mix with heavy sat/dist must differ audibly from clean.
        const auto mixed1 = render (1.0f);
        float diff1 = 0.0f;
        for (int i = 0; i < blockSize; ++i)
            diff1 = juce::jmax (diff1, std::abs (mixed1.getSample (0, i)
                                                 - cleanBuf.getSample (0, i)));
        expect (diff1 > 1.0e-3f, "chaos mix=1 audibly changes output (diff "
                                 + juce::String (diff1) + ")");
    }

    static void chaosMatrixSourceTest()
    {
        std::cout << "chaosMatrixSourceTest\n";

        namespace params = spa::params;
        namespace id = spa::params::id;

        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 512;

        spa::SPASynthProcessor proc;
        proc.prepareToPlay (sampleRate, blockSize);

        // Chaos as a matrix source hammering oscillator level. Fast rate and
        // full depth: per-block peaks must fluctuate.
        setParam (proc, id::chaos::depth, 1.0f);
        setParam (proc, id::chaos::rate, 15.0f);
        setRouteParams (proc, 0, params::ModSource::chaos,
                        id::oscSlot (0, id::osc::level), -1.0f);

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);

        float minPeak = 1.0e9f, maxPeak = 0.0f;
        for (int b = 0; b < (int) (2.0 * sampleRate / blockSize); ++b)
        {
            proc.processBlock (buffer, midi);
            midi.clear();
            if (b < 8)
                continue;
            const auto peak = buffer.getMagnitude (0, blockSize);
            minPeak = juce::jmin (minPeak, peak);
            maxPeak = juce::jmax (maxPeak, peak);
        }

        expect (maxPeak > 0.02f, "chaos-modulated note is audible");
        expect (minPeak < maxPeak * 0.7f,
                "chaos source varies level over time (min " + juce::String (minPeak)
                + " vs max " + juce::String (maxPeak) + ")");
    }

    // ORGANIC CHAOS scrolling trace: the Telemetry ring should fill at the
    // expected decimated rate with real (non-constant, in-range) values when
    // chaos is active, and read as ~silent when it is not.
    static void chaosTraceTest()
    {
        std::cout << "chaosTraceTest\n";

        namespace params = spa::params;
        namespace id = spa::params::id;
        using Telemetry = spa::dsp::Telemetry;

        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 512;
        constexpr int numBlocks = (int) (1.0 * sampleRate / blockSize);   // ~1 second

        auto runOneSecond = [&] (bool chaosEnabled) -> int
        {
            spa::SPASynthProcessor proc;
            proc.prepareToPlay (sampleRate, blockSize);

            setParam (proc, id::chaos::enable, chaosEnabled ? 1.0f : 0.0f);
            setParam (proc, id::chaos::depth, 1.0f);
            setParam (proc, id::chaos::rate, 8.0f);

            juce::AudioBuffer<float> buffer (2, blockSize);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            for (int b = 0; b < numBlocks; ++b)
            {
                proc.processBlock (buffer, midi);
                midi.clear();
            }

            auto& tel = proc.getTelemetry();
            const auto writeIdx = tel.chaosTraceWrite.load();

            // The ring hasn't wrapped in one second at this decimation rate
            // (writeIdx < chaosTraceSize), so the actually-written samples
            // are simply indices [0, writeIdx) -- no wraparound arithmetic
            // needed here (unlike the UI's always-full-window read). Reduce
            // to aggregates rather than asserting per sample -- one bad
            // sample among hundreds should read as one failed expectation,
            // not silently dilute the "ok" count.
            float minV = 1.0e9f, maxV = -1.0e9f;
            bool allInRange = true;
            const auto n = juce::jmin (writeIdx, Telemetry::chaosTraceSize);
            for (int i = 0; i < n; ++i)
            {
                const auto v = tel.chaosTrace[(size_t) i].load();
                if (v < -1.0f || v > 1.0f)
                    allInRange = false;
                minV = juce::jmin (minV, v);
                maxV = juce::jmax (maxV, v);
            }

            expect (allInRange, "all chaos trace samples in [-1, 1]");

            if (chaosEnabled)
                expect (maxV - minV > 0.01f,
                        "chaos trace is non-constant when enabled (min "
                        + juce::String (minV) + " max " + juce::String (maxV) + ")");
            else
                expect (minV == 0.0f && maxV == 0.0f,
                        "chaos trace reads zero when chaos is disabled");

            return writeIdx;
        };

        const auto writesEnabled = runOneSecond (true);

        // ~750 mod chunks/s at 48k/64, decimated by chaosTraceDecimation.
        const auto expectedWrites =
            (int) (numBlocks * blockSize / 64 / Telemetry::chaosTraceDecimation);
        expect (std::abs (writesEnabled - expectedWrites) < expectedWrites / 10 + 2,
                "trace write count near expected (" + juce::String (writesEnabled)
                + " vs " + juce::String (expectedWrites) + ")");

        runOneSecond (false);
    }

    // Writes a WAV whose amplitude ramps 0 -> 1 over its length (440 Hz sine).
    static juce::File writeRampSine (double seconds, double sampleRate)
    {
        const auto numSamples = (int) (seconds * sampleRate);
        juce::AudioBuffer<float> buffer (1, numSamples);
        for (int i = 0; i < numSamples; ++i)
        {
            const auto ramp = (float) i / (float) numSamples;
            buffer.setSample (0, i, ramp * (float) std::sin (
                juce::MathConstants<double>::twoPi * 440.0 * i / sampleRate));
        }

        const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getNonexistentChildFile ("spasynth-sfx-test", ".wav");
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions()
                                                       .withSampleRate (sampleRate)
                                                       .withNumChannels (1)
                                                       .withBitsPerSample (24));
        if (writer != nullptr)
            writer->writeFromAudioSampleBuffer (buffer, 0, numSamples);
        return file;
    }

    // Pumps the message loop until the sample lands in the slot.
    static bool waitForSample (spa::SPASynthProcessor& proc, int slot, int timeoutMs)
    {
        const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
        while (juce::Time::getMillisecondCounter() < deadline)
        {
            if (proc.getSampleName (slot).isNotEmpty() || proc.getSampleError (slot).isNotEmpty())
                return proc.getSampleError (slot).isEmpty();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        }
        return false;
    }

    static void samplePlaybackTest()
    {
        std::cout << "samplePlaybackTest\n";

        namespace params = spa::params;
        namespace id = spa::params::id;

        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 512;

        const auto file = writeRampSine (2.0, sampleRate);

        spa::SPASynthProcessor proc;
        proc.prepareToPlay (sampleRate, blockSize);
        proc.loadSampleFromFile (0, file);

        // The in-flight flag drives the UI loading state: set synchronously
        // at launch, cleared only when the install lands on the message
        // thread (which needs the pump below).
        expect (proc.isSampleLoading (0), "slot reports loading while in flight");
        expect (waitForSample (proc, 0, 15000), "sample loads with analysis");
        expect (! proc.isSampleLoading (0), "loading flag clears once installed");

        // Classic sample mode, keytrack off -> plays at source pitch (440 Hz).
        setParam (proc, id::oscSlot (0, id::osc::mode), (float) (int) params::OscMode::sample);
        setParam (proc, id::oscSlot (0, id::osc::keytrack), 0.0f);
        setParam (proc, id::oscSlot (0, id::osc::sampleStart), 0.5f);  // start in audible region

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100), 0);
        renderBlocks (proc, buffer, midi, 16);

        // Estimate frequency by zero crossings over one block.
        proc.processBlock (buffer, midi);
        int crossings = 0;
        for (int i = 1; i < blockSize; ++i)
            if ((buffer.getSample (0, i - 1) < 0.0f) != (buffer.getSample (0, i) < 0.0f))
                ++crossings;
        const auto freq = (float) crossings * (float) sampleRate / (2.0f * blockSize);
        expect (freq > 400.0f && freq < 480.0f,
                "keytrack-off sample plays at source pitch (" + juce::String (freq) + " Hz)");

        file.deleteFile();
    }

    static void granularTest()
    {
        std::cout << "granularTest\n";

        namespace params = spa::params;
        namespace id = spa::params::id;

        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 512;

        const auto file = writeRampSine (2.0, sampleRate);

        spa::SPASynthProcessor proc;
        proc.prepareToPlay (sampleRate, blockSize);
        proc.loadSampleFromFile (0, file);
        expect (waitForSample (proc, 0, 15000), "sample loads for granular");

        setParam (proc, id::oscSlot (0, id::osc::mode), (float) (int) params::OscMode::granular);
        setParam (proc, id::oscSlot (0, id::osc::grainPos), 0.8f);  // loud region
        setParam (proc, id::oscSlot (0, id::osc::grainDensity), 30.0f);

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        const auto peak = renderBlocks (proc, buffer, midi, 32);
        expect (peak > 0.02f, "granular engine produces output (peak "
                              + juce::String (peak) + ")");

        // The live grain cloud is published for the animated display: at least
        // one grain is reported, and grains scan forward (a moving playhead),
        // not pinned at the static grain-position knob.
        auto& tel = proc.getTelemetry();
        expect (tel.grainViz[0].count.load() > 0, "grain cloud is published while sounding");

        const auto firstScan = tel.grainViz[0].pos[0].load();
        bool advanced = false;
        for (int i = 0; i < 8 && ! advanced; ++i)
        {
            proc.processBlock (buffer, midi);
            // Any grain whose read head has moved off the exact spawn centre
            // proves the playhead animates rather than sitting on grainPos.
            const auto n = tel.grainViz[0].count.load();
            for (int gi = 0; gi < n; ++gi)
                if (std::abs (tel.grainViz[0].pos[gi].load() - firstScan) > 1.0e-4f)
                    advanced = true;
        }
        expect (advanced, "grain read positions advance (playhead animates)");

        file.deleteFile();
    }

    static juce::File makeFakeLibrary();   // defined later in this file

    static void quickSwapTest()
    {
        std::cout << "quickSwapTest\n";

        namespace id = spa::params::id;
        namespace lib = spa::library;

        const auto savedRoot = lib::getLibraryRoot();   // restore machine setting after
        const auto root = makeFakeLibrary();            // Alpha/Beta packs, 3 wavs each
        lib::setLibraryRoot (root);

        spa::SPASynthProcessor proc;
        proc.prepareToPlay (48000.0, 512);

        auto settle = [&]
        {
            const auto deadline = juce::Time::getMillisecondCounter() + 15000u;
            while (proc.isSampleLoading (0)
                   && juce::Time::getMillisecondCounter() < deadline)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        };

        const auto alpha = root.getChildFile ("Alpha Pack");
        proc.loadSampleFromFile (0, alpha.getChildFile ("two.wav"));
        settle();

        // Siblings are exactly this pack's wavs, name-sorted, none from Beta.
        auto sibs = proc.getPackSiblings (0);
        expect (sibs.size() == 3, "pack siblings are the 3 Alpha wavs");
        bool allAlpha = true;
        for (const auto& f : sibs)
            allAlpha = allAlpha && f.getParentDirectory() == alpha;
        expect (allAlpha, "siblings come only from the current pack");
        expect (! sibs.isEmpty() && sibs.getFirst().getFileName() == "one.wav"
                    && sibs.getLast().getFileName() == "two.wav",
                "siblings are name-sorted (one, three, two)");

        // A file outside the library has no swap siblings.
        const auto stray = writeRampSine (0.2, 48000.0);
        proc.loadSampleFromFile (0, stray);
        settle();
        expect (proc.getPackSiblings (0).isEmpty(),
                "a sample outside the library exposes no siblings");

        // Latest request wins even when an earlier one is still resolving.
        proc.loadSampleFromFile (0, alpha.getChildFile ("three.wav"));
        proc.loadSampleFromFile (0, alpha.getChildFile ("two.wav"));
        settle();
        expect (proc.getSampleFile (0).getFileName() == "two.wav",
                "the newest load request wins (no stale stomp)");

        stray.deleteFile();
        root.deleteRecursively();
        lib::setLibraryRoot (savedRoot);
    }

    static void sfxFollowerTest()
    {
        std::cout << "sfxFollowerTest\n";

        namespace params = spa::params;
        namespace id = spa::params::id;

        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 512;

        const auto file = writeRampSine (2.0, sampleRate);

        spa::SPASynthProcessor proc;
        proc.prepareToPlay (sampleRate, blockSize);
        proc.loadSampleFromFile (0, file);
        expect (waitForSample (proc, 0, 15000), "sample loads for follower");

        // Slot A plays the ramping SFX (loop off). Its amp follower drives
        // slot B's level DOWN: as the SFX gets louder, slot B gets quieter.
        setParam (proc, id::oscSlot (0, id::osc::mode), (float) (int) params::OscMode::sample);
        setParam (proc, id::oscSlot (0, id::osc::loop), 0.0f);
        setParam (proc, id::oscSlot (0, id::osc::level), -60.0f);  // SFX itself silent
        setParam (proc, id::oscSlot (1, id::osc::enable), 1.0f);
        setRouteParams (proc, 0, params::ModSource::sfxAmpA,
                        id::oscSlot (1, id::osc::level), -1.0f);

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);

        // Average block peaks early vs late in the 2-second sample.
        auto averagePeak = [&] (int numBlocks)
        {
            float sum = 0.0f;
            for (int b = 0; b < numBlocks; ++b)
            {
                proc.processBlock (buffer, midi);
                midi.clear();
                sum += buffer.getMagnitude (0, blockSize);
            }
            return sum / (float) numBlocks;
        };

        averagePeak (8);  // attack settles
        const auto early = averagePeak (30);
        averagePeak ((int) (sampleRate / blockSize));  // skip ~1s into the ramp
        const auto late = averagePeak (30);

        expect (early > 0.02f, "follower patch is audible early on");
        expect (late < early * 0.6f,
                "amp follower tracks the SFX ramp (early " + juce::String (early)
                + " vs late " + juce::String (late) + ")");

        file.deleteFile();
    }

    // FX chain order packs to a uint64 and back; garbage falls back to natural.
    static void fxOrderTest()
    {
        std::cout << "fxOrderTest\n";
        using FX = spa::dsp::FXChain;

        FX::Module order[FX::numModules] { FX::Module::convolve, FX::Module::limiter,
            FX::Module::eq, FX::Module::reverb, FX::Module::tremVib, FX::Module::mod,
            FX::Module::delay, FX::Module::chorus, FX::Module::distortion };
        FX::Module back[FX::numModules];
        FX::unpackOrder (FX::packOrder (order), back);
        bool roundTrip = true;
        for (int i = 0; i < FX::numModules; ++i)
            roundTrip = roundTrip && (order[i] == back[i]);
        expect (roundTrip, "fx order packs and unpacks round-trip");

        FX::unpackOrder (0xFFFFFFFFFFFFFFFFull, back);   // garbage
        bool natural = true;
        for (int i = 0; i < FX::numModules; ++i)
            natural = natural && ((int) back[i] == i);
        expect (natural, "invalid packed order falls back to natural order");
    }

    // MIDI Beat Clock derives tempo (24 pulses per quarter). At 120 BPM that is
    // one clock every 1000 samples at 48k; the tracker should read ~120.
    static void midiClockTest()
    {
        std::cout << "midiClockTest\n";
        spa::dsp::MidiClockSync clock;
        clock.prepare (48000.0);

        constexpr int spc = 1000;         // samples per clock at 120 BPM
        constexpr int blockSize = 512;
        int absolute = 0, nextClock = 0;
        bool sawStart = false;

        for (int b = 0; b < 250; ++b)
        {
            juce::MidiBuffer midi;
            if (! sawStart) { midi.addEvent (juce::MidiMessage::midiStart(), 0); sawStart = true; }
            while (nextClock < absolute + blockSize)
            {
                midi.addEvent (juce::MidiMessage::midiClock(), nextClock - absolute);
                nextClock += spc;
            }
            clock.process (midi, blockSize);
            absolute += blockSize;
        }

        expect (clock.hasClock(), "midi clock detected");
        expect (clock.isPlaying(), "midi start sets transport playing");
        expect (std::abs (clock.bpm() - 120.0) < 2.0,
                "derives ~120 BPM from the clock (" + juce::String (clock.bpm()) + ")");
    }

    // Panic() must silence a latched arp (the stuck-note scenario): with latch
    // on, releasing the key keeps notes going; panic clears the held chord and
    // kills the voices.
    static void panicTest()
    {
        std::cout << "panicTest\n";
        namespace id = spa::params::id;
        constexpr double sr = 48000.0;
        constexpr int n = 512;

        spa::SPASynthProcessor proc;
        proc.prepareToPlay (sr, n);
        setParam (proc, id::chaos::enable, 0.0f);
        setParam (proc, id::arp::enable, 1.0f);
        setParam (proc, id::arp::latch, 1.0f);

        juce::AudioBuffer<float> buf (2, n);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        midi.addEvent (juce::MidiMessage::noteOff (1, 60), 10);   // latch holds it

        auto energyOver = [&] (int blocks)
        {
            float e = 0.0f;
            for (int b = 0; b < blocks; ++b)
            {
                proc.processBlock (buf, midi);
                midi.clear();
                e += buf.getRMSLevel (0, 0, n);
            }
            return e;
        };

        const float stuck = energyOver ((int) (0.5 * sr / n));
        expect (stuck > 0.0f,
                "latched arp keeps sounding after key release (" + juce::String (stuck) + ")");

        proc.panic();
        const float after = energyOver ((int) (0.3 * sr / n));
        expect (after < stuck * 0.05f,
                "panic silences the latched arp (" + juce::String (after) + ")");
    }

    // processBlockBypassed() must run the identical engine/FX pipeline
    // processBlock does (minus filtered note-ons) so a reverb/delay tail --
    // or a still-releasing voice -- keeps ringing out through host bypass
    // instead of being hard-cut, while new notes cannot start and note-offs
    // still pass through to wind everything down.
    static void bypassTailTest()
    {
        std::cout << "bypassTailTest\n";
        namespace id = spa::params::id;
        constexpr double sr = 48000.0;
        constexpr int n = 512;

        spa::SPASynthProcessor proc;
        proc.prepareToPlay (sr, n);
        setParam (proc, id::chaos::enable, 0.0f);
        setParam (proc, id::ampRelease, 0.1f);

        // An audible, self-decaying delay tail so there is something to ring
        // out once bypassed.
        setParam (proc, id::fx::delayEnable, 1.0f);
        setParam (proc, id::fx::delaySync, 0.0f);
        setParam (proc, id::fx::delayTime, 50.0f);
        setParam (proc, id::fx::delayFeedback, 0.6f);
        setParam (proc, id::fx::delayMix, 1.0f);

        juce::AudioBuffer<float> buf (2, n);
        juce::MidiBuffer midi;

        // Get a voice actively sounding, with some delay tail built up.
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        for (int b = 0; b < 6; ++b)
        {
            proc.processBlock (buf, midi);
            midi.clear();
        }
        expect (buf.getMagnitude (0, n) > 0.05f, "voice + delay audible before bypass");

        // First bypassed block, note still held (no note-off sent): the
        // default JUCE processBlockBypassed() would hard-zero an
        // instrument's whole output bus here since it has no input bus to
        // pass through -- our override must not.
        midi.clear();
        proc.processBlockBypassed (buf, midi);
        expect (buf.getMagnitude (0, n) > 0.02f,
                "bypassed block still audible, not hard-cut ("
                + juce::String (buf.getMagnitude (0, n)) + ")");

        // While bypassed: a note-on must be filtered out (no new voice
        // starts), while a note-off in the same call must still pass
        // through and start the release/tail wind-down.
        midi.clear();
        midi.addEvent (juce::MidiMessage::noteOn (1, 72, (juce::uint8) 100), 5);
        midi.addEvent (juce::MidiMessage::noteOff (1, 60), 250);
        proc.processBlockBypassed (buf, midi);
        midi.clear();

        // Render well past both the amp release and the delay feedback tail,
        // then look only at the LAST handful of blocks (not the max over the
        // whole run, which would still be dominated by the loud release
        // transient right after the note-off). If the note-on had
        // incorrectly started a voice on 72 (which never gets a note-off),
        // or the note-off on 60 had been dropped, that tail would never
        // settle to silence.
        const int settleBlocks = (int) (1.5 * sr / n);
        const int tailCheckBlocks = 8;
        float lateMag = 0.0f;
        for (int b = 0; b < settleBlocks; ++b)
        {
            proc.processBlockBypassed (buf, midi);
            if (b >= settleBlocks - tailCheckBlocks)
                lateMag = juce::jmax (lateMag, buf.getMagnitude (0, n));
        }
        expect (lateMag < 0.001f,
                "note-off passes through bypass and note-on is filtered, "
                "settles to silence (late mag " + juce::String (lateMag) + ")");
    }

    // Reverb MIX must be a true dry/wet dial: fully dry at 0, fully wet at 1.
    // (Was capped so the dry never dropped below 60%, so you could never reach
    // full reverb.) Settle the gain smoothing on silence, then probe the first
    // sample of an impulse — the reverb tail is still silent there, so that
    // sample is essentially the dry signal scaled by the dry level.
    static void reverbMixTest()
    {
        std::cout << "reverbMixTest\n";
        using FX = spa::dsp::FXChain;
        constexpr double sr = 48000.0;
        constexpr int n = 512;

        auto dryAtImpulse = [&] (float mix)
        {
            FX fx;
            fx.prepare (sr, n);
            FX::Params p;
            p.reverbEnable = true;
            p.reverbMix = mix;
            juce::AudioBuffer<float> buf (2, n);
            for (int b = 0; b < 12; ++b) { buf.clear(); fx.process (buf, p); }
            buf.clear();
            buf.setSample (0, 0, 1.0f);
            buf.setSample (1, 0, 1.0f);
            fx.process (buf, p);
            return buf.getSample (0, 0);
        };

        const float dry0 = dryAtImpulse (0.0f);
        const float dry1 = dryAtImpulse (1.0f);
        expect (dry0 > 0.9f && dry0 < 1.1f,
                "reverb mix 0 is unity dry, not boosted (" + juce::String (dry0) + ")");
        expect (dry1 < 0.1f,
                "reverb mix 1 removes the dry, full wet (" + juce::String (dry1) + ")");

        // Regression guard for the FDN wet-gain fix: at registry-default reverb
        // settings (size 0.5, decay 2.0, damping 0.5, mode Hall -- FX::Params'
        // defaults already mirror ParameterRegistry) and mix=1 (raw wet path
        // only), a short 0.5-amplitude noise burst must not blow the wet path
        // up past roughly the input scale. Fixed code measures ~1.8 here (old
        // unnormalized injection/tap measured ~6x); bound gives ~2x headroom
        // while staying well under the old hot behaviour.
        {
            FX fx;
            fx.prepare (sr, n);
            FX::Params mp;
            mp.reverbEnable = true;
            mp.reverbMix = 1.0f;

            uint32_t rng = 99999u;
            auto noise = [&rng]
            {
                rng = rng * 1664525u + 1013904223u;
                return ((float) (rng >> 9) / (float) (1u << 23)) * 2.0f - 1.0f;
            };

            float peak = 0.0f;
            const int blocks = (int) (1.0 * sr / n);
            const int exciteBlocks = (int) (0.2 * sr / n);
            juce::AudioBuffer<float> buf (2, n);
            for (int b = 0; b < blocks; ++b)
            {
                buf.clear();
                if (b < exciteBlocks)
                    for (int s = 0; s < n; ++s)
                    {
                        buf.setSample (0, s, noise() * 0.5f);
                        buf.setSample (1, s, noise() * 0.5f);
                    }
                fx.process (buf, mp);
                for (int ch = 0; ch < 2; ++ch)
                    for (int s = 0; s < n; ++s)
                        peak = juce::jmax (peak, std::abs (buf.getSample (ch, s)));
            }
            expect (peak < 4.0f,
                    "default-settings full-wet burst stays near input scale (peak "
                    + juce::String (peak) + ")");
        }
    }

    // The FDN reverb must stay finite and bounded across every mode even at
    // long decay + heavy modulation, both from an impulse and under sustained
    // input. A unitary feedback matrix with per-line gains < 1 guarantees this;
    // the test is the safety net against a future coefficient regression.
    static void reverbStabilityTest()
    {
        std::cout << "reverbStabilityTest\n";
        using FX = spa::dsp::FXChain;
        constexpr double sr = 48000.0;
        constexpr int n = 256;

        uint32_t rng = 22222u;
        auto noise = [&rng]
        {
            rng = rng * 1664525u + 1013904223u;
            return ((float) (rng >> 9) / (float) (1u << 23)) * 2.0f - 1.0f;
        };

        for (int mode = 0; mode < 5; ++mode)
        {
            FX fx;
            fx.prepare (sr, n);
            FX::Params p;
            p.reverbEnable = true;
            p.reverbMode = mode;
            p.reverbMix = 1.0f;
            p.reverbDecay = 10.0f;   // long tail
            p.reverbSize = 1.0f;
            p.reverbModDepth = 1.0f; // heavy tail modulation
            p.reverbDamping = 0.2f;

            float peak = 0.0f;
            bool finite = true;
            // ~2.5 s: 0.2 s of noise excitation, then decay in silence.
            const int blocks = (int) (2.5 * sr / n);
            const int exciteBlocks = (int) (0.2 * sr / n);
            juce::AudioBuffer<float> buf (2, n);
            for (int b = 0; b < blocks; ++b)
            {
                buf.clear();
                if (b < exciteBlocks)
                    for (int s = 0; s < n; ++s)
                    {
                        buf.setSample (0, s, noise() * 0.5f);
                        buf.setSample (1, s, noise() * 0.5f);
                    }
                fx.process (buf, p);
                for (int ch = 0; ch < 2; ++ch)
                    for (int s = 0; s < n; ++s)
                    {
                        const float v = buf.getSample (ch, s);
                        if (! std::isfinite (v)) finite = false;
                        peak = juce::jmax (peak, std::abs (v));
                    }
            }
            expect (finite, "reverb mode " + juce::String (mode) + " stays finite");
            // 0.5-amplitude noise excitation, mix=1 (raw wet path only). Bounds
            // the FDN's structural gain (1/sqrt(N) injection + tap normalization) --
            // a regression here means the wet path is hot again, not just unstable.
            expect (peak < 3.0f,
                    "reverb mode " + juce::String (mode) + " stays bounded (peak "
                    + juce::String (peak) + ")");
        }
    }

    // Bit-crush distortion type (Crush, appended index 3): DRIVE controls
    // both bit-depth quantisation and sample-and-hold decimation. Verifies
    // the quantiser collapses to few distinct values, decimation produces
    // held runs, drive=0 stays near-transparent (no stale hold), the other
    // three types are untouched, and the choice list is append-only correct.
    static void distCrushTest()
    {
        std::cout << "distCrushTest\n";
        using FX = spa::dsp::FXChain;
        namespace params = spa::params;
        namespace fx = spa::params::id::fx;

        constexpr double sr = 48000.0;
        constexpr int n = 512;
        constexpr double freqHz = 100.0;

        // Choice list: append-only, Crush must be last of exactly 4.
        const spa::params::ParamDef* distTypeDef = nullptr;
        for (const auto& def : params::all())
            if (def.id == fx::distType) { distTypeDef = &def; break; }
        expect (distTypeDef != nullptr, "fxDist.type param found");
        if (distTypeDef != nullptr)
        {
            expect (distTypeDef->choices.size() == 4, "fxDist.type has 4 choices");
            expect (distTypeDef->choices[3] == "Crush", "Crush is the 4th (appended) choice");
        }

        auto makeSine = [&] (juce::AudioBuffer<float>& buf, double amp)
        {
            for (int s = 0; s < n; ++s)
            {
                const auto v = (float) (amp * std::sin (2.0 * juce::MathConstants<double>::pi
                                                        * freqHz * (double) s / sr));
                buf.setSample (0, s, v);
                buf.setSample (1, s, v);
            }
        };

        // Runs the crush path at a given drive and returns (unique rounded
        // value count, longest run of identical consecutive samples, peak,
        // finite). Shared between the drive=1.0 and drive=0.5 checks below.
        auto runCrush = [&] (float drive, float toneHz)
        {
            FX fx;
            fx.prepare (sr, n);
            FX::Params p;
            p.distEnable = true;
            p.distType = 3;
            p.distDrive = drive;
            p.distToneHz = toneHz;
            p.distMix = 1.0f;

            juce::AudioBuffer<float> buf (2, n);
            makeSine (buf, 0.9);
            fx.process (buf, p);

            std::set<int> uniqueVals;
            bool finite = true;
            float peak = 0.0f;
            int longestRun = 0, currentRun = 1;
            float prev = buf.getSample (0, 0);
            for (int s = 0; s < n; ++s)
            {
                const auto v = buf.getSample (0, s);
                if (! std::isfinite (v)) finite = false;
                peak = juce::jmax (peak, std::abs (v));
                uniqueVals.insert ((int) std::round (v * 10000.0f));

                if (s > 0)
                {
                    if (std::abs (v - prev) < 1.0e-7f) { ++currentRun; longestRun = juce::jmax (longestRun, currentRun); }
                    else currentRun = 1;
                }
                prev = v;
            }

            struct Result { int uniqueCount; int longestRun; float peak; bool finite; };
            return Result { (int) uniqueVals.size(), longestRun, peak, finite };
        };

        // sr/4 is the TPT one-pole's critical-damping point (settles in a
        // single sample), keeping the post-crush lowpass from smearing the
        // held/quantised steps into extra transient values.
        const auto toneHzForUniqueness = (float) (sr * 0.25);

        // (a) + (c): drive = 1.0 -- few distinct values, finite/bounded, long
        // held runs (sample-and-hold decimation working).
        {
            const auto r = runCrush (1.0f, toneHzForUniqueness);
            expect (r.finite, "crush drive=1 stays finite");
            expect (r.peak <= 1.0f + 1.0e-3f, "crush drive=1 stays bounded (peak " + juce::String (r.peak) + ")");
            expect (r.uniqueCount < 20, "crush drive=1 quantises to few distinct values ("
                    + juce::String (r.uniqueCount) + ")");
            expect (r.longestRun >= 20, "crush drive=1 holds samples (longest run "
                    + juce::String (r.longestRun) + " at 48k)");
        }

        // Pins the exponential drive->bits/hold curve: drive=0.5 must sit
        // well below drive=0's (near-16-bit) distinct-value count, not
        // stranded near-transparent the way a linear mapping would leave it.
        {
            const auto r = runCrush (0.5f, toneHzForUniqueness);
            expect (r.finite, "crush drive=0.5 stays finite");
            expect (r.uniqueCount < 200, "crush drive=0.5 is well into the crushed range ("
                    + juce::String (r.uniqueCount) + " distinct values)");
        }

        // (b): drive = 0.0 -- near-transparent quantisation, no stale hold.
        // Compared against the same signal through an identical standalone
        // tone filter (not the raw dry signal) so this isolates the crush
        // quantiser/decimator's own transparency from the tone filter's own
        // (expected, shared-with-every-dist-type) shaping.
        {
            constexpr float toneHz = 20000.0f;

            FX fx;
            fx.prepare (sr, n);
            FX::Params p;
            p.distEnable = true;
            p.distType = 3;
            p.distDrive = 0.0f;
            p.distToneHz = toneHz;
            p.distMix = 1.0f;

            juce::AudioBuffer<float> dry (2, n);
            makeSine (dry, 0.9);
            auto wet = dry;
            fx.process (wet, p);

            juce::dsp::FirstOrderTPTFilter<float> refTone;
            refTone.prepare ({ sr, (juce::uint32) n, 1 });
            refTone.setType (juce::dsp::FirstOrderTPTFilterType::lowpass);
            refTone.setCutoffFrequency (toneHz);

            float maxErr = 0.0f;
            for (int s = 0; s < n; ++s)
            {
                const auto ref = refTone.processSample (0, dry.getSample (0, s));
                maxErr = juce::jmax (maxErr, std::abs (wet.getSample (0, s) - ref));
            }
            expect (maxErr < 1.0e-3f, "crush drive=0 near-transparent (max err "
                    + juce::String (maxErr) + ")");
        }

        // (d): types 0/1/2 unaffected by the Crush addition.
        for (int type = 0; type < 3; ++type)
        {
            FX fx;
            fx.prepare (sr, n);
            FX::Params p;
            p.distEnable = true;
            p.distType = type;
            p.distDrive = 0.5f;
            p.distToneHz = 20000.0f;
            p.distMix = 1.0f;

            juce::AudioBuffer<float> buf (2, n);
            makeSine (buf, 0.9);
            fx.process (buf, p);

            bool finite = true;
            float peak = 0.0f;
            for (int s = 0; s < n; ++s)
            {
                const auto v = buf.getSample (0, s);
                if (! std::isfinite (v)) finite = false;
                peak = juce::jmax (peak, std::abs (v));
            }
            expect (finite, "dist type " + juce::String (type) + " still finite");
            expect (peak > 0.01f, "dist type " + juce::String (type) + " still non-trivial");
        }
    }

    // Validates the parametric-EQ RBJ coefficients: the analytic magnitude
    // response must match what the biquads actually do, a bell boost must raise
    // its band's energy, and a high-cut must attenuate highs.
    static void parametricEqTest()
    {
        std::cout << "parametricEqTest\n";
        using EQ = spa::dsp::ParametricEQ;
        constexpr double sr = 48000.0;
        constexpr float twoPi = juce::MathConstants<float>::twoPi;

        std::array<EQ::Band, EQ::numBands> bands {};
        bands[0] = { true, (int) EQ::Type::bell, 1000.0f, 12.0f, 2.0f };
        const float atCentre = EQ::magnitudeDb (bands, 1000.0f, sr);
        const float atFar    = EQ::magnitudeDb (bands, 60.0f, sr);
        expect (std::abs (atCentre - 12.0f) < 0.5f,
                "bell centre gain ~ +12 dB (" + juce::String (atCentre) + ")");
        expect (std::abs (atFar) < 1.0f,
                "bell far from centre ~ flat (" + juce::String (atFar) + ")");

        auto rmsThrough = [&] (const std::array<EQ::Band, EQ::numBands>& bs, float freq)
        {
            EQ eq; eq.prepare (sr, 512);
            eq.updateBands (bs);
            juce::AudioBuffer<float> buf (2, 8192);
            for (int i = 0; i < 8192; ++i)
            {
                const float s = std::sin (twoPi * freq * (float) i / (float) sr);
                buf.setSample (0, i, s); buf.setSample (1, i, s);
            }
            eq.process (buf);
            double sum = 0; int n = 0;
            for (int i = 2000; i < 8192; ++i) { const float v = buf.getSample (0, i); sum += v * v; ++n; }
            return (float) std::sqrt (sum / n);
        };

        std::array<EQ::Band, EQ::numBands> off {};
        std::array<EQ::Band, EQ::numBands> boost {};
        boost[0] = { true, (int) EQ::Type::bell, 1000.0f, 12.0f, 2.0f };
        expect (rmsThrough (boost, 1000.0f) > rmsThrough (off, 1000.0f) * 2.0f,
                "bell boost raises 1 kHz RMS");

        std::array<EQ::Band, EQ::numBands> hicut {};
        hicut[0] = { true, (int) EQ::Type::highCut, 2000.0f, 0.0f, 0.707f };
        expect (rmsThrough (hicut, 10000.0f) < rmsThrough (off, 10000.0f) * 0.3f,
                "high-cut attenuates 10 kHz");
    }

    // Voice modes gate how many voices a chord (or a single note, for unison)
    // brings up. Counts are read from the telemetry active-voice tally after a
    // block that plays the notes.
    static void voiceModeTest()
    {
        std::cout << "voiceModeTest\n";
        namespace id = spa::params::id;
        constexpr double sr = 48000.0;
        constexpr int block = 512;

        auto activeVoices = [&] (int mode, int unison, std::vector<int> notes)
        {
            spa::SPASynthProcessor proc;
            proc.prepareToPlay (sr, block);
            setParam (proc, id::voiceMode, (float) mode);
            if (unison > 0) setParam (proc, id::unisonVoices, (float) unison);
            juce::MidiBuffer midi;
            for (size_t k = 0; k < notes.size(); ++k)
                midi.addEvent (juce::MidiMessage::noteOn (1, notes[k], (juce::uint8) 100),
                               (int) k);
            juce::AudioBuffer<float> buf (2, block);
            buf.clear();
            proc.processBlock (buf, midi);
            return proc.getTelemetry().activeVoices.load();
        };

        expect (activeVoices (0, 0, { 60, 64, 67, 71 }) == 4, "poly chord = 4 voices");
        expect (activeVoices (1, 0, { 60, 64, 67, 71 }) == 1, "mono chord = 1 voice");
        expect (activeVoices (2, 0, { 60, 64, 67, 71 }) == 2, "duo chord = 2 voices");
        expect (activeVoices (3, 0, { 60, 64, 67 }) == 3, "paraphonic chord = 3 voices");
        expect (activeVoices (4, 5, { 60 }) == 5, "unison note = 5 voices");

        // Paraphonic must actually sound (shared envelope opens on the chord) and
        // then, after all keys release, fall silent and free every voice.
        {
            spa::SPASynthProcessor proc;
            proc.prepareToPlay (sr, block);
            setParam (proc, id::voiceMode, 3.0f);
            setParam (proc, id::ampAttack, 0.001f);
            setParam (proc, id::ampRelease, 0.02f);
            juce::AudioBuffer<float> buf (2, block);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 110), 0);
            midi.addEvent (juce::MidiMessage::noteOn (1, 67, (juce::uint8) 110), 0);
            float peak = 0.0f;
            for (int b = 0; b < 8; ++b)   // let the shared env open
            {
                buf.clear(); juce::MidiBuffer m = (b == 0 ? midi : juce::MidiBuffer());
                proc.processBlock (buf, m);
                peak = juce::jmax (peak, buf.getMagnitude (0, 0, block));
            }
            expect (peak > 0.01f, "paraphonic chord produces sound");

            juce::MidiBuffer off;
            off.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
            off.addEvent (juce::MidiMessage::noteOff (1, 67), 0);
            buf.clear(); proc.processBlock (buf, off);
            for (int b = 0; b < 40; ++b) { buf.clear(); juce::MidiBuffer m; proc.processBlock (buf, m); }
            expect (proc.getTelemetry().activeVoices.load() == 0,
                    "paraphonic frees all voices after release");
        }
    }

    // Whole-synth oversampling must render correctly-levelled audio at every
    // factor (the up/render/decimate path is easy to get silent or blown up).
    // The factor is picked up at prepareToPlay, so we prepare fresh per factor.
    static void oversamplingTest()
    {
        std::cout << "oversamplingTest\n";
        namespace id = spa::params::id;
        constexpr double sr = 48000.0;
        constexpr int block = 512;

        auto renderPeak = [&] (int osIndex)
        {
            spa::SPASynthProcessor proc;
            setParam (proc, id::oversampling, (float) osIndex);
            proc.prepareToPlay (sr, block);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            juce::AudioBuffer<float> buf (2, block);
            float peak = 0.0f;
            for (int b = 0; b < 24; ++b)
            {
                buf.clear();
                juce::MidiBuffer m = (b == 0 ? midi : juce::MidiBuffer());
                proc.processBlock (buf, m);
                peak = juce::jmax (peak, buf.getMagnitude (0, 0, block));
            }
            return peak;
        };

        const float off = renderPeak (0);
        const float os2 = renderPeak (1);
        const float os4 = renderPeak (2);
        const float os8 = renderPeak (3);
        expect (off > 0.02f, "renders at 1x (" + juce::String (off) + ")");
        expect (os2 > 0.02f && os2 < off * 2.0f + 0.1f, "2x level matches 1x");
        expect (os4 > 0.02f && os4 < off * 2.0f + 0.1f, "4x level matches 1x");
        expect (os8 > 0.02f && os8 < off * 2.0f + 0.1f, "8x level matches 1x");
    }

    static void fxDelayReverbTest()
    {
        std::cout << "fxDelayReverbTest\n";

        namespace id = spa::params::id;

        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 512;

        // Short staccato note; measure energy in the window 0.2-1.0s after
        // note-off, with and without delay+reverb.
        auto tailEnergy = [&] (bool fxOn)
        {
            spa::SPASynthProcessor proc;
            proc.prepareToPlay (sampleRate, blockSize);
            setParam (proc, id::ampRelease, 0.02f);
            setParam (proc, id::chaos::enable, 0.0f);

            if (fxOn)
            {
                setParam (proc, id::fx::delayEnable, 1.0f);
                setParam (proc, id::fx::delaySync, 0.0f);
                setParam (proc, id::fx::delayTime, 150.0f);
                setParam (proc, id::fx::delayFeedback, 0.6f);
                setParam (proc, id::fx::delayMix, 0.8f);
                setParam (proc, id::fx::reverbEnable, 1.0f);
                setParam (proc, id::fx::reverbMix, 0.5f);
            }

            juce::AudioBuffer<float> buffer (2, blockSize);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            midi.addEvent (juce::MidiMessage::noteOff (1, 60), blockSize - 1);

            float energy = 0.0f;
            const auto blocksTotal = (int) (1.0 * sampleRate / blockSize);
            const auto blocksSkip = (int) (0.2 * sampleRate / blockSize);
            for (int b = 0; b < blocksTotal; ++b)
            {
                proc.processBlock (buffer, midi);
                midi.clear();
                if (b >= blocksSkip)
                    energy += buffer.getRMSLevel (0, 0, blockSize);
            }
            return energy;
        };

        const auto dry = tailEnergy (false);
        const auto wet = tailEnergy (true);
        expect (wet > dry * 3.0f + 1.0e-4f,
                "delay+reverb produce a tail (dry " + juce::String (dry)
                + " vs wet " + juce::String (wet) + ")");
    }

    // The reported plugin tail (getTailLengthSeconds -> FXChain::tailSeconds)
    // must include the Convolve impulse length, or hosts truncate bounces/
    // freezes before the convolution ring-out finishes.
    static void convolveTailLengthTest()
    {
        std::cout << "convolveTailLengthTest\n";

        namespace id = spa::params::id;

        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 512;
        constexpr double irSeconds = 2.0;

        // A known-length impulse: 2 seconds of low-level noise at 48 kHz.
        const auto irFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                .getNonexistentChildFile ("spasynth-conv-ir-test", ".wav");
        {
            const int numSamples = (int) (irSeconds * sampleRate);
            juce::AudioBuffer<float> irBuffer (1, numSamples);
            juce::Random rng (1234);
            for (int i = 0; i < numSamples; ++i)
                irBuffer.setSample (0, i, rng.nextFloat() * 2.0f - 1.0f);

            juce::WavAudioFormat wav;
            std::unique_ptr<juce::OutputStream> stream = irFile.createOutputStream();
            auto writer = wav.createWriterFor (stream,
                                               juce::AudioFormatWriterOptions()
                                                   .withSampleRate (sampleRate)
                                                   .withNumChannels (1)
                                                   .withBitsPerSample (24));
            expect (writer != nullptr, "test IR WAV writer created");
            if (writer != nullptr)
            {
                writer->writeFromAudioSampleBuffer (irBuffer, 0, numSamples);
                writer.reset();
            }
        }

        spa::SPASynthProcessor proc;
        proc.prepareToPlay (sampleRate, blockSize);

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        const auto pump = [&]   // updateFXParams only runs inside processBlock
        {
            buffer.clear();
            proc.processBlock (buffer, midi);
        };

        // Convolve disabled: no other tail-producing FX on, so the reported
        // tail should be ~0 even with an IR loaded.
        proc.loadConvolutionIR (irFile);
        setParam (proc, id::fx::convEnable, 0.0f);
        setParam (proc, id::fx::delayEnable, 0.0f);
        setParam (proc, id::fx::reverbEnable, 0.0f);
        pump();
        expect (proc.getTailLengthSeconds() < irSeconds * 0.5,
                "tail excludes the IR while Convolve is disabled ("
                + juce::String (proc.getTailLengthSeconds()) + "s)");

        // Convolve enabled: the reported tail must cover the (reshaped) IR.
        setParam (proc, id::fx::convEnable, 1.0f);
        pump();
        const auto tailOn = proc.getTailLengthSeconds();
        expect (tailOn >= irSeconds - 0.1,
                "tail includes the Convolve IR length once enabled (tail "
                + juce::String (tailOn) + "s vs IR " + juce::String (irSeconds) + "s)");

        irFile.deleteFile();
    }

    static void fxEQDistortionTest()
    {
        std::cout << "fxEQDistortionTest\n";

        namespace id = spa::params::id;

        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 512;

        auto brightnessWith = [&] (auto configure)
        {
            spa::SPASynthProcessor proc;
            proc.prepareToPlay (sampleRate, blockSize);
            setParam (proc, id::chaos::enable, 0.0f);
            setParam (proc, id::oscSlot (0, id::osc::position), 0.66f);  // saw-ish
            configure (proc);

            juce::AudioBuffer<float> buffer (2, blockSize);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            renderBlocks (proc, buffer, midi, 16);

            proc.processBlock (buffer, midi);
            float hf = 0.0f;
            for (int i = 1; i < blockSize; ++i)
                hf += std::abs (buffer.getSample (0, i) - buffer.getSample (0, i - 1));
            return hf / (float) blockSize;
        };

        const auto flat = brightnessWith ([] (auto&) {});
        const auto darkened = brightnessWith ([] (auto& proc)
        {
            namespace fx = id::fx;
            setParam (proc, fx::eqEnable, 1.0f);
            // Band 7: high-shelf cut. Band 5: bell cut at 4 kHz.
            setParam (proc, id::eqBand (6, fx::eqband::enable), 1.0f);
            setParam (proc, id::eqBand (6, fx::eqband::type), 2.0f /* High Shelf */);
            setParam (proc, id::eqBand (6, fx::eqband::gain), -18.0f);
            setParam (proc, id::eqBand (5, fx::eqband::enable), 1.0f);
            setParam (proc, id::eqBand (5, fx::eqband::type), 0.0f /* Bell */);
            setParam (proc, id::eqBand (5, fx::eqband::freq), 4000.0f);
            setParam (proc, id::eqBand (5, fx::eqband::gain), -18.0f);
        });
        expect (darkened < flat * 0.8f,
                "EQ high/mid cut darkens output (flat " + juce::String (flat)
                + " vs cut " + juce::String (darkened) + ")");

        // Distortion flattens peaks: crest factor (peak/RMS) must drop.
        auto crestWith = [&] (auto configure)
        {
            spa::SPASynthProcessor proc;
            proc.prepareToPlay (sampleRate, blockSize);
            setParam (proc, id::chaos::enable, 0.0f);
            setParam (proc, id::oscSlot (0, id::osc::position), 0.66f);
            configure (proc);

            juce::AudioBuffer<float> buffer (2, blockSize);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            renderBlocks (proc, buffer, midi, 16);

            proc.processBlock (buffer, midi);
            const auto peak = buffer.getMagnitude (0, blockSize);
            const auto rms = buffer.getRMSLevel (0, 0, blockSize);
            return rms > 0.0f ? peak / rms : 0.0f;
        };

        const auto crestClean = crestWith ([] (auto&) {});
        const auto crestDriven = crestWith ([] (auto& proc)
        {
            setParam (proc, id::fx::distEnable, 1.0f);
            setParam (proc, id::fx::distDrive, 1.0f);
            setParam (proc, id::fx::distTone, 20000.0f);
        });
        expect (crestDriven < crestClean * 0.9f,
                "distortion flattens peaks (clean crest " + juce::String (crestClean)
                + " vs driven " + juce::String (crestDriven) + ")");
    }

    // Regression for the "toggle blast" bug: FX modules with internal
    // recursive state (EQ biquads, the phaser/flanger's allpass/feedback/
    // delay state) used to freeze that state when disabled and resume from it
    // on re-enable, dumping stale (possibly hot) energy into the mix as a
    // decaying blast — reported as intermittent noise blasts in a restored
    // Logic session. Both halves excite the module with loud noise, disable
    // it, let a few silent blocks pass (frozen state, module skipped so
    // output stays silent), then re-enable with silence and assert the
    // output stays near-silent instead of ringing out the trapped state.
    static void fxToggleBlastTest()
    {
        std::cout << "fxToggleBlastTest\n";
        using FX = spa::dsp::FXChain;
        using EQ = spa::dsp::ParametricEQ;
        constexpr double sr = 48000.0;
        constexpr int n = 256;

        uint32_t rng = 77777u;
        auto noise = [&rng]
        {
            rng = rng * 1664525u + 1013904223u;
            return ((float) (rng >> 9) / (float) (1u << 23)) * 2.0f - 1.0f;
        };

        auto peakOverBlocks = [&] (FX& fx, const FX::Params& p, int blocks, bool excite)
        {
            float peak = 0.0f;
            juce::AudioBuffer<float> buf (2, n);
            for (int b = 0; b < blocks; ++b)
            {
                buf.clear();
                if (excite)
                    for (int s = 0; s < n; ++s)
                    {
                        const float v = noise() * 0.9f;
                        buf.setSample (0, s, v);
                        buf.setSample (1, s, v);
                    }
                fx.process (buf, p);
                for (int ch = 0; ch < 2; ++ch)
                    for (int s = 0; s < n; ++s)
                        peak = juce::jmax (peak, std::abs (buf.getSample (ch, s)));
            }
            return peak;
        };

        // -- Parametric EQ: hi-Q, high-gain bell band, disable then re-enable.
        {
            FX fx; fx.prepare (sr, n);
            FX::Params p;
            p.eqEnable = true;
            p.eqBands[0] = { true, (int) EQ::Type::bell, 2000.0f, 24.0f, 18.0f };

            peakOverBlocks (fx, p, 40, true);          // ring the band up
            p.eqBands[0].enabled = false;
            peakOverBlocks (fx, p, 20, false);          // frozen while disabled
            p.eqBands[0].enabled = true;
            const auto blastPeak = peakOverBlocks (fx, p, 20, false);   // re-enable, silence in

            expect (blastPeak < 0.05f,
                    "EQ band re-enable does not ring out trapped state (peak "
                    + juce::String (blastPeak) + ")");
        }

        // -- Mod effect (flanger): high feedback, disable then re-enable.
        {
            FX fx; fx.prepare (sr, n);
            FX::Params p;
            p.modEnable = true;
            p.modType = 1;              // flanger
            p.modRate = 0.7f;
            p.modDepth = 0.9f;
            p.modFeedback = 0.95f;
            p.modManualMs = 5.0f;
            p.modMix = 1.0f;

            peakOverBlocks (fx, p, 40, true);
            p.modEnable = false;
            peakOverBlocks (fx, p, 20, false);
            p.modEnable = true;
            const auto blastPeak = peakOverBlocks (fx, p, 20, false);

            expect (blastPeak < 0.05f,
                    "mod (flanger) re-enable does not ring out trapped feedback (peak "
                    + juce::String (blastPeak) + ")");
        }
    }

    static void randomizerTest()
    {
        std::cout << "randomizerTest\n";

        namespace params = spa::params;
        namespace id = spa::params::id;

        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 512;

        spa::SPASynthProcessor proc;
        proc.prepareToPlay (sampleRate, blockSize);
        auto& apvts = proc.getAPVTS();

        auto snapshot = [&]
        {
            std::vector<float> values;
            for (const auto& def : params::all())
                values.push_back (apvts.getParameter (def.id)->getValue());
            return values;
        };

        // Re-roll changes a substantial number of parameters.
        const auto before = snapshot();
        proc.randomizeAll();
        const auto after = snapshot();

        int changed = 0;
        for (size_t i = 0; i < before.size(); ++i)
            if (std::abs (before[i] - after[i]) > 1.0e-4f)
                ++changed;
        expect (changed > 30, "re-roll changes many params ("
                              + juce::String (changed) + " changed)");

        // Osc A always survives a re-roll enabled.
        expect (apvts.getParameter (id::oscSlot (0, id::osc::enable))->getValue() >= 0.5f,
                "osc A stays enabled after re-roll");

        // Constrained bounds hold at default wildness: cutoff never below its
        // minNorm window, resonance never in the self-oscillation zone.
        // Coarse tune never rolls at all — semitone jumps break the song key
        // (fine detune still does).
        const auto coarseBefore = apvts.getParameter (
            id::oscSlot (0, id::osc::coarse))->getValue();
        bool boundsOk = true, coarseOk = true;
        for (int roll = 0; roll < 30; ++roll)
        {
            proc.randomizeAll();
            boundsOk = boundsOk
                    && apvts.getParameter (id::filter1Resonance)->getValue() <= 0.86f;
            coarseOk = coarseOk
                    && juce::approximatelyEqual (
                           apvts.getParameter (id::oscSlot (0, id::osc::coarse))->getValue(),
                           coarseBefore);
        }
        expect (boundsOk, "randomization respects per-param constrained ranges");
        expect (coarseOk, "coarse tune is excluded from randomization");

        // Locks: filter section untouched when locked.
        proc.setLockGroupLocked ((int) params::LockGroup::filter, true);
        const auto cutoffBefore = apvts.getParameter (id::filter1Cutoff)->getValue();
        const auto typeBefore = apvts.getParameter (id::filter1Type)->getValue();
        for (int roll = 0; roll < 5; ++roll)
            proc.randomizeAll();
        expect (juce::approximatelyEqual (
                    apvts.getParameter (id::filter1Cutoff)->getValue(), cutoffBefore)
                && juce::approximatelyEqual (
                    apvts.getParameter (id::filter1Type)->getValue(), typeBefore),
                "locked filter section survives re-rolls");
        proc.setLockGroupLocked ((int) params::LockGroup::filter, false);

        // No-sample slots never land in sample/granular mode (any synthesis
        // engine - wavetable/analog/FM/noise/pluck - is fine).
        bool modesOk = true;
        for (int roll = 0; roll < 10; ++roll)
        {
            proc.randomizeAll();
            for (int s = 0; s < params::numOscSlots; ++s)
            {
                auto* param = apvts.getParameter (id::oscSlot (s, id::osc::mode));
                const auto mode = (params::OscMode) (int) param->convertFrom0to1 (param->getValue());
                modesOk = modesOk
                       && mode != params::OscMode::sample
                       && mode != params::OscMode::granular;
            }
        }
        expect (modesOk, "sample-less slots avoid sample/granular modes");
    }

    static void randomizerProducesSoundTest()
    {
        std::cout << "randomizerProducesSoundTest\n";

        namespace params = spa::params;

        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 512;

        spa::SPASynthProcessor proc;
        proc.prepareToPlay (sampleRate, blockSize);

        // The matrix can legitimately duck levels (that's its job); lock it so
        // this test isolates the "every re-roll makes sound" guarantee.
        proc.setLockGroupLocked ((int) params::LockGroup::matrix, true);

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;

        int audible = 0;
        constexpr int rolls = 8;
        for (int roll = 0; roll < rolls; ++roll)
        {
            proc.randomizeAll();
            // ~1.7s window: slow-attack rolls (legal, musical) need time to
            // speak before the audibility check.
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            const auto peak = renderBlocks (proc, buffer, midi, 160);
            midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
            renderBlocks (proc, buffer, midi, 90);
            if (peak > 0.003f)
                ++audible;
        }

        expect (audible == rolls, "every re-roll produces an audible patch ("
                                  + juce::String (audible) + "/" + juce::String (rolls) + ")");
    }

    // RANDOMIZE ALL's headphone-safety guards (gain-budget trim + limiter
    // forced on) are invariants over any roll, so this iterates many rolls
    // rather than checking a single one - RNG-robust.
    static void randomizeLoudnessGuardTest()
    {
        std::cout << "randomizeLoudnessGuardTest\n";

        namespace params = spa::params;
        namespace id = spa::params::id;

        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 512;

        spa::SPASynthProcessor proc;
        proc.prepareToPlay (sampleRate, blockSize);
        auto& apvts = proc.getAPVTS();

        float worstSum = 0.0f;
        bool limiterAlwaysOn = true;
        constexpr int rolls = 30;
        for (int roll = 0; roll < rolls; ++roll)
        {
            proc.randomizeAll();

            float gainSum = 0.0f;
            for (int s = 0; s < params::numOscSlots; ++s)
            {
                auto* enableParam = apvts.getParameter (id::oscSlot (s, id::osc::enable));
                if (enableParam->getValue() < 0.5f)
                    continue;
                auto* levelParam = apvts.getParameter (id::oscSlot (s, id::osc::level));
                const auto levelDb = levelParam->convertFrom0to1 (levelParam->getValue());
                gainSum += juce::Decibels::decibelsToGain (levelDb, -60.0f);
            }
            worstSum = std::max (worstSum, gainSum);

            const auto limiterOn = *apvts.getRawParameterValue (id::fx::limEnable) >= 0.5f;
            limiterAlwaysOn = limiterAlwaysOn && limiterOn;
        }

        expect (worstSum <= 1.25f + 1.0e-3f,
                "gain-budget guard holds over " + juce::String (rolls)
                    + " rolls (worst sum " + juce::String (worstSum, 4) + ")");
        expect (limiterAlwaysOn,
                "limiter is always on after a re-roll (safety ceiling)");
    }

    // Builds a throwaway library: two packs with tiny WAVs.
    static juce::File makeFakeLibrary()
    {
        const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getNonexistentChildFile ("spasynth-lib-test", "");
        for (auto* pack : { "Alpha Pack", "Beta Pack" })
            for (auto* wav : { "one.wav", "two.wav", "three.wav" })
            {
                juce::AudioBuffer<float> buffer (1, 4800);
                for (int i = 0; i < 4800; ++i)
                    buffer.setSample (0, i, 0.5f * (float) std::sin (
                        juce::MathConstants<double>::twoPi * 220.0 * i / 48000.0));

                const auto file = root.getChildFile (pack).getChildFile (wav);
                file.getParentDirectory().createDirectory();
                juce::WavAudioFormat fmt;
                std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
                if (auto writer = fmt.createWriterFor (stream,
                        juce::AudioFormatWriterOptions().withSampleRate (48000.0)
                            .withNumChannels (1).withBitsPerSample (24)))
                    writer->writeFromAudioSampleBuffer (buffer, 0, 4800);
            }
        return root;
    }

    static void libraryScanTest()
    {
        std::cout << "libraryScanTest\n";

        namespace lib = spa::library;

        const auto root = makeFakeLibrary();
        const auto packs = lib::scanLibrary (root);

        expect (packs.size() == 2, "scan finds two pack categories");
        expect (! packs.empty() && packs[0].name == "Alpha Pack"
                && packs[0].wavs.size() == 3,
                "pack folder maps to category with its wavs");

        // Portable path round-trip.
        const auto wav = packs[0].wavs.getFirst();
        const auto portable = lib::toPortable (wav, root);
        expect (portable.startsWith ("$LIB$"), "library paths serialize portably");
        expect (lib::fromPortable (portable, root) == wav, "portable path resolves back");

        root.deleteRecursively();
    }

    static void libraryDiscoveryTest()
    {
        std::cout << "libraryDiscoveryTest\n";

        namespace lib = spa::library;

        const auto realLib = makeFakeLibrary();
        const auto emptyDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                  .getNonexistentChildFile ("spasynth-empty", "");
        emptyDir.createDirectory();
        const auto missing = juce::File ("/nonexistent/spasynth-lib");

        expect (lib::looksLikeLibrary (realLib), "pack folders identify a library");
        expect (! lib::looksLikeLibrary (emptyDir), "empty folder is not a library");
        expect (! lib::looksLikeLibrary (missing), "missing folder is not a library");

        // Discovery skips invalid candidates and lands on the first real one.
        expect (lib::discoverLibrary ({ missing, emptyDir, realLib }) == realLib,
                "discovery finds the library among standard locations");
        expect (lib::discoverLibrary ({ missing, emptyDir }) == juce::File(),
                "discovery reports nothing when no candidate is valid");

        // Candidate expansion: a company dir holding a non-canonically named
        // library (starter library dragged out of its zip, renamed folder)
        // is still discovered — but the canonical name wins when present.
        const auto companyDir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                    .getNonexistentChildFile ("spasynth-company", "");
        const auto starter = companyDir.getChildFile ("SPASynth Starter Library");
        const auto starterPack = starter.getChildFile ("Some Pack");
        starterPack.createDirectory();
        starterPack.getChildFile ("a.wav").replaceWithData ("x", 1);

        auto expanded = lib::expandLibraryCandidates ({ companyDir }, "SPASynth Library");
        expect (! expanded.empty()
                    && expanded.front() == companyDir.getChildFile ("SPASynth Library"),
                "canonical library name is the first candidate");
        expect (lib::discoverLibrary (expanded) == starter,
                "renamed library inside a company dir is discovered as fallback");

        const auto canonical = companyDir.getChildFile ("SPASynth Library");
        const auto canonicalPack = canonical.getChildFile ("Real Pack");
        canonicalPack.createDirectory();
        canonicalPack.getChildFile ("b.wav").replaceWithData ("x", 1);
        expect (lib::discoverLibrary (lib::expandLibraryCandidates ({ companyDir },
                                                                    "SPASynth Library"))
                    == canonical,
                "canonical library outranks fallback folders");

        companyDir.deleteRecursively();
        realLib.deleteRecursively();
        emptyDir.deleteRecursively();
    }

    // WAV files sitting directly in a library root (no pack subfolder) form
    // a pack of their own, named after the root folder itself -- covers a
    // customer pointing SPASynth at a plain folder of samples.
    static void looseWavLibraryTest()
    {
        std::cout << "looseWavLibraryTest\n";

        namespace lib = spa::library;

        const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getNonexistentChildFile ("spasynth-loose-lib", "");
        root.createDirectory();
        for (auto* wav : { "kick.wav", "snare.wav" })
            root.getChildFile (wav).replaceWithData ("x", 1);

        auto packs = lib::scanLibrary (root);
        expect (packs.size() == 1, "loose WAVs in the root form one pack");
        expect (! packs.empty() && packs[0].name == root.getFileName(),
                "synthetic root pack is named after the root folder");
        expect (! packs.empty() && packs[0].wavs.size() == 2,
                "synthetic root pack picks up both loose WAVs");
        expect (lib::looksLikeLibrary (root),
                "looksLikeLibrary agrees with scanLibrary for loose WAVs");

        // A real pack subfolder alongside the loose files -- both must count,
        // with neither double-counting the other's WAVs.
        const auto packDir = root.getChildFile ("Extra Pack");
        packDir.createDirectory();
        packDir.getChildFile ("tom.wav").replaceWithData ("x", 1);

        packs = lib::scanLibrary (root);
        expect (packs.size() == 2, "loose WAVs and a pack subfolder both count, no overlap");

        int rootPackWavs = -1, extraPackWavs = -1;
        for (const auto& p : packs)
        {
            if (p.name == root.getFileName())
                rootPackWavs = p.wavs.size();
            else if (p.name == "Extra Pack")
                extraPackWavs = p.wavs.size();
        }
        expect (rootPackWavs == 2, "root pack keeps exactly its 2 loose WAVs (not double counted)");
        expect (extraPackWavs == 1, "subfolder pack keeps exactly its own WAV");

        root.deleteRecursively();
    }

    // findLibraryRoot() must never discard a user-chosen root just because it
    // currently has zero packs -- that was the actual reported bug (a
    // freshly-picked folder getting silently overwritten with a rediscovered
    // default, with no message shown).
    static void libraryRootPersistsWhenEmptyTest()
    {
        std::cout << "libraryRootPersistsWhenEmptyTest\n";

        namespace lib = spa::library;

        const auto savedRoot = lib::getLibraryRoot();   // restore machine setting after

        const auto emptyRoot = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                   .getNonexistentChildFile ("spasynth-empty-configured", "");
        emptyRoot.createDirectory();
        lib::setLibraryRoot (emptyRoot);

        expect (lib::findLibraryRoot() == emptyRoot,
                "findLibraryRoot returns a user-chosen root even with zero packs");
        expect (lib::getLibraryRoot() == emptyRoot,
                "the configured setting is left untouched, not silently rediscovered");

        emptyRoot.deleteRecursively();
        lib::setLibraryRoot (savedRoot);
    }

    // Regression test for a tester-reported bug (v1.0.8): clicking a preset
    // in the browser produced a short burst of noise even though nothing was
    // playing (there is no preview/audition feature). Root cause: a preset
    // click routinely lands while the previous note's voice is still in its
    // release tail and/or the FX chain (delay/reverb/mod) still holds ringing
    // feedback state; restoreStateTree()'s apvts.replaceState() swaps every
    // coefficient-driving parameter out from under that live, non-zero state
    // in one shot -- e.g. the FDN reverb's feedback matrix recomputed for a
    // totally different size/decay while its delay lines still held the old
    // preset's tail -- and a coefficient jump against non-zero history
    // produces an audible click/burst. Fixed by having restoreStateTree() do
    // the same hard reset panic() performs (kill all voices, clear the arp
    // latch, flush the FX chain's stateful buffers), synchronously inside the
    // getCallbackLock() already held for replaceState(), so the very next
    // block sees new parameters applied to already-silent state.
    static void presetLoadNoiseBurstTest()
    {
        std::cout << "presetLoadNoiseBurstTest\n";

        namespace params = spa::params;
        namespace id = spa::params::id;
        namespace lib = spa::library;

        constexpr double sr = 48000.0;
        constexpr int n = 512;
        constexpr float silentPeak = 1.0e-4f;

        spa::SPASynthProcessor proc;
        proc.prepareToPlay (sr, n);

        const auto presetsRoot = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                     .getNonexistentChildFile ("spasynth-burst-presets", "");
        lib::PresetManager pm ([&] { return proc.buildStateTree(); },
                               [&] (const juce::ValueTree& t) { proc.restoreStateTree (t); },
                               presetsRoot);

        // Two presets with hot, very different delay/reverb/mod/filter settings
        // -- the kind of jump a real preset browsing session produces.
        auto configureA = [&]
        {
            setParam (proc, id::fx::delayEnable, 1.0f);
            setParam (proc, id::fx::delaySync, 0.0f);
            setParam (proc, id::fx::delayTime, 220.0f);
            setParam (proc, id::fx::delayFeedback, 0.55f);
            setParam (proc, id::fx::delayMix, 0.5f);
            setParam (proc, id::fx::reverbEnable, 1.0f);
            setParam (proc, id::fx::reverbMode, 0.0f);
            setParam (proc, id::fx::reverbDecay, 2.5f);
            setParam (proc, id::fx::reverbSize, 0.6f);
            setParam (proc, id::fx::reverbMix, 0.4f);
            setParam (proc, id::fx::modEnable, 1.0f);
            setParam (proc, id::fx::modType, 1.0f);
            setParam (proc, id::fx::modRate, 0.3f);
            setParam (proc, id::fx::modFeedback, 0.85f);
            setParam (proc, id::fx::modMix, 0.6f);
            setParam (proc, id::filter1Cutoff, 3000.0f);
            setParam (proc, id::filter1Resonance, 0.6f);
        };
        auto configureB = [&]
        {
            setParam (proc, id::fx::delayEnable, 1.0f);
            setParam (proc, id::fx::delaySync, 0.0f);
            setParam (proc, id::fx::delayTime, 380.0f);
            setParam (proc, id::fx::delayFeedback, 0.7f);
            setParam (proc, id::fx::delayMix, 0.35f);
            setParam (proc, id::fx::reverbEnable, 1.0f);
            setParam (proc, id::fx::reverbMode, 2.0f);
            setParam (proc, id::fx::reverbDecay, 6.0f);
            setParam (proc, id::fx::reverbSize, 0.9f);
            setParam (proc, id::fx::reverbMix, 0.7f);
            setParam (proc, id::fx::modEnable, 1.0f);
            setParam (proc, id::fx::modType, 0.0f);
            setParam (proc, id::fx::modRate, 1.4f);
            setParam (proc, id::fx::modFeedback, 0.2f);
            setParam (proc, id::fx::modMix, 0.3f);
            setParam (proc, id::filter1Cutoff, 900.0f);
            setParam (proc, id::filter1Resonance, 0.85f);
        };

        configureA();
        expect (pm.saveUserPreset ("BurstA"), "preset A saves");
        configureB();
        expect (pm.saveUserPreset ("BurstB"), "preset B saves");
        pm.rescan();

        juce::File fileA, fileB;
        for (const auto& p : pm.getPresets())
        {
            if (p.name == "BurstA") fileA = p.file;
            if (p.name == "BurstB") fileB = p.file;
        }
        expect (fileA.existsAsFile() && fileB.existsAsFile(), "both burst presets on disk");

        juce::AudioBuffer<float> buf (2, n);
        juce::MidiBuffer midi;

        auto peakOverSilentBlocks = [&] (int blocks)
        {
            float peak = 0.0f;
            for (int b = 0; b < blocks; ++b)
            {
                buf.clear();
                proc.processBlock (buf, midi);
                peak = juce::jmax (peak, buf.getMagnitude (0, n));
            }
            return peak;
        };

        // A short note + release, leaving the voice mid-release and the FX
        // chain's feedback lines still hot when the preset switch lands.
        auto playNoteAndReleaseSome = [&]
        {
            midi.clear();
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            for (int b = 0; b < 12; ++b) { buf.clear(); proc.processBlock (buf, midi); midi.clear(); }
            midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
            for (int b = 0; b < 4; ++b) { buf.clear(); proc.processBlock (buf, midi); midi.clear(); }
        };

        // Cold transitions: never played, nothing to ring out.
        pm.loadPresetFile (fileA);
        expect (peakOverSilentBlocks (50) < silentPeak, "cold A load stays silent");
        pm.loadPresetFile (fileB);
        expect (peakOverSilentBlocks (50) < silentPeak, "cold A->B stays silent");

        // Hot-tail transitions: this is the tester's actual repro -- a voice
        // still releasing and FX feedback still ringing when the click lands.
        playNoteAndReleaseSome();
        pm.loadPresetFile (fileA);
        expect (peakOverSilentBlocks (50) < silentPeak,
                "hot-tail B->A stays silent (no burst on preset click)");
        playNoteAndReleaseSome();
        pm.loadPresetFile (fileB);
        expect (peakOverSilentBlocks (50) < silentPeak,
                "hot-tail A->B stays silent (no burst on preset click)");

        // Loading the SAME preset twice in a row while hot must also stay
        // silent (not just a difference-in-parameters case).
        playNoteAndReleaseSome();
        pm.loadPresetFile (fileB);
        peakOverSilentBlocks (5);
        pm.loadPresetFile (fileB);
        expect (peakOverSilentBlocks (50) < silentPeak, "hot same-preset reload stays silent");

        // Async sample/wavetable/convolution-IR loads pending vs settled must
        // not matter either -- measure both before and after pumping the
        // message loop.
        playNoteAndReleaseSome();
        pm.loadPresetFile (fileA);
        expect (peakOverSilentBlocks (10) < silentPeak,
                "hot reload stays silent before pending async loads settle");
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        expect (peakOverSilentBlocks (50) < silentPeak,
                "hot reload stays silent after pending async loads settle");

        presetsRoot.deleteRecursively();
    }

    static void presetRoundTripTest()
    {
        std::cout << "presetRoundTripTest\n";

        namespace params = spa::params;
        namespace id = spa::params::id;
        namespace lib = spa::library;

        spa::SPASynthProcessor proc;
        proc.prepareToPlay (48000.0, 512);

        const auto presetsRoot = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                     .getNonexistentChildFile ("spasynth-presets-test", "");
        lib::PresetManager pm ([&] { return proc.buildStateTree(); },
                               [&] (const juce::ValueTree& t) { proc.restoreStateTree (t); },
                               presetsRoot);

        setParam (proc, id::filter1Cutoff, 1234.0f);
        setParam (proc, id::oscSlot (0, id::osc::position), 0.42f);
        expect (pm.saveUserPreset ("RoundTrip"), "user preset saves");

        setParam (proc, id::filter1Cutoff, 20000.0f);
        setParam (proc, id::oscSlot (0, id::osc::position), 0.0f);

        pm.rescan();
        bool loaded = false;
        for (size_t i = 0; i < pm.getPresets().size(); ++i)
            if (pm.getPresets()[i].name == "RoundTrip")
                loaded = pm.loadPreset ((int) i);
        expect (loaded, "user preset loads back");

        const auto cutoff = proc.getAPVTS().getParameter (id::filter1Cutoff)
                                ->convertFrom0to1 (proc.getAPVTS()
                                    .getParameter (id::filter1Cutoff)->getValue());
        expect (std::abs (cutoff - 1234.0f) < 5.0f,
                "params restore from preset (cutoff " + juce::String (cutoff) + ")");
        expect (pm.getCurrentName() == "RoundTrip", "current preset name tracks");

        presetsRoot.deleteRecursively();
    }

    // User preset banks (subfolders of User/): rescan groups them by folder
    // name, saveUserPreset honors a chosen bank folder (or falls back to the
    // User root if the chosen folder is outside User/ entirely), and the
    // isUser flag -- not category == "User" -- is what marks a preset as a
    // user preset, so a bank preset still counts as one.
    static void presetBankTest()
    {
        std::cout << "presetBankTest\n";

        namespace lib = spa::library;

        spa::SPASynthProcessor proc;
        proc.prepareToPlay (48000.0, 512);

        const auto presetsRoot = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                     .getNonexistentChildFile ("spasynth-bank-test", "");
        lib::PresetManager pm ([&] { return proc.buildStateTree(); },
                               [&] (const juce::ValueTree& t) { proc.restoreStateTree (t); },
                               presetsRoot);

        const auto userRoot = pm.getUserPresetFolder();
        const auto bankFolder = userRoot.getChildFile ("Leads");
        bankFolder.createDirectory();
        bankFolder.getChildFile ("x" + juce::String (lib::PresetManager::presetExtension))
            .replaceWithText ("placeholder");
        userRoot.getChildFile ("y" + juce::String (lib::PresetManager::presetExtension))
            .replaceWithText ("placeholder");

        pm.rescan();

        const lib::PresetManager::PresetInfo* bankPreset = nullptr;
        const lib::PresetManager::PresetInfo* rootPreset = nullptr;
        for (const auto& p : pm.getPresets())
        {
            if (p.name == "x") bankPreset = &p;
            if (p.name == "y") rootPreset = &p;
        }
        expect (bankPreset != nullptr && rootPreset != nullptr, "both presets found on rescan");
        if (bankPreset != nullptr)
        {
            expect (bankPreset->category == "Leads", "bank preset category is the bank folder name");
            expect (bankPreset->isUser, "bank preset is flagged as a user preset");
        }
        if (rootPreset != nullptr)
        {
            expect (rootPreset->category == "User", "root-level user preset keeps category \"User\"");
            expect (rootPreset->isUser, "root-level user preset is flagged as a user preset");
        }

        const auto categories = pm.getCategories();
        expect (categories.contains ("Leads") && categories.contains ("User"),
                "both \"Leads\" and \"User\" appear as categories");

        // Saving into a bank subfolder chosen in the save dialog (as if the
        // user had picked it, or just created it via "New Folder").
        expect (pm.saveUserPreset ("Saved In Bank", bankFolder),
                "saves into a chosen bank folder");
        expect (bankFolder.getChildFile ("Saved In Bank" + juce::String (lib::PresetManager::presetExtension))
                    .existsAsFile(),
                "preset file lands inside the chosen bank folder, not the User root");

        // A folder outside User/ entirely must fall back to the User root
        // rather than writing somewhere the browser will never scan.
        const auto outsideFolder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                       .getNonexistentChildFile ("spasynth-bank-outside", "");
        outsideFolder.createDirectory();
        expect (pm.saveUserPreset ("Saved Outside", outsideFolder),
                "still saves successfully when the chosen folder is outside User/");
        expect (! outsideFolder.getChildFile ("Saved Outside"
                        + juce::String (lib::PresetManager::presetExtension)).existsAsFile(),
                "does not write into the folder outside User/");
        expect (userRoot.getChildFile ("Saved Outside" + juce::String (lib::PresetManager::presetExtension))
                    .existsAsFile(),
                "falls back to the User root instead");

        outsideFolder.deleteRecursively();
        presetsRoot.deleteRecursively();
    }

    // A hand-edited or damaged .spasynth file must fail to load cleanly
    // rather than crash: valid XML with the right root tag but no child
    // state element, and outright XML garbage.
    static void malformedPresetTest()
    {
        std::cout << "malformedPresetTest\n";

        namespace lib = spa::library;

        spa::SPASynthProcessor proc;
        proc.prepareToPlay (48000.0, 512);

        const auto presetsRoot = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                     .getNonexistentChildFile ("spasynth-malformed-test", "");
        lib::PresetManager pm ([&] { return proc.buildStateTree(); },
                               [&] (const juce::ValueTree& t) { proc.restoreStateTree (t); },
                               presetsRoot);

        const auto emptyRoot = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                   .getNonexistentChildFile ("spasynth-malformed-empty", ".spasynth");
        emptyRoot.replaceWithText ("<SPASynthPreset name=\"x\"/>");
        expect (! pm.loadPresetFile (emptyRoot),
                "root tag with no child state element fails to load, no crash");

        const auto garbage = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                 .getNonexistentChildFile ("spasynth-malformed-garbage", ".spasynth");
        garbage.replaceWithText ("this is not <xml at all >>> {{{ garbage");
        expect (! pm.loadPresetFile (garbage),
                "invalid XML fails to load, no crash");

        emptyRoot.deleteFile();
        garbage.deleteFile();
        presetsRoot.deleteRecursively();
    }

    // "Reset to Default" (the menu item added post-1.0.3) must restore every
    // parameter to its ParameterRegistry default and clear the current-preset
    // name back to "Init".
    static void presetResetToDefaultTest()
    {
        std::cout << "presetResetToDefaultTest\n";

        namespace id = spa::params::id;

        spa::SPASynthProcessor proc;
        proc.prepareToPlay (48000.0, 512);

        auto& apvts = proc.getAPVTS();
        auto* cutoffParam = apvts.getParameter (id::filter1Cutoff);
        auto* gainParam = apvts.getParameter (id::masterGain);
        const auto defaultCutoff = cutoffParam->convertFrom0to1 (cutoffParam->getDefaultValue());
        const auto defaultGain = gainParam->convertFrom0to1 (gainParam->getDefaultValue());

        // filter1Cutoff defaults fully open (20 kHz); move it well down instead
        // of up so the range clamp doesn't silently leave it unchanged.
        setParam (proc, id::filter1Cutoff, defaultCutoff - 15000.0f);
        setParam (proc, id::masterGain, defaultGain - 6.0f);
        setParam (proc, id::oscSlot (0, id::osc::position), 0.9f);

        proc.getPresetManager().resetToDefault();

        const auto cutoffAfter = cutoffParam->convertFrom0to1 (cutoffParam->getValue());
        const auto gainAfter = gainParam->convertFrom0to1 (gainParam->getValue());
        expect (std::abs (cutoffAfter - defaultCutoff) < 1.0f,
                "filter1Cutoff back at registry default after reset ("
                + juce::String (cutoffAfter) + " vs " + juce::String (defaultCutoff) + ")");
        expect (std::abs (gainAfter - defaultGain) < 0.01f,
                "masterGain back at registry default after reset ("
                + juce::String (gainAfter) + " vs " + juce::String (defaultGain) + ")");
        expect (proc.getPresetManager().getCurrentName() == "Init",
                "current preset name resets to \"Init\"");
    }

    static void factoryPresetGenerationTest()
    {
        std::cout << "factoryPresetGenerationTest\n";

        namespace params = spa::params;
        namespace id = spa::params::id;
        namespace lib = spa::library;

        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 512;

        spa::SPASynthProcessor proc;
        proc.prepareToPlay (sampleRate, blockSize);

        const auto libRoot = makeFakeLibrary();
        const auto presetsRoot = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                     .getNonexistentChildFile ("spasynth-factory-test", "");

        lib::PresetManager pm ([&] { return proc.buildStateTree(); },
                               [&] (const juce::ValueTree& t) { proc.restoreStateTree (t); },
                               presetsRoot);

        const auto packs = lib::scanLibrary (libRoot);
        const auto written = pm.generateFactoryPresets (packs, libRoot);
        expect (written == 6, "3 factory presets per pack ("
                              + juce::String (written) + " written)");
        expect (pm.getCategories().size() == 2, "one preset category per pack");

        // Load a "Keys" preset end-to-end: sample mode engages and the
        // library sample actually loads (portable path resolution works).
        bool foundKeys = false;
        for (size_t i = 0; i < pm.getPresets().size(); ++i)
        {
            if (pm.getPresets()[i].name == "Alpha Pack Keys")
            {
                foundKeys = pm.loadPreset ((int) i);
                break;
            }
        }
        expect (foundKeys, "factory Keys preset loads");

        // Portable-path lambda in restoreStateTree resolves against the
        // *configured* library root; for the test, resolve manually instead:
        // the preset stores $LIB$ paths, so with no configured root the load
        // lands nowhere. Verify the stored path is portable and resolvable.
        const auto presetFile = presetsRoot.getChildFile ("Factory")
                                    .getChildFile ("Alpha Pack")
                                    .findChildFiles (juce::File::findFiles, false,
                                                     "*Keys*").getFirst();
        const auto xml = juce::XmlDocument::parse (presetFile);
        expect (xml != nullptr, "factory preset file parses as XML");
        if (xml != nullptr)
        {
            const auto state = juce::ValueTree::fromXml (*xml->getFirstChildElement());
            const auto stored = state.getChildWithName ("SAMPLES")
                                     .getProperty ("slot0").toString();
            expect (stored.startsWith ("$LIB$"), "factory preset stores portable path");
            expect (lib::fromPortable (stored, libRoot).existsAsFile(),
                    "portable path resolves to a real library file");
        }

        // The mode parameter came through the preset.
        const auto mode = (int) proc.getAPVTS().getParameter (
            id::oscSlot (0, id::osc::mode))->convertFrom0to1 (
                proc.getAPVTS().getParameter (id::oscSlot (0, id::osc::mode))->getValue());
        expect (mode == (int) params::OscMode::sample, "Keys preset sets sample mode");

        libRoot.deleteRecursively();
        presetsRoot.deleteRecursively();
    }

    // Same as factoryPresetGenerationTest, but for a library root with loose
    // WAVs directly inside it and no pack subfolder at all -- the synthetic
    // root pack must generate real, loadable presets whose portable paths
    // resolve to files sitting directly under the library root.
    static void factoryPresetRootPackTest()
    {
        std::cout << "factoryPresetRootPackTest\n";

        namespace params = spa::params;
        namespace id = spa::params::id;
        namespace lib = spa::library;

        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 512;

        spa::SPASynthProcessor proc;
        proc.prepareToPlay (sampleRate, blockSize);

        const auto libRoot = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                 .getNonexistentChildFile ("spasynth-rootpack-test", "");
        libRoot.createDirectory();
        for (auto* wav : { "one.wav", "two.wav", "three.wav" })
        {
            juce::AudioBuffer<float> buffer (1, 4800);
            for (int i = 0; i < 4800; ++i)
                buffer.setSample (0, i, 0.5f * (float) std::sin (
                    juce::MathConstants<double>::twoPi * 220.0 * i / 48000.0));

            const auto file = libRoot.getChildFile (wav);
            juce::WavAudioFormat fmt;
            std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
            if (auto writer = fmt.createWriterFor (stream,
                    juce::AudioFormatWriterOptions().withSampleRate (48000.0)
                        .withNumChannels (1).withBitsPerSample (24)))
                writer->writeFromAudioSampleBuffer (buffer, 0, 4800);
        }

        const auto presetsRoot = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                     .getNonexistentChildFile ("spasynth-rootpack-presets-test", "");

        lib::PresetManager pm ([&] { return proc.buildStateTree(); },
                               [&] (const juce::ValueTree& t) { proc.restoreStateTree (t); },
                               presetsRoot);

        const auto packs = lib::scanLibrary (libRoot);
        expect (packs.size() == 1, "loose-WAV root scans to a single synthetic pack");

        const auto written = pm.generateFactoryPresets (packs, libRoot);
        expect (written == 3, "3 factory presets generated from the root pack ("
                              + juce::String (written) + " written)");
        expect (pm.getCategories().size() == 1,
                "one preset category, named after the root folder");

        const auto expectedName = libRoot.getFileName() + " Keys";
        bool foundKeys = false;
        for (size_t i = 0; i < pm.getPresets().size(); ++i)
        {
            if (pm.getPresets()[i].name == expectedName)
            {
                foundKeys = pm.loadPreset ((int) i);
                break;
            }
        }
        expect (foundKeys, "factory Keys preset generated from the root pack loads");

        const auto categoryDir = presetsRoot.getChildFile ("Factory")
                                     .getChildFile (libRoot.getFileName());
        expect (categoryDir.isDirectory(),
                "category folder uses the root folder's own name");
        const auto presetFile = categoryDir.findChildFiles (juce::File::findFiles, false,
                                                             "*Keys*").getFirst();
        const auto xml = juce::XmlDocument::parse (presetFile);
        expect (xml != nullptr, "root-pack factory preset file parses as XML");
        if (xml != nullptr)
        {
            const auto state = juce::ValueTree::fromXml (*xml->getFirstChildElement());
            const auto stored = state.getChildWithName ("SAMPLES")
                                     .getProperty ("slot0").toString();
            expect (stored.startsWith ("$LIB$"), "root-pack preset stores a portable path");
            expect (lib::fromPortable (stored, libRoot).existsAsFile(),
                    "portable path resolves to a real file directly under the library root");
        }

        libRoot.deleteRecursively();
        presetsRoot.deleteRecursively();
    }

    // Renders the editor offscreen for visual review: SPASynthTests --snapshot <dir>
    static void renderEditorSnapshots (const juce::File& outDir)
    {
        spa::SPASynthProcessor proc;
        proc.prepareToPlay (48000.0, 512);

        // Reproduce the long-content-name case in osc A's header.
        {
            namespace id = spa::params::id;
            const auto longName = juce::File::getSpecialLocation (juce::File::tempDirectory)
                .getChildFile ("1965 Brother Typewriter Platen Knob Turn Long Name 01_SP.wav");
            const auto src = writeRampSine (0.5, 48000.0);
            src.copyFileTo (longName);
            src.deleteFile();
            proc.loadSampleFromFile (0, longName);
            waitForSample (proc, 0, 15000);
            setParam (proc, id::oscSlot (0, id::osc::mode),
                      (float) (int) spa::params::OscMode::sample);

            // Non-default start/loop points so every render below actually
            // shows the loop-point overlay (defaults are start=0, loop 0..1,
            // which would just band the whole waveform edge-to-edge). loop
            // itself defaults on already.
            setParam (proc, id::oscSlot (0, id::osc::sampleStart), 0.05f);
            setParam (proc, id::oscSlot (0, id::osc::loopStart), 0.2f);
            setParam (proc, id::oscSlot (0, id::osc::loopEnd), 0.8f);
        }

        // Remember the user's accents so the custom-accent render below
        // doesn't pollute their settings.
        const auto savedAccent = spa::ui::currentTheme().accent;
        const auto savedAccentMod = spa::ui::currentTheme().accentMod;

        for (const bool customAccents : { false, true })
        {
            // Second pass: violet/lime accents to verify the colour picker's
            // reach across the whole UI.
            spa::ui::setAccentColors (customAccents ? juce::Colour (0xffa06cf0) : savedAccent,
                                      customAccents ? juce::Colour (0xff8fd14f) : savedAccentMod);

            std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
            editor->setSize (spa::ui::metrics::baseWidth, spa::ui::metrics::baseHeight);

            // Default pass: front the wrap-heavy tabs and the preset drawer
            // so they get visual review; accent pass shows the plain grid.
            if (! customAccents)
            {
                std::function<void (juce::Component&)> frontExtras =
                    [&] (juce::Component& c)
                {
                    if (auto* tabs = dynamic_cast<juce::TabbedComponent*> (&c))
                    {
                        // Front by name: tabs can be reordered, so a fixed index
                        // would front whatever module now sits in that slot.
                        if (tabs->getTabNames().contains ("DELAY"))
                            tabs->setCurrentTabIndex (tabs->getTabNames().indexOf ("DELAY"));
                        if (tabs->getTabNames().contains ("FILTER 2"))
                            tabs->setCurrentTabIndex (1);
                    }
                    if (auto* browser = dynamic_cast<spa::ui::PresetBrowser*> (&c))
                        browser->openImmediately();
                    for (auto* child : c.getChildren())
                        frontExtras (*child);
                };
                frontExtras (*editor);
            }

            const auto image = editor->createComponentSnapshot (editor->getLocalBounds());
            const auto file = outDir.getChildFile (customAccents ? "spasynth-accent.png"
                                                                 : "spasynth-dark.png");
            file.deleteFile();
            juce::PNGImageFormat png;
            juce::FileOutputStream stream (file);
            if (stream.openedOk())
                png.writeImageToStream (image, stream);
            std::cout << "snapshot: " << file.getFullPathName() << "\n";
        }

        spa::ui::setAccentColors (savedAccent, savedAccentMod);

        // Third pass: the sample-loading state. Deterministic because the
        // pending-load decrement is queued behind the message loop, which
        // this render never pumps — the overlay is guaranteed on screen.
        {
            const auto src = writeRampSine (0.5, 48000.0);
            proc.loadSampleFromFile (0, src);

            std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
            editor->setSize (spa::ui::metrics::baseWidth, spa::ui::metrics::baseHeight);

            const auto image = editor->createComponentSnapshot (editor->getLocalBounds());
            const auto file = outDir.getChildFile ("spasynth-loading.png");
            file.deleteFile();
            juce::PNGImageFormat png;
            juce::FileOutputStream stream (file);
            if (stream.openedOk())
                png.writeImageToStream (image, stream);
            std::cout << "snapshot: " << file.getFullPathName() << "\n";

            waitForSample (proc, 0, 15000);   // let the load land before teardown
            src.deleteFile();
        }

        // Marketing pass: the shot for the website. Full synth visible (no
        // drawer), a presentable sample name in osc A's header, rendered at
        // 2x for retina displays. Accents come from the machine settings
        // like every render — regenerate from a defaults machine state.
        {
            const auto niceName = juce::File::getSpecialLocation (juce::File::tempDirectory)
                .getChildFile ("Glass Marimba Hit 03_SPAudio.wav");
            const auto src = writeRampSine (0.5, 48000.0);
            src.copyFileTo (niceName);
            src.deleteFile();
            proc.loadSampleFromFile (0, niceName);
            // waitForSample() is satisfied by the PREVIOUS pass's sample, so
            // wait on the in-flight flag — otherwise this render captures the
            // loading overlay instead of the marimba waveform.
            const auto deadline = juce::Time::getMillisecondCounter() + 15000u;
            while (proc.isSampleLoading (0)
                   && juce::Time::getMillisecondCounter() < deadline)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (10);

            std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
            editor->setSize (spa::ui::metrics::baseWidth, spa::ui::metrics::baseHeight);

            const auto image = editor->createComponentSnapshot (
                editor->getLocalBounds(), true, 2.0f);
            const auto file = outDir.getChildFile ("spasynth-marketing.png");
            file.deleteFile();
            juce::PNGImageFormat png;
            juce::FileOutputStream stream (file);
            if (stream.openedOk())
                png.writeImageToStream (image, stream);
            std::cout << "snapshot: " << file.getFullPathName() << "\n";

            niceName.deleteFile();
        }

        // Keyboard strip shown (settings menu -> Show Keyboard): verify the
        // on-screen keyboard and the taller base height layout.
        {
            proc.getAPVTS().state.setProperty ("uiKeyboardVisible", true, nullptr);
            std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
            editor->setSize (spa::ui::metrics::baseWidth,
                             spa::ui::metrics::baseHeight + spa::ui::metrics::keyboardStripHeight);

            const auto image = editor->createComponentSnapshot (editor->getLocalBounds());
            const auto file = outDir.getChildFile ("spasynth-keyboard.png");
            file.deleteFile();
            juce::PNGImageFormat png;
            juce::FileOutputStream stream (file);
            if (stream.openedOk())
                png.writeImageToStream (image, stream);
            std::cout << "snapshot: " << file.getFullPathName() << "\n";
            proc.getAPVTS().state.setProperty ("uiKeyboardVisible", false, nullptr);
        }

        // ORGANIC CHAOS live-trace pass: enable chaos, hold a note through
        // ~1s of real processing so the telemetry trace ring actually has a
        // seismograph to show, then snapshot with the note still held so
        // isLive() is true and the trace draws in full accent colour instead
        // of the dimmed idle state.
        {
            namespace id = spa::params::id;
            setParam (proc, id::chaos::enable, 1.0f);
            setParam (proc, id::chaos::depth, 1.0f);
            // 1.2 Hz reads as a clean slow-drift demo image. (Telemetry's
            // trace ring now writes every mod chunk, undecimated, so an 8 Hz+
            // rate -- which testers will actually use -- also renders as a
            // smooth slope rather than a cliff; see scratchpad renders
            // snap-chaos-live3 (this seed) vs snap-chaos-live3-fast (8 Hz).)
            setParam (proc, id::chaos::rate, 1.2f);

            constexpr double sampleRate = 48000.0;
            constexpr int blockSize = 512;
            juce::AudioBuffer<float> buffer (2, blockSize);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            // 3 seconds so the trace ring (~2.7s wide) fills the well's full
            // width for this demo render, rather than the first-real-second
            // partial fill from an idle start.
            for (int b = 0; b < (int) (3.0 * sampleRate / blockSize); ++b)
            {
                proc.processBlock (buffer, midi);
                midi.clear();
            }

            std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
            editor->setSize (spa::ui::metrics::baseWidth, spa::ui::metrics::baseHeight);

            const auto image = editor->createComponentSnapshot (editor->getLocalBounds());
            const auto file = outDir.getChildFile ("spasynth-chaos.png");
            file.deleteFile();
            juce::PNGImageFormat png;
            juce::FileOutputStream stream (file);
            if (stream.openedOk())
                png.writeImageToStream (image, stream);
            std::cout << "snapshot: " << file.getFullPathName() << "\n";

            juce::MidiBuffer allOff;
            allOff.addEvent (juce::MidiMessage::allNotesOff (1), 0);
            proc.processBlock (buffer, allOff);
        }
    }

    // The preset-name button must be clickable the moment the editor opens —
    // regression probe for the "Init unclickable until a knob moves" bug.
    static void editorHitTestProbe()
    {
        std::cout << "editorHitTestProbe\n";

        spa::SPASynthProcessor proc;
        proc.prepareToPlay (48000.0, 512);

        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
        editor->setVisible (true);   // hosts do this when attaching the view
        editor->setSize (spa::ui::metrics::baseWidth, spa::ui::metrics::baseHeight);

        juce::TextButton* presetButton = nullptr;
        std::function<void (juce::Component&)> find = [&] (juce::Component& c)
        {
            if (auto* b = dynamic_cast<juce::TextButton*> (&c))
                if (b->getTooltip() == "Browse presets")
                    presetButton = b;
            for (auto* child : c.getChildren())
                find (*child);
        };
        find (*editor);
        expect (presetButton != nullptr, "preset name button found");
        if (presetButton == nullptr)
            return;

        auto probe = [&] (const char* when)
        {
            const auto centre = editor->getLocalPoint (presetButton,
                presetButton->getLocalBounds().getCentre().toFloat());
            auto* hit = editor->getComponentAt (centre.roundToInt());
            const bool ok = hit == presetButton;
            if (! ok)
            {
                std::cout << "  probe point in editor: " << centre.toString()
                          << "  editor bounds: " << editor->getBounds().toString() << "\n"
                          << "  button bounds (parent-rel): " << presetButton->getBounds().toString()
                          << " visible=" << (int) presetButton->isVisible() << "\n";
                for (auto* p = presetButton->getParentComponent(); p != nullptr;
                     p = p->getParentComponent())
                    std::cout << "  ancestor: " << typeid (*p).name()
                              << " bounds=" << p->getBounds().toString()
                              << " visible=" << (int) p->isVisible() << "\n";
                if (hit != nullptr)
                    std::cout << "  blocked by: " << typeid (*hit).name()
                              << " name='" << hit->getName() << "'"
                              << " bounds=" << hit->getBounds().toString() << "\n";
                else
                    std::cout << "  hit: (none)\n";
            }
            expect (ok, juce::String ("preset button is hit-testable ") + when);
        };

        probe ("right after construction");

        // Give deferred work (async library refresh, timers) a chance to run,
        // then re-probe — the bug showed up only before the first param nudge.
        juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
        probe ("after the message loop has run");
    }

    static void midiLearnTest()
    {
        std::cout << "midiLearnTest\n";

        namespace id = spa::params::id;

        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 512;

        spa::SPASynthProcessor proc;
        proc.prepareToPlay (sampleRate, blockSize);
        auto& learn = proc.getMidiLearn();
        auto* cutoff = proc.getAPVTS().getParameter (id::filter1Cutoff);

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;

        // Arm learn on cutoff; the first CC heard (74) captures the binding.
        learn.armLearn (id::filter1Cutoff);
        expect (learn.isArmed(), "learn arms for a parameter");

        midi.addEvent (juce::MidiMessage::controllerEvent (1, 74, 64), 0);
        proc.processBlock (buffer, midi);
        midi.clear();

        expect (! learn.isArmed(), "first CC captures the binding");
        expect (learn.getAssignedCC (id::filter1Cutoff) == 74,
                "CC 74 assigned to cutoff");

        // Mapped CC moves the parameter.
        midi.addEvent (juce::MidiMessage::controllerEvent (1, 74, 0), 0);
        proc.processBlock (buffer, midi);
        midi.clear();
        expect (cutoff->getValue() < 0.01f, "CC value 0 slams cutoff to min");

        midi.addEvent (juce::MidiMessage::controllerEvent (1, 74, 127), 0);
        proc.processBlock (buffer, midi);
        midi.clear();
        expect (cutoff->getValue() > 0.99f, "CC value 127 opens cutoff fully");

        // Mapping survives a host save/restore round-trip...
        const auto sessionState = proc.buildStateTree (true);
        learn.clearAll();
        expect (learn.getAssignedCC (id::filter1Cutoff) == -1, "clearAll clears");
        proc.restoreStateTree (sessionState);
        expect (learn.getAssignedCC (id::filter1Cutoff) == 74,
                "MIDI map restores with the session");

        // ...but presets don't carry (or clobber) it.
        const auto presetState = proc.buildStateTree (false);
        expect (! presetState.getChildWithName (
                    spa::MidiLearnManager::mapTreeType).isValid(),
                "preset state excludes the MIDI map");
        proc.restoreStateTree (presetState);
        expect (learn.getAssignedCC (id::filter1Cutoff) == 74,
                "loading a preset keeps hardware mappings");
    }

    static void arpeggiatorTest()
    {
        std::cout << "arpeggiatorTest\n";

        namespace params = spa::params;
        using Arp = spa::dsp::Arpeggiator;

        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 512;

        Arp arp;
        arp.prepare (sampleRate);

        Arp::Params p;
        p.enable = true;
        p.mode = params::ArpMode::up;
        p.division = 12;      // 1/16 @ 120bpm = 125ms = 6000 samples
        p.gate = 0.5f;
        p.sampleRate = sampleRate;

        // Hold C-E-G; run one second; collect events.
        std::vector<int> ons;
        int offs = 0;

        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        midi.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100), 0);
        midi.addEvent (juce::MidiMessage::noteOn (1, 67, (juce::uint8) 100), 0);

        for (int block = 0; block < (int) (sampleRate / blockSize); ++block)
        {
            arp.process (midi, blockSize, p);
            for (const auto metadata : midi)
            {
                if (metadata.getMessage().isNoteOn())
                    ons.push_back (metadata.getMessage().getNoteNumber());
                else if (metadata.getMessage().isNoteOff())
                    ++offs;
            }
            midi.clear();
        }

        // 1s at 125ms/step = 8 steps (first at t=0).
        expect (ons.size() >= 7 && ons.size() <= 9,
                "up mode: ~8 steps per second (" + juce::String ((int) ons.size()) + ")");
        expect (offs >= (int) ons.size() - 1, "gated note-offs follow note-ons");

        bool cycleOk = ons.size() >= 6;
        const int expected[3] = { 60, 64, 67 };
        for (size_t i = 0; i < juce::jmin ((size_t) 6, ons.size()); ++i)
            cycleOk = cycleOk && ons[i] == expected[i % 3];
        expect (cycleOk, "up mode cycles C-E-G in pitch order");

        // Swing regression: heavy swing delays every 2nd step but must not DROP
        // any. A swung step whose un-swung beat sits just before a block boundary
        // used to be skipped, so swing lost ~half the notes.
        {
            Arp sw;
            sw.prepare (sampleRate);
            Arp::Params sp = p;            // up, 1/16, gate 0.5, not latched
            sp.swing = 0.5f;
            juce::MidiBuffer m;
            m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            m.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100), 0);
            m.addEvent (juce::MidiMessage::noteOn (1, 67, (juce::uint8) 100), 0);
            int swOns = 0;
            for (int block = 0; block < (int) (sampleRate / blockSize); ++block)
            {
                sw.process (m, blockSize, sp);
                for (const auto md : m)
                    if (md.getMessage().isNoteOn()) ++swOns;
                m.clear();
            }
            expect (swOns >= 7 && swOns <= 9,
                    "swing keeps ~8 steps/sec, none dropped (" + juce::String (swOns) + ")");
        }

        // Latch: release all keys, arp keeps stepping.
        p.latch = true;
        midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
        midi.addEvent (juce::MidiMessage::noteOff (1, 64), 0);
        midi.addEvent (juce::MidiMessage::noteOff (1, 67), 0);
        int latchedOns = 0;
        for (int block = 0; block < (int) (0.5 * sampleRate / blockSize); ++block)
        {
            arp.process (midi, blockSize, p);
            for (const auto metadata : midi)
                if (metadata.getMessage().isNoteOn())
                    ++latchedOns;
            midi.clear();
        }
        expect (latchedOns >= 3, "latch keeps arping after keys release ("
                                 + juce::String (latchedOns) + ")");

        // Phrase mode: only the lowest held note seeds the pattern; check
        // emitted pitches match the phrase intervals from C4.
        Arp arp2;
        arp2.prepare (sampleRate);
        p = {};
        p.enable = true;
        p.mode = params::ArpMode::phrase;
        p.phrase = 2;         // "Fifths": 0, 7, 12, 7
        p.division = 12;
        p.sampleRate = sampleRate;

        std::vector<int> phraseNotes;
        midi.addEvent (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100), 0);
        for (int block = 0; block < (int) (sampleRate / blockSize); ++block)
        {
            arp2.process (midi, blockSize, p);
            for (const auto metadata : midi)
                if (metadata.getMessage().isNoteOn())
                    phraseNotes.push_back (metadata.getMessage().getNoteNumber());
            midi.clear();
        }

        const int phraseExpected[4] = { 48, 55, 60, 55 };
        bool phraseOk = phraseNotes.size() >= 4;
        for (size_t i = 0; i < juce::jmin ((size_t) 8, phraseNotes.size()); ++i)
            phraseOk = phraseOk && phraseNotes[i] == phraseExpected[i % 4];
        expect (phraseOk, "phrase mode plays root/fifth/octave pattern from C3");

        // Disable mid-run: pass-through resumes and actives get released.
        p.enable = false;
        midi.addEvent (juce::MidiMessage::noteOn (1, 72, (juce::uint8) 90), 10);
        arp2.process (midi, blockSize, p);
        bool sawPassThrough = false;
        for (const auto metadata : midi)
            if (metadata.getMessage().isNoteOn()
                && metadata.getMessage().getNoteNumber() == 72)
                sawPassThrough = true;
        expect (sawPassThrough, "disabled arp passes MIDI through");
    }

    // Full chain: arp on, hold a key through several steps, release it, then let
    // it ring out. Every voice must free itself (no stuck notes).
    static void arpStuckNoteTest()
    {
        std::cout << "arpStuckNoteTest\n";
        namespace id = spa::params::id;
        constexpr double sr = 48000.0;
        constexpr int block = 256;

        spa::SPASynthProcessor proc;
        proc.prepareToPlay (sr, block);
        setParam (proc, id::arp::enable, 1.0f);
        setParam (proc, id::arp::division, 12.0f);   // 1/16

        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer onMsg;
        onMsg.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 110), 0);
        for (int b = 0; b < 60; ++b)                 // hold through several steps
        {
            buf.clear();
            juce::MidiBuffer m = (b == 0 ? onMsg : juce::MidiBuffer());
            proc.processBlock (buf, m);
        }

        juce::MidiBuffer offMsg;
        offMsg.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
        buf.clear();
        proc.processBlock (buf, offMsg);
        for (int b = 0; b < 400; ++b)                // let releases finish (~2 s)
        {
            buf.clear();
            juce::MidiBuffer m;
            proc.processBlock (buf, m);
        }

        expect (proc.getTelemetry().activeVoices.load() == 0,
                "arp: no stuck notes after release ("
                + juce::String (proc.getTelemetry().activeVoices.load()) + " active)");

        // The arp must actually run on the internal clock (no host): the held key
        // should have produced sound while it was down.
        float held = 0.0f;
        {
            spa::SPASynthProcessor p2;
            p2.prepareToPlay (sr, block);
            setParam (p2, id::arp::enable, 1.0f);
            setParam (p2, id::arp::division, 12.0f);
            juce::AudioBuffer<float> b2 (2, block);
            juce::MidiBuffer on2; on2.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 110), 0);
            for (int b = 0; b < 80; ++b)
            {
                b2.clear();
                juce::MidiBuffer m = (b == 0 ? on2 : juce::MidiBuffer());
                p2.processBlock (b2, m);
                held = juce::jmax (held, b2.getMagnitude (0, 0, block));
            }
            expect (held > 0.01f, "arp runs on the internal clock (audible while held)");
        }
    }

    // Some hosts send zero-sample "flush" blocks (e.g. around latency
    // compensation or transport edits). The arp's step-scan loop used to
    // assume numSamples > 0; regression coverage for the numSamples <= 0
    // early-out in Arpeggiator::process.
    static void arpZeroSampleBlockTest()
    {
        std::cout << "arpZeroSampleBlockTest\n";
        namespace id = spa::params::id;
        constexpr double sr = 48000.0;
        constexpr int block = 256;

        spa::SPASynthProcessor proc;
        proc.prepareToPlay (sr, block);
        setParam (proc, id::arp::enable, 1.0f);
        setParam (proc, id::arp::division, 12.0f);   // 1/16

        juce::AudioBuffer<float> buf (2, block);
        juce::AudioBuffer<float> zeroBuf (2, 0);
        juce::MidiBuffer onMsg;
        onMsg.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 110), 0);

        juce::MidiBuffer m = onMsg;
        proc.processBlock (buf, m);

        // Interleave zero-sample blocks with normal ones; should never crash.
        for (int b = 0; b < 20; ++b)
        {
            juce::MidiBuffer empty;
            proc.processBlock (zeroBuf, empty);

            buf.clear();
            juce::MidiBuffer empty2;
            proc.processBlock (buf, empty2);
        }

        expect (true, "zero-sample blocks interleaved with normal blocks did not crash");

        // Normal processing should still be alive afterwards (arp still
        // stepping, not wedged by the zero-sample interruptions).
        float peak = 0.0f;
        for (int b = 0; b < 20; ++b)
        {
            buf.clear();
            juce::MidiBuffer empty;
            proc.processBlock (buf, empty);
            peak = juce::jmax (peak, buf.getMagnitude (0, 0, block));
        }
        expect (peak > 0.01f, "arp keeps producing sound after zero-sample blocks");
    }

    // A host reporting a non-finite ppq (e.g. mid tempo-map edit, corrupt
    // session) used to hang the arp's beat-clock step-scan forever, since
    // NaN comparisons never satisfy the loop's exit condition. Regression
    // coverage for the std::isfinite guard in Arpeggiator::process; the
    // pass/fail signal here is simply that processing returns at all
    // (a regression here means CI hangs rather than reporting FAIL).
    static void arpNonFinitePpqTest()
    {
        std::cout << "arpNonFinitePpqTest\n";
        namespace id = spa::params::id;
        constexpr double sr = 48000.0;
        constexpr int block = 256;

        struct NanPpqPlayHead : public juce::AudioPlayHead
        {
            juce::Optional<PositionInfo> getPosition() const override
            {
                PositionInfo info;
                info.setBpm (120.0);
                info.setIsPlaying (true);
                info.setPpqPosition (std::numeric_limits<double>::quiet_NaN());
                return info;
            }
        };

        spa::SPASynthProcessor proc;
        proc.prepareToPlay (sr, block);
        setParam (proc, id::arp::enable, 1.0f);
        setParam (proc, id::arp::division, 12.0f);

        NanPpqPlayHead nanPlayHead;
        proc.setPlayHead (&nanPlayHead);

        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer onMsg;
        onMsg.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 110), 0);

        float peak = 0.0f;
        for (int b = 0; b < 40; ++b)
        {
            buf.clear();
            juce::MidiBuffer m = (b == 0 ? onMsg : juce::MidiBuffer());
            proc.processBlock (buf, m);
            peak = juce::jmax (peak, buf.getMagnitude (0, 0, block));
        }

        expect (true, "processing returned with a NaN-ppq playhead (no hang)");
        expect (peak > 0.01f, "arp falls back to the internal clock and still sounds");

        proc.setPlayHead (nullptr);
    }

    // Logic reports NEGATIVE ppq during a record count-in (and pre-roll before
    // bar 1). The arp syncs stepCounter to that ppq on transport start, and a
    // negative counter fed through C++ `%` indexed the pattern arrays with a
    // negative subscript: an out-of-bounds stack read that segfaulted Logic's
    // render thread the moment a second track was recorded (1.0.13, 2026-09-07).
    // Every mode must play only the held pitches (any octave) from a count-in.
    static void arpNegativePpqTest()
    {
        std::cout << "arpNegativePpqTest\n";
        namespace params = spa::params;
        using Arp = spa::dsp::Arpeggiator;

        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 256;
        constexpr int numModes = (int) params::ArpMode::phrase + 1;

        for (int modeIndex = 0; modeIndex < numModes; ++modeIndex)
        {
            const auto mode = (params::ArpMode) modeIndex;
            const bool isPhrase = mode == params::ArpMode::phrase;

            Arp arp;
            arp.prepare (sampleRate);

            Arp::Params p;
            p.enable = true;
            p.mode = mode;
            p.division = 12;       // 1/16 @ 120bpm = 0.25 beats/step
            p.octaves = 2;         // span > held count so wrapping matters
            p.velocityMode = 2;    // accent mode also takes stepCounter % len
            p.gate = 0.5f;
            p.sampleRate = sampleRate;
            p.bpm = 120.0;
            p.hostPlaying = true;

            const double samplesPerBeat = sampleRate * 60.0 / p.bpm;
            const double blockBeats = blockSize / samplesPerBeat;
            double ppq = -8.0;   // two-bar count-in at 4/4

            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            midi.addEvent (juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100), 0);
            midi.addEvent (juce::MidiMessage::noteOn (1, 67, (juce::uint8) 100), 0);

            int onsBeforeBarOne = 0, onsTotal = 0;
            bool pitchesOk = true;
            for (; ppq < 2.0; ppq += blockBeats)
            {
                p.ppqAtBlockStart = ppq;
                arp.process (midi, blockSize, p);
                for (const auto metadata : midi)
                {
                    const auto m = metadata.getMessage();
                    if (! m.isNoteOn())
                        continue;
                    ++onsTotal;
                    if (ppq < 0.0)
                        ++onsBeforeBarOne;
                    const auto note = m.getNoteNumber();
                    const auto pc = note % 12;
                    const bool ok = isPhrase ? (note >= 60 && note <= 127)
                                             : (pc == 0 || pc == 4 || pc == 7);
                    pitchesOk = pitchesOk && ok;
                }
                midi.clear();
            }

            const auto tag = "mode " + juce::String (modeIndex);
            expect (onsBeforeBarOne > 0, tag + ": arp runs during the count-in");
            expect (onsTotal > onsBeforeBarOne, tag + ": arp keeps running past bar 1");
            expect (pitchesOk, tag + ": only held pitches are played from a negative ppq");
        }
    }

    static void arpChanceTest()
    {
        std::cout << "arpChanceTest\n";

        namespace params = spa::params;
        using Arp = spa::dsp::Arpeggiator;

        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 512;

        struct Hit { int note; int velocity; };

        // Runs `seconds` of a held C4 through a fresh arp; returns note-ons.
        // division 12 = 1/16 @ 120bpm = 8 steps per second.
        const auto run = [&] (Arp::Params p, double seconds)
        {
            Arp arp;
            arp.prepare (sampleRate);
            p.enable = true;
            p.division = 12;
            p.sampleRate = sampleRate;

            std::vector<Hit> hits;
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            for (int block = 0; block < (int) (seconds * sampleRate / blockSize); ++block)
            {
                arp.process (midi, blockSize, p);
                for (const auto metadata : midi)
                    if (metadata.getMessage().isNoteOn())
                        hits.push_back ({ metadata.getMessage().getNoteNumber(),
                                          (int) metadata.getMessage().getVelocity() });
                midi.clear();
            }
            return hits;
        };

        {
            Arp::Params p;
            const auto hits = run (p, 1.0);
            bool clean = ! hits.empty();
            for (const auto& h : hits)
                clean = clean && h.note == 60 && h.velocity == 100;
            expect (clean, "defaults leave the pattern untouched ("
                           + juce::String ((int) hits.size()) + " hits)");
        }

        {
            Arp::Params p;
            p.chance = 0.0f;
            expect (run (p, 1.0).empty(), "chance 0 rests every step");
        }

        {
            Arp::Params p;
            p.chance = 0.5f;
            const auto n = (int) run (p, 4.0).size();   // 32 steps
            expect (n >= 5 && n <= 27,
                    "chance 0.5 fires some steps, rests others (" + juce::String (n) + "/32)");
        }

        {
            Arp::Params p;
            p.stutter = 1.0f;
            const auto hits = run (p, 2.0);              // 16 steps -> 32..64 hits
            bool samePitch = true;
            for (const auto& h : hits)
                samePitch = samePitch && h.note == 60;
            expect ((int) hits.size() >= 30,
                    "stutter ratchets every step into repeats ("
                    + juce::String ((int) hits.size()) + " hits from 16 steps)");
            expect (samePitch, "ratchet repeats keep the step's pitch");
        }

        {
            Arp::Params p;
            p.jump = 1.0f;
            const auto hits = run (p, 2.0);
            bool octaves = ! hits.empty();
            for (const auto& h : hits)
                octaves = octaves && (h.note == 48 || h.note == 72);
            expect (octaves, "jump 1 lands an octave up or down every step");
        }

        {
            Arp::Params p;
            p.humanize = 1.0f;
            const auto hits = run (p, 2.0);
            int minVel = 127, maxVel = 1;
            for (const auto& h : hits)
            {
                minVel = juce::jmin (minVel, h.velocity);
                maxVel = juce::jmax (maxVel, h.velocity);
            }
            expect (maxVel - minVel >= 10,
                    "humanize spreads velocities (" + juce::String (minVel)
                    + ".." + juce::String (maxVel) + ")");
        }
    }

    static void extraEnginesTest()
    {
        std::cout << "extraEnginesTest\n";

        namespace params = spa::params;
        namespace id = spa::params::id;

        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 512;

        const auto renderWith = [&] (params::OscMode mode,
                                     std::function<void (spa::SPASynthProcessor&)> configure,
                                     int note, int blocks, juce::AudioBuffer<float>& capture)
        {
            spa::SPASynthProcessor proc;
            proc.prepareToPlay (sampleRate, blockSize);
            setParam (proc, id::chaos::enable, 0.0f);
            setParam (proc, id::oscSlot (0, id::osc::mode), (float) (int) mode);
            if (configure)
                configure (proc);

            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0);
            float peak = 0.0f;
            for (int b = 0; b < blocks; ++b)
            {
                proc.processBlock (capture, midi);
                midi.clear();
                peak = juce::jmax (peak, capture.getMagnitude (0, blockSize));
            }
            return peak;
        };

        juce::AudioBuffer<float> buffer (2, blockSize);

        // Analog saw at A3: audible and at the right pitch (zero crossings).
        {
            const auto peak = renderWith (params::OscMode::analog, {}, 57, 24, buffer);
            expect (peak > 0.05f, "analog saw is audible");

            int crossings = 0;
            for (int i = 1; i < blockSize; ++i)
                if ((buffer.getSample (0, i - 1) < 0.0f) != (buffer.getSample (0, i) < 0.0f))
                    ++crossings;
            const auto freq = (float) crossings * (float) sampleRate / (2.0f * blockSize);
            expect (freq > 200.0f && freq < 240.0f,
                    "analog saw tracks pitch (" + juce::String (freq) + " Hz, expect ~220)");
        }

        // FM: raising the index adds sidebands (HF metric grows).
        {
            const auto hfOf = [&] (float index)
            {
                renderWith (params::OscMode::fm, [index] (auto& proc)
                {
                    setParam (proc, id::oscSlot (0, id::osc::fmIndex), index);
                }, 57, 24, buffer);
                float hf = 0.0f;
                for (int i = 1; i < blockSize; ++i)
                    hf += std::abs (buffer.getSample (0, i) - buffer.getSample (0, i - 1));
                return hf;
            };
            const auto clean = hfOf (0.0f);
            const auto driven = hfOf (8.0f);
            expect (driven > clean * 1.5f,
                    "FM index adds sidebands (idx0 " + juce::String (clean)
                    + " vs idx8 " + juce::String (driven) + ")");
        }

        // Noise: audible, aperiodic-ish (no dominant zero-crossing regularity
        // check needed - just assert output).
        {
            const auto peak = renderWith (params::OscMode::noise, {}, 57, 12, buffer);
            expect (peak > 0.05f, "noise engine is audible");
        }

        // Pluck: strikes then decays while the key is held.
        {
            spa::SPASynthProcessor proc;
            proc.prepareToPlay (sampleRate, blockSize);
            setParam (proc, id::chaos::enable, 0.0f);
            setParam (proc, id::oscSlot (0, id::osc::mode),
                      (float) (int) params::OscMode::pluck);
            setParam (proc, id::oscSlot (0, id::osc::pluckDamp), 0.3f);

            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 69, (juce::uint8) 100), 0);
            float early = 0.0f, late = 0.0f;
            for (int b = 0; b < 90; ++b)
            {
                proc.processBlock (buffer, midi);
                midi.clear();
                const auto peak = buffer.getMagnitude (0, blockSize);
                if (b < 8)
                    early = juce::jmax (early, peak);
                if (b >= 70)
                    late = juce::jmax (late, peak);
            }
            expect (early > 0.05f, "pluck strikes audibly");
            expect (late < early * 0.5f,
                    "pluck decays while held (early " + juce::String (early)
                    + " vs late " + juce::String (late) + ")");
        }
    }

    // Pluck engine buffers are allocated lazily (SPASynthVoice::
    // ensurePluckAllocated, triggered from SPASynthProcessor::
    // parameterChanged() when the osc-mode param is set to pluck). Switching
    // a slot to Pluck and striking a note immediately afterwards -- no
    // message-loop pumping beyond setValueNotifyingHost's own synchronous
    // listener dispatch -- must not race the allocation and produce silence
    // (or worse, an unallocated-buffer misbehaviour).
    static void pluckLazyAllocTest()
    {
        std::cout << "pluckLazyAllocTest\n";

        namespace params = spa::params;
        namespace id = spa::params::id;

        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 512;

        spa::SPASynthProcessor proc;
        proc.prepareToPlay (sampleRate, blockSize);
        setParam (proc, id::chaos::enable, 0.0f);

        // setValueNotifyingHost() dispatches to
        // AudioProcessorValueTreeState::Listener::parameterChanged()
        // synchronously (see the ctor/parameterChanged comment in
        // SPASynthProcessor.cpp), so the lazy Pluck allocation has already
        // happened by the time this call returns -- no callAsync/message-
        // loop pump needed before striking the note.
        setParam (proc, id::oscSlot (0, id::osc::mode), (float) (int) params::OscMode::pluck);

        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 69, (juce::uint8) 100), 0);

        float early = 0.0f;
        for (int b = 0; b < 8; ++b)
        {
            proc.processBlock (buffer, midi);
            midi.clear();
            early = juce::jmax (early, buffer.getMagnitude (0, blockSize));
        }
        expect (early > 0.05f,
                "pluck strikes audibly right after the mode switch, no allocation race ("
                + juce::String (early) + ")");
    }

    static void filterExtrasTest()
    {
        std::cout << "filterExtrasTest\n";

        namespace id = spa::params::id;

        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 512;

        const auto brightness = [&] (std::function<void (spa::SPASynthProcessor&)> configure,
                                     int note)
        {
            spa::SPASynthProcessor proc;
            proc.prepareToPlay (sampleRate, blockSize);
            setParam (proc, id::chaos::enable, 0.0f);
            setParam (proc, id::oscSlot (0, id::osc::position), 0.66f);  // saw-ish
            setParam (proc, id::filter1Type, 1.0f);                     // LP 24
            setParam (proc, id::filter1Cutoff, 300.0f);
            if (configure)
                configure (proc);

            juce::AudioBuffer<float> buffer (2, blockSize);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0);
            for (int b = 0; b < 20; ++b)
            {
                proc.processBlock (buffer, midi);
                midi.clear();
            }
            float hf = 0.0f;
            for (int i = 1; i < blockSize; ++i)
                hf += std::abs (buffer.getSample (0, i) - buffer.getSample (0, i - 1));
            return hf / (float) blockSize;
        };

        // Keytracking: with full tracking, a high note opens the filter.
        const auto highNoTrack = brightness ({}, 96);
        const auto highTracked = brightness ([] (auto& proc)
        {
            setParam (proc, id::filter1Keytrack, 1.0f);
        }, 96);
        expect (highTracked > highNoTrack * 1.5f,
                "keytracking opens cutoff for high notes (untracked "
                + juce::String (highNoTrack) + " vs tracked "
                + juce::String (highTracked) + ")");

        // Env amount: positive env2 depth brightens the sustain phase.
        const auto noEnv = brightness ({}, 48);
        const auto withEnv = brightness ([] (auto& proc)
        {
            setParam (proc, id::filter1EnvAmount, 1.0f);
        }, 48);
        expect (withEnv > noEnv * 1.5f,
                "env amount opens the filter (dry " + juce::String (noEnv)
                + " vs env " + juce::String (withEnv) + ")");

        // Mix 0 bypasses the filter entirely.
        const auto filtered = brightness ({}, 60);
        const auto bypassed = brightness ([] (auto& proc)
        {
            setParam (proc, id::filter1Mix, 0.0f);
        }, 60);
        expect (bypassed > filtered * 2.0f,
                "mix 0 bypasses the LP filter (filtered " + juce::String (filtered)
                + " vs bypassed " + juce::String (bypassed) + ")");
    }

    static void dualFilterTest()
    {
        std::cout << "dualFilterTest\n";

        namespace id = spa::params::id;

        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 512;

        // F1 = LP 300 Hz, F2 = HP 3 kHz. In series the pass-bands are
        // disjoint -> near silence. In parallel both bands pass -> loud.
        const auto peakWith = [&] (bool f2On, bool parallel)
        {
            spa::SPASynthProcessor proc;
            proc.prepareToPlay (sampleRate, blockSize);
            setParam (proc, id::chaos::enable, 0.0f);
            setParam (proc, id::oscSlot (0, id::osc::position), 0.66f);  // saw-ish
            setParam (proc, id::filter1Type, 1.0f);       // LP 24
            setParam (proc, id::filter1Cutoff, 300.0f);
            setParam (proc, id::filter2Enable, f2On ? 1.0f : 0.0f);
            setParam (proc, id::filter2Type, 3.0f);       // HP 24
            setParam (proc, id::filter2Cutoff, 3000.0f);
            setParam (proc, id::filterRouting, parallel ? 1.0f : 0.0f);

            juce::AudioBuffer<float> buffer (2, blockSize);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            float peak = 0.0f;
            for (int b = 0; b < 24; ++b)
            {
                proc.processBlock (buffer, midi);
                midi.clear();
                if (b >= 8)
                    peak = juce::jmax (peak, buffer.getMagnitude (0, blockSize));
            }
            return peak;
        };

        const auto single = peakWith (false, false);
        const auto series = peakWith (true, false);
        const auto parallel = peakWith (true, true);

        expect (single > 0.02f, "single filter baseline is audible");
        expect (series < single * 0.25f,
                "series LP300->HP3k gates the signal (single " + juce::String (single)
                + " vs series " + juce::String (series) + ")");
        expect (parallel > series * 3.0f,
                "parallel routing passes both bands (series " + juce::String (series)
                + " vs parallel " + juce::String (parallel) + ")");
        expect (peakWith (true, false) <= series * 1.5f,
                "series result is repeatable");
    }

    // Filter1's ON switch must genuinely bypass the filter (distinct from
    // mix=0, which fades the filtered signal itself). Mirrors the
    // filterExtrasTest/dualFilterTest brightness harness.
    static void filter1EnableTest()
    {
        std::cout << "filter1EnableTest\n";

        namespace id = spa::params::id;

        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 512;

        const auto brightness = [&] (bool filterOn)
        {
            spa::SPASynthProcessor proc;
            proc.prepareToPlay (sampleRate, blockSize);
            setParam (proc, id::chaos::enable, 0.0f);
            setParam (proc, id::oscSlot (0, id::osc::position), 0.66f);  // saw-ish, bright
            setParam (proc, id::filter1Type, 1.0f);                     // LP 24
            setParam (proc, id::filter1Cutoff, 300.0f);
            setParam (proc, id::filter1Mix, 1.0f);                      // fully wet either way
            setParam (proc, id::filter1Enable, filterOn ? 1.0f : 0.0f);

            juce::AudioBuffer<float> buffer (2, blockSize);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
            for (int b = 0; b < 20; ++b)
            {
                proc.processBlock (buffer, midi);
                midi.clear();
            }
            float hf = 0.0f;
            for (int i = 1; i < blockSize; ++i)
                hf += std::abs (buffer.getSample (0, i) - buffer.getSample (0, i - 1));
            return hf / (float) blockSize;
        };

        const auto on = brightness (true);
        const auto off = brightness (false);
        expect (off > on * 1.5f,
                "filter1 OFF is audibly brighter than ON at mix=1 (on " + juce::String (on)
                + " vs off " + juce::String (off) + ")");
    }

    // Fundamental frequency estimate via positive-going zero crossings.
    static float zeroCrossingHz (const juce::AudioBuffer<float>& capture,
                                 int start, int len, double sampleRate)
    {
        const auto* d = capture.getReadPointer (0);
        int crossings = 0;
        for (int i = start + 1; i < start + len; ++i)
            if (d[i - 1] < 0.0f && d[i] >= 0.0f)
                ++crossings;
        return (float) ((double) crossings * sampleRate / (double) len);
    }

    static void glideTest()
    {
        std::cout << "glideTest\n";

        namespace id = spa::params::id;
        constexpr double sampleRate = 48000.0;
        constexpr int blockSize = 512;

        // Renders numBlocks into one long capture buffer; queued MIDI fires in
        // the first block.
        const auto capture = [] (spa::SPASynthProcessor& proc, juce::MidiBuffer& midi,
                                 int numBlocks)
        {
            juce::AudioBuffer<float> block (2, blockSize);
            juce::AudioBuffer<float> out (1, numBlocks * blockSize);
            for (int b = 0; b < numBlocks; ++b)
            {
                proc.processBlock (block, midi);
                midi.clear();
                out.copyFrom (0, b * blockSize, block, 0, 0, blockSize);
            }
            return out;
        };

        const auto hz = [] (float note) { return 440.0f * std::exp2 ((note - 69.0f) / 12.0f); };
        const auto loHz = hz (48.0f);   // ~130.8
        const auto hiHz = hz (72.0f);   // ~523.3

        const auto makeProc = [] (float mode, float timeMs)
        {
            auto proc = std::make_unique<spa::SPASynthProcessor>();
            proc->prepareToPlay (sampleRate, blockSize);
            setParam (*proc, id::glideMode, mode);
            setParam (*proc, id::glideTime, timeMs);
            setParam (*proc, id::ampRelease, 0.02f);
            return proc;
        };

        // --- Always: the new note ramps in from the previous one -------------
        {
            auto proc = makeProc (1.0f, 600.0f);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100), 0);
            capture (*proc, midi, 24);

            midi.addEvent (juce::MidiMessage::noteOff (1, 48), 0);
            midi.addEvent (juce::MidiMessage::noteOn (1, 72, (juce::uint8) 100), 0);
            const auto out = capture (*proc, midi, 100);

            const auto early = zeroCrossingHz (out, 2400, 4800, sampleRate);   // 50-150 ms
            const auto late = zeroCrossingHz (out, out.getNumSamples() - 14400,
                                              14400, sampleRate);              // last 300 ms
            expect (early > 0.8f * loHz && early < 0.6f * hiHz,
                    "Always glides through intermediate pitch (early "
                    + juce::String (early) + " Hz)");
            expect (std::abs (late - hiHz) < 0.05f * hiHz,
                    "glide lands on the target (late " + juce::String (late) + " Hz)");
        }

        // --- Off: the new note jumps straight to pitch -----------------------
        {
            auto proc = makeProc (0.0f, 600.0f);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100), 0);
            capture (*proc, midi, 24);

            midi.addEvent (juce::MidiMessage::noteOff (1, 48), 0);
            midi.addEvent (juce::MidiMessage::noteOn (1, 72, (juce::uint8) 100), 0);
            const auto out = capture (*proc, midi, 30);

            const auto early = zeroCrossingHz (out, 2400, 4800, sampleRate);
            expect (std::abs (early - hiHz) < 0.08f * hiHz,
                    "Off jumps straight to the target (early "
                    + juce::String (early) + " Hz)");
        }

        // --- Legato: detached notes do not glide -----------------------------
        {
            auto proc = makeProc (2.0f, 800.0f);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100), 0);
            capture (*proc, midi, 12);
            midi.addEvent (juce::MidiMessage::noteOff (1, 48), 0);
            capture (*proc, midi, 12);   // fully released before the next note

            midi.addEvent (juce::MidiMessage::noteOn (1, 72, (juce::uint8) 100), 0);
            const auto out = capture (*proc, midi, 30);

            const auto early = zeroCrossingHz (out, 2400, 4800, sampleRate);
            expect (std::abs (early - hiHz) < 0.08f * hiHz,
                    "Legato does not glide after a released key (early "
                    + juce::String (early) + " Hz)");
        }

        // --- Legato: overlapping notes glide ----------------------------------
        {
            auto proc = makeProc (2.0f, 800.0f);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100), 0);
            capture (*proc, midi, 24);

            midi.addEvent (juce::MidiMessage::noteOn (1, 72, (juce::uint8) 100), 0);
            midi.addEvent (juce::MidiMessage::noteOff (1, 48), 32);   // released just after
            const auto out = capture (*proc, midi, 30);

            const auto early = zeroCrossingHz (out, 4800, 4800, sampleRate);   // 100-200 ms
            expect (early > 0.8f * loHz && early < 0.5f * hiHz,
                    "Legato glides while the previous key overlaps (early "
                    + juce::String (early) + " Hz)");
        }
    }

    static void licenseLineTest()
    {
        std::cout << "licenseLineTest\n";

        const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getChildFile ("spasynth-license-test.txt");

        expect (spa::library::licenseLineFromFile (file).isEmpty(),
                "missing license file yields empty");

        file.replaceWithText ("\n  \n  Licensed to mike@example.com "
                              + juce::String::fromUTF8 ("\xe2\x80\x94")
                              + " Pro Edition  \nsecond line\n");
        expect (spa::library::licenseLineFromFile (file)
                    == "Licensed to mike@example.com "
                       + juce::String::fromUTF8 ("\xe2\x80\x94") + " Pro Edition",
                "first non-empty line, trimmed");

        file.replaceWithText ("   \n\n");
        expect (spa::library::licenseLineFromFile (file).isEmpty(),
                "whitespace-only file yields empty");

        file.deleteFile();
    }

    static void presetBrowserFilterTest()
    {
        std::cout << "presetBrowserFilterTest\n";

        using Info = spa::library::PresetManager::PresetInfo;
        using Browser = spa::ui::PresetBrowser;

        const std::vector<Info> presets {
            { "Anvil Keys",    "Anvil", {}, false },
            { "Anvil Texture", "Anvil", {}, false },
            { "Bells Pulse",   "Bells", {}, false },
            { "My Lead",       "User",  {}, true  },
        };

        expect (Browser::typeOf (presets[0]) == "Keys", "factory type derives from name suffix");
        expect (Browser::typeOf (presets[3]) == "User", "isUser flag wins over name, not the category string");
        expect (Browser::typeOf ({ "Weird Name", "Bells", {}, false }).isEmpty(),
                "unknown factory shape has no type");
        expect (Browser::typeOf ({ "Bank Lead", "Leads", {}, true }) == "User",
                "a bank preset (category != \"User\") still counts as User via the isUser flag");

        const auto names = [&] (const std::vector<int>& idx)
        {
            juce::StringArray out;
            for (auto i : idx)
                out.add (presets[(size_t) i].name);
            return out.joinIntoString (",");
        };

        expect (Browser::filterIndices (presets, {}, {}).size() == 4,
                "empty filter passes everything");
        expect (names (Browser::filterIndices (presets, { {}, "Keys", {}, false }, {}))
                    == "Anvil Keys", "type chip filters by preset flavour");
        expect (names (Browser::filterIndices (presets, { "bells", {}, {}, false }, {}))
                    == "Bells Pulse", "search is case-insensitive");
        expect (names (Browser::filterIndices (presets, { {}, {}, "Anvil", false }, {}))
                    == "Anvil Keys,Anvil Texture", "category filters by pack");
        expect (names (Browser::filterIndices (presets, { {}, {}, {}, true },
                                               juce::StringArray ("Bells/Bells Pulse")))
                    == "Bells Pulse", "favorites-only keeps starred keys");
        expect (names (Browser::filterIndices (presets, { "anvil", "Texture", {}, false }, {}))
                    == "Anvil Texture", "filters combine (search + type)");
        expect (Browser::filterIndices (presets, { "zzz", {}, {}, false }, {}).empty(),
                "no match yields an empty list");
        expect (Browser::favoriteKey (presets[2]) == "Bells/Bells Pulse",
                "favorite key is category/name");
    }

    // Dependent-control dimming (LFO rate vs. division, gated by sync) --
    // exercises the real editor tree, since DependentEnable's whole job is
    // wiring live JUCE components, not just computing a bool.
    static void dependentEnableTest()
    {
        std::cout << "dependentEnableTest\n";

        namespace id = spa::params::id;

        spa::SPASynthProcessor proc;
        proc.prepareToPlay (48000.0, 512);

        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
        editor->setSize (spa::ui::metrics::baseWidth, spa::ui::metrics::baseHeight);

        auto* rate = findByParamID (*editor, id::lfoParam (0, id::lfo::rate));
        auto* division = findByParamID (*editor, id::lfoParam (0, id::lfo::division));
        expect (rate != nullptr && division != nullptr, "LFO 1 rate/division controls found");
        if (rate == nullptr || division == nullptr)
            return;

        // AsyncUpdater's message defers to the message thread -- poll with a
        // deadline rather than a single dispatch pass, same idiom as
        // waitForSample() above.
        const auto pumpUntil = [] (std::function<bool()> ready)
        {
            const auto deadline = juce::Time::getMillisecondCounter() + 2000u;
            while (! ready() && juce::Time::getMillisecondCounter() < deadline)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        };

        // Default: sync off -> rate is live, division is irrelevant.
        expect (rate->isEnabled(), "rate enabled while unsynced (default)");
        expect (! division->isEnabled(), "division disabled while unsynced (default)");

        setParam (proc, id::lfoParam (0, id::lfo::sync), 1.0f);
        pumpUntil ([&] { return ! rate->isEnabled(); });

        expect (! rate->isEnabled(), "rate disabled once synced");
        expect (division->isEnabled(), "division enabled once synced");

        setParam (proc, id::lfoParam (0, id::lfo::sync), 0.0f);
        pumpUntil ([&] { return rate->isEnabled(); });

        expect (rate->isEnabled(), "rate re-enabled after sync turned back off");
        expect (! division->isEnabled(), "division re-disabled after sync turned back off");
    }

    // Structural regression test for the whole-app QWERTY focus-steal fix
    // (CLAUDE.md's 1.0.8/1.0.10 notes): every mouse-clickable JUCE widget
    // defaults to grabbing keyboard focus on click (Component::
    // internalMouseDown -> grabKeyboardFocusInternal, unconditional, walking
    // up the parent chain, re-checking each ancestor's OWN
    // dontFocusOnMouseClickFlag in turn, until something either takes focus
    // or blocks the attempt) -- which silently kills computer-keyboard
    // note-play via the on-screen keyboard until a virtual key is clicked
    // again, since MidiKeyboardComponent::keyStateChanged only fires while
    // it's the focused component (and focusLost() cuts any held notes).
    //
    // Walks the ENTIRE editor tree (every tab/section is constructed at
    // build time even when its tab isn't current, so a single build with
    // the preset drawer opened reaches everything) and asserts
    // getMouseClickGrabsKeyboardFocus() == false on every single component,
    // except a 3-item allowlist, each justified:
    //  - any juce::TextEditor, and anything inside one (its internal
    //    viewport/scrollbars/caret -- TextEditor is a composite component,
    //    not a leaf): these legitimately need focus on click so the user
    //    can type (the preset browser's search box today; any future
    //    TextEditor gets the same pass).
    //  - juce::MidiKeyboardComponent: needs to KEEP click-grabs-focus --
    //    that's how clicking a virtual key resumes QWERTY play today.
    //  - the PresetBrowser itself: togglePresetBrowser() deliberately calls
    //    grabKeyboardFocus() directly (not via a click) when the drawer
    //    opens AND the on-screen keyboard isn't visible, so Esc can still
    //    close it -- a pre-existing, intentional design unrelated to this
    //    bug, so its own background click is allowed to keep holding focus
    //    too. (This test's editor never shows the keyboard strip, so that
    //    branch is the one exercised here; see
    //    presetBrowserKeyboardFocusTest below for the keyboard-visible case,
    //    where opening the drawer must NOT steal focus.)
    static void presetBrowserFocusGrabTest()
    {
        std::cout << "presetBrowserFocusGrabTest\n";

        spa::SPASynthProcessor proc;
        proc.prepareToPlay (48000.0, 512);

        // Give the browser's list real, loadable rows without touching the
        // machine's actual factory/user preset folders: a handful of
        // throwaway user presets, clearly tagged so cleanup can't miss or
        // clobber anything real. saveUserPreset() just serializes the
        // current APVTS state -- no audio content needed.
        auto& pm = proc.getPresetManager();
        for (int i = 0; i < 8; ++i)
            expect (pm.saveUserPreset ("ZZ SPASynth Focus Test " + juce::String (i)),
                    "throwaway focus-test preset " + juce::String (i) + " saves");

        juce::Array<juce::File> createdFiles;
        for (const auto& p : pm.getPresets())
            if (p.name.startsWith ("ZZ SPASynth Focus Test"))
                createdFiles.add (p.file);

        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
        editor->setSize (spa::ui::metrics::baseWidth, spa::ui::metrics::baseHeight);

        spa::ui::PresetBrowser* browser = nullptr;
        juce::MidiKeyboardComponent* keyboard = nullptr;
        juce::Component* prevPresetButton = nullptr;
        juce::Component* nextPresetButton = nullptr;
        std::function<void (juce::Component&)> findParts = [&] (juce::Component& c)
        {
            if (browser == nullptr)
                browser = dynamic_cast<spa::ui::PresetBrowser*> (&c);
            if (keyboard == nullptr)
                keyboard = dynamic_cast<juce::MidiKeyboardComponent*> (&c);
            if (prevPresetButton == nullptr && c.getComponentID() == "navPrev")
                prevPresetButton = &c;
            if (nextPresetButton == nullptr && c.getComponentID() == "navNext")
                nextPresetButton = &c;
            for (auto* child : c.getChildren())
                findParts (*child);
        };
        findParts (*editor);

        expect (browser != nullptr, "preset browser found in the editor tree");
        expect (keyboard != nullptr, "on-screen keyboard found in the editor tree");
        expect (prevPresetButton != nullptr && nextPresetButton != nullptr,
                "top-bar preset nav carets found");

        if (browser == nullptr)
        {
            for (auto& f : createdFiles)
                f.deleteFile();
            return;
        }

        browser->openImmediately();
        browser->resized();   // force layout now, not on the next paint, so ListBox rows exist

        // Let anything deferred (animation/async) settle -- same idiom as
        // dependentEnableTest's pumpUntil above.
        {
            const auto deadline = juce::Time::getMillisecondCounter() + 500u;
            while (juce::Time::getMillisecondCounter() < deadline)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        }

        // Confirm the list actually has row components before trusting the
        // walk below to have exercised them -- an empty list would make
        // "rows are covered" vacuous.
        juce::ListBox* list = nullptr;
        std::function<void (juce::Component&)> findList = [&] (juce::Component& c)
        {
            if (list == nullptr)
                list = dynamic_cast<juce::ListBox*> (&c);
            for (auto* child : c.getChildren())
                findList (*child);
        };
        findList (*browser);
        expect (list != nullptr, "browser's ListBox found");

        int rowChildren = 0;
        if (list != nullptr)
            if (auto* vp = list->getViewport())
                if (auto* content = vp->getViewedComponent())
                    rowChildren = content->getNumChildComponents();
        expect (rowChildren > 0,
                "ListBox has row components after layout (" + juce::String (rowChildren) + " found)");

        // Builds "Type#id <- Type#id <- ..." from a component up to the
        // editor root, for offender diagnostics below.
        auto describeParentChain = [] (juce::Component& c)
        {
            juce::String chain;
            for (auto* p = c.getParentComponent(); p != nullptr; p = p->getParentComponent())
            {
                if (chain.isNotEmpty())
                    chain << " <- ";
                chain << typeid (*p).name();
                if (p->getComponentID().isNotEmpty())
                    chain << "#" << p->getComponentID();
            }
            return chain;
        };

        // The whole-editor sweep: zero tolerance except the 3-item
        // allowlist documented above the test.
        int offenders = 0;
        std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
        {
            const bool allowed = dynamic_cast<juce::TextEditor*> (&c) != nullptr
                               || c.findParentComponentOfClass<juce::TextEditor>() != nullptr
                               || dynamic_cast<juce::MidiKeyboardComponent*> (&c) != nullptr
                               || &c == browser;

            if (! allowed && c.getMouseClickGrabsKeyboardFocus())
            {
                ++offenders;
                std::cout << "  FAIL   focus-grab left on: " << typeid (c).name()
                          << "  name=\"" << c.getName() << "\""
                          << "  id=\"" << c.getComponentID() << "\""
                          << "  parents: " << describeParentChain (c) << "\n";
            }

            for (auto* child : c.getChildren())
                walk (*child);
        };
        walk (*editor);
        expect (offenders == 0,
                juce::String (offenders)
                    + " component(s) in the editor still grab keyboard focus on click");

        // Sanity check on the specific top-bar controls this bug report
        // names -- redundant with the sweep above, but pinned explicitly so
        // a future refactor that renames/moves them still gets a targeted
        // failure message.
        if (prevPresetButton != nullptr)
            expect (! prevPresetButton->getMouseClickGrabsKeyboardFocus(),
                    "prev-preset caret doesn't grab focus (8833a57)");
        if (nextPresetButton != nullptr)
            expect (! nextPresetButton->getMouseClickGrabsKeyboardFocus(),
                    "next-preset caret doesn't grab focus (8833a57)");
        if (keyboard != nullptr)
            expect (keyboard->getMouseClickGrabsKeyboardFocus(),
                    "on-screen keyboard keeps click-grabs-focus (needed for keyStateChanged/QWERTY)");

        for (auto& f : createdFiles)
            f.deleteFile();
    }

    // Behavioral regression for the "opening the preset browser with the
    // on-screen keyboard visible steals QWERTY focus" bug (found on
    // 1.0.10's first build, after presetBrowserFocusGrabTest's whole-tree
    // sweep landed). togglePresetBrowser() used to grabKeyboardFocus() on
    // the browser unconditionally on open; now it only does that when the
    // keyboard strip is hidden, and Esc is handled by ContentComponent::
    // keyPressed instead when focus stayed on the keyboard.
    //
    // Parts (a) and (b) below need REAL OS keyboard focus (grabKeyboardFocus
    // only takes effect when Component::isShowing() is true, which at the
    // root requires an actual peer -- see Component::grabKeyboardFocusInternal
    // in juce_Component.cpp), so the editor is addToDesktop()'d, unlike every
    // other test in this file. If that doesn't hold real focus in a given
    // headless CI environment, grabKeyboardFocus() silently no-ops (release
    // builds don't assert) and the "still focused" checks below would fail
    // honestly rather than pass vacuously -- so a failure here should be
    // read as "couldn't get real focus in this environment" before assuming
    // a code regression; the structural allowlist sweep in
    // presetBrowserFocusGrabTest above covers the same fix without needing
    // real focus.
    static void presetBrowserKeyboardFocusTest()
    {
        std::cout << "presetBrowserKeyboardFocusTest\n";

        const auto pumpFor = [] (int ms)
        {
            const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
            while (juce::Time::getMillisecondCounter() < deadline)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        };

        auto findParts = [] (juce::Component& root, spa::ui::PresetBrowser*& browser,
                             juce::MidiKeyboardComponent*& keyboard,
                             juce::TextButton*& presetButton)
        {
            std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
            {
                if (browser == nullptr)
                    browser = dynamic_cast<spa::ui::PresetBrowser*> (&c);
                if (keyboard == nullptr)
                    keyboard = dynamic_cast<juce::MidiKeyboardComponent*> (&c);
                if (presetButton == nullptr)
                    if (auto* b = dynamic_cast<juce::TextButton*> (&c))
                        if (b->getTooltip() == "Browse presets")
                            presetButton = b;
                for (auto* child : c.getChildren())
                    walk (*child);
            };
            walk (root);
        };

        // (a) + (b): keyboard strip visible.
        {
            spa::SPASynthProcessor proc;
            proc.prepareToPlay (48000.0, 512);
            proc.getAPVTS().state.setProperty ("uiKeyboardVisible", true, nullptr);

            std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
            editor->setSize (spa::ui::metrics::baseWidth,
                             spa::ui::metrics::baseHeight + spa::ui::metrics::keyboardStripHeight);
            editor->addToDesktop (0);
            editor->setVisible (true);
            pumpFor (200);   // let the peer settle before asking it to hold focus

            spa::ui::PresetBrowser* browser = nullptr;
            juce::MidiKeyboardComponent* keyboard = nullptr;
            juce::TextButton* presetButton = nullptr;
            findParts (*editor, browser, keyboard, presetButton);

            expect (browser != nullptr && keyboard != nullptr && presetButton != nullptr,
                    "browser/keyboard/preset button found (keyboard-visible editor)");

            if (browser != nullptr && keyboard != nullptr && presetButton != nullptr)
            {
                keyboard->grabKeyboardFocus();
                pumpFor (50);
                const bool gotRealFocus = keyboard->hasKeyboardFocus (false);

                if (! gotRealFocus)
                {
                    std::cout << "  ..   couldn't obtain real OS keyboard focus in this "
                                 "environment -- skipping (a)/(b), covered structurally "
                                 "by presetBrowserFocusGrabTest instead\n";
                }
                else
                {
                    // (a) Opening the drawer must NOT move focus off the keyboard.
                    presetButton->triggerClick();
                    pumpFor (300);   // outlast the 170ms open animation

                    expect (keyboard->hasKeyboardFocus (false),
                            "(a) on-screen keyboard keeps real focus when the drawer opens "
                            "over it");
                    expect (! browser->hasKeyboardFocus (true),
                            "(a) preset browser does NOT take focus while the keyboard is "
                            "visible");

                    // (b) Esc must still close the drawer, reaching
                    // ContentComponent::keyPressed via the parent walk since
                    // MidiKeyboardComponent::keyPressed returns false for a key
                    // it doesn't map (juce_MidiKeyboardComponent.cpp) --
                    // exercised through the real peer, the same path a live
                    // Esc keystroke takes (ComponentPeer::handleKeyPress in
                    // juce_ComponentPeer.cpp).
                    const auto openBounds = browser->getOpenBounds();
                    const auto closedBounds = openBounds.translated (
                        -openBounds.getWidth() - 12, 0);
                    expect (browser->getBounds() == openBounds,
                            "(b) drawer is at its open bounds before Esc");

                    if (auto* peer = editor->getPeer())
                        peer->handleKeyPress (juce::KeyPress::escapeKey, 0);
                    pumpFor (500);   // outlast the 170ms close animation

                    expect (browser->getBounds() == closedBounds,
                            "(b) Esc closed the drawer while focus was on the keyboard");
                    expect (keyboard->hasKeyboardFocus (false),
                            "(b) keyboard still/again has focus after Esc closed the drawer");
                }
            }

            editor->removeFromDesktop();
        }

        // (c) keyboard strip hidden: opening the drawer still gives the
        // browser focus, so Esc has something focused to reach it through.
        {
            spa::SPASynthProcessor proc;
            proc.prepareToPlay (48000.0, 512);
            // uiKeyboardVisible defaults to false -- don't set it.

            std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
            editor->setSize (spa::ui::metrics::baseWidth, spa::ui::metrics::baseHeight);
            editor->addToDesktop (0);
            editor->setVisible (true);
            pumpFor (200);

            spa::ui::PresetBrowser* browser = nullptr;
            juce::MidiKeyboardComponent* keyboard = nullptr;
            juce::TextButton* presetButton = nullptr;
            findParts (*editor, browser, keyboard, presetButton);

            expect (browser != nullptr && presetButton != nullptr,
                    "browser/preset button found (keyboard-hidden editor)");

            if (browser != nullptr && presetButton != nullptr)
            {
                presetButton->triggerClick();
                pumpFor (50);

                expect (browser->hasKeyboardFocus (true),
                        "(c) preset browser (or a child, e.g. search box) takes focus when "
                        "opened with the keyboard strip hidden, so Esc still works there");
            }

            editor->removeFromDesktop();
        }
    }

    // Regression for the VOICE call-out's MODE/PRIORITY dropdowns being
    // "finnicky" in Logic (v1.0.11, Mike): needed a click-and-hold to keep
    // the menu open at all, and items weren't selectable even then. Root
    // cause traced through JUCE source: CallOutBox::launchAsynchronously was
    // called with a null parent (SPASynthEditor.cpp), so it added itself
    // straight to the desktop as its own native peer/window and started a
    // 100ms self-toFront(true) timer that force-claims real OS key-window
    // status (juce_CallOutBox.cpp) -- a second peer contending with the
    // editor's own peer and the popup menu's peer right as the combo popup
    // opens. On top of that, CallOutBox::enterModalState(true, ...) tries to
    // grabKeyboardFocus() when it opens, but every control VoicePanel hosts
    // has wantsKeyboardFocus explicitly off (Controls.h's Choice/Knob, part
    // of the QWERTY focus-steal fixes), so the grab found no target and
    // silently no-opped. With nothing holding real Component-level focus,
    // the popup menu's own dismiss-on-focus-loss safety net
    // (juce_PopupMenu.cpp's doesAnyJuceCompHaveFocus/checkButtonState) fell
    // back to a racy native per-peer key-window check instead of the
    // reliable "the clicked combo already holds focus" case every other
    // combo in the app gets. Fixed two ways: (1) VoicePanel's call-out is
    // now parented to the editor shell (getTopLevelComponent()), like the
    // accent picker's call-out already was, so it never creates that second
    // native peer; (2) VoicePanel itself is left focusable (unlike every
    // other widget in this UI) so CallOutBox's own focus grab has a real,
    // deterministic target the moment it opens.

    // Regression (Mike, 2026-09-05, 1.0.12 in Logic): open the VOICE call-out,
    // switch the voice mode, then close the plugin window -> crash inside
    // JuceAU deleteEditor ("pointer being freed was not allocated"). Mirrors
    // that sequence on a desktop-hosted editor for both the call-out-still-
    // open and the just-dismissed cases; a crash here is the failure.
    static void voicePanelEditorCloseTest()
    {
        std::cout << "voicePanelEditorCloseTest\n";
        namespace id = spa::params::id;

        const auto pumpFor = [] (int ms)
        {
            const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
            while (juce::Time::getMillisecondCounter() < deadline)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        };
        const auto findVoiceButton = [] (juce::Component& root) -> juce::Button*
        {
            juce::Button* found = nullptr;
            std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
            {
                if (found == nullptr)
                    if (auto* b = dynamic_cast<juce::Button*> (&c))
                        if (b->getTooltip().startsWith ("Voice mode"))
                            found = b;
                for (auto* child : c.getChildren()) walk (*child);
            };
            walk (root);
            return found;
        };
        const auto findCallout = [] (juce::Component& root) -> juce::CallOutBox*
        {
            juce::CallOutBox* found = nullptr;
            std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
            {
                if (found == nullptr) found = dynamic_cast<juce::CallOutBox*> (&c);
                for (auto* child : c.getChildren()) walk (*child);
            };
            walk (root);
            return found;
        };

        // Variants: 0 = window closed with the call-out still open; 1/2 =
        // dismissed shortly/long before the close; 3 = window closed with
        // the call-out open AND the processor destroyed immediately after,
        // with no message pump in between -- what a host does on project
        // close. The orphaned call-out's VoicePanel still held parameter
        // attachments, and the modal manager's deferred delete then ran
        // their destructors against a dead APVTS (heap-use-after-free under
        // ASan). ContentComponent's destructor now detaches the panel
        // synchronously.
        for (int variant = 0; variant < 4; ++variant)
        {
            auto procPtr = std::make_unique<spa::SPASynthProcessor>();
            auto& proc = *procPtr;
            proc.prepareToPlay (48000.0, 512);
            // Host-style holder, modelled on the JUCE AU wrapper's
            // EditorCompHolder: it is the top-level component and its
            // destructor deleteAllChildren()s. Anything a plugin wrongly
            // parents to getTopLevelComponent() gets `delete`d here.
            struct HostHolder : juce::Component
            {
                ~HostHolder() override { deleteAllChildren(); }
            };
            auto holder = std::make_unique<HostHolder>();
            auto* editorRaw = proc.createEditor();
            editorRaw->setSize (spa::ui::metrics::baseWidth, spa::ui::metrics::baseHeight);
            holder->addAndMakeVisible (editorRaw);
            holder->setSize (editorRaw->getWidth(), editorRaw->getHeight());
            holder->addToDesktop (0);
            holder->setVisible (true);
            pumpFor (150);
            juce::Component& editor = *editorRaw;

            auto* voiceButton = findVoiceButton (editor);
            expect (voiceButton != nullptr, "VOICE button found");
            if (voiceButton == nullptr) return;
            voiceButton->triggerClick();
            pumpFor (60);
            // SafePointer, not a raw pointer: JUCE's CallOutBoxCallback runs
            // a 200ms timer that dismisses the call-out whenever the process
            // is not in the foreground (a CLI test run never is), and the
            // ModalComponentManager then deletes it asynchronously -- so
            // across the pumps below this pointer can legitimately die.
            // Holding it raw made this test crash ~1 run in 3 (a genuine
            // heap-use-after-free in the TEST, found by ASan 2026-09-07).
            juce::Component::SafePointer<juce::CallOutBox> callout (findCallout (editor));
            expect (callout != nullptr, "call-out open");
            expect (callout != nullptr && callout->getParentComponent() == editorRaw,
                    "call-out is parented to the editor shell, not the host's top-level holder");

            // Switch the voice mode while the call-out is showing (Poly -> Mono -> Unison).
            setParam (proc, id::voiceMode, 1.0f);
            pumpFor (60);
            setParam (proc, id::voiceMode, 4.0f);
            pumpFor (60);

            if (variant == 1 && callout != nullptr) { callout->dismiss(); pumpFor (20); }
            if (variant == 2 && callout != nullptr) { callout->dismiss(); pumpFor (300); }

            holder.reset();          // host closes the window (deleteAllChildren)
            if (variant == 3)
                procPtr.reset();     // ...and the processor, before any message pump
            pumpFor (400);           // let deferred modal cleanup run
            expect (true, juce::String ("editor closed after VOICE mode switch, variant ") + juce::String (variant));
        }
    }

    static void voicePanelCallOutFocusTest()
    {
        std::cout << "voicePanelCallOutFocusTest\n";

        const auto pumpFor = [] (int ms)
        {
            const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
            while (juce::Time::getMillisecondCounter() < deadline)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        };

        const auto findByTooltip = [] (juce::Component& root, const juce::String& tooltip) -> juce::Button*
        {
            juce::Button* found = nullptr;
            std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
            {
                if (found == nullptr)
                    if (auto* b = dynamic_cast<juce::Button*> (&c))
                        if (b->getTooltip() == tooltip)
                            found = b;
                for (auto* child : c.getChildren())
                    walk (*child);
            };
            walk (root);
            return found;
        };

        // --- structural: the call-out is reachable from the editor tree once
        // open (proves it's parented, not a separate desktop peer), and its
        // own focus flags are set as the fix intends. Needs a REAL peer
        // (unlike presetBrowserFocusGrabTest's non-desktop editor): opening
        // the call-out runs CallOutBox::enterModalState(true, ...), which
        // calls grabKeyboardFocus() -> jassert (isShowing() || isOnDesktop())
        // (juce_Component.cpp) -- that would trip in a debug build with no
        // peer at all, the same reason presetBrowserKeyboardFocusTest's (a)/
        // (b) parts need addToDesktop.
        {
            spa::SPASynthProcessor proc;
            proc.prepareToPlay (48000.0, 512);

            std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
            editor->setSize (spa::ui::metrics::baseWidth, spa::ui::metrics::baseHeight);
            editor->addToDesktop (0);
            editor->setVisible (true);
            pumpFor (200);

            auto* voiceButton = findByTooltip (*editor,
                "Voice mode: Poly / Mono / Duo / Paraphonic / Unison");
            expect (voiceButton != nullptr, "VOICE button found in the editor tree");

            if (voiceButton != nullptr)
            {
                voiceButton->triggerClick();
                pumpFor (50);

                // Find the call-out by walking the WHOLE editor -- proves
                // it's reachable there at all, i.e. actually parented
                // (CallOutBox::launchAsynchronously's parent!=nullptr path,
                // juce_CallOutBox.cpp), not off on its own as a bare
                // desktop peer the rest of the tree can't see.
                juce::CallOutBox* callout = nullptr;
                std::function<void (juce::Component&)> findCallout = [&] (juce::Component& c)
                {
                    if (callout == nullptr)
                        callout = dynamic_cast<juce::CallOutBox*> (&c);
                    for (auto* child : c.getChildren())
                        findCallout (*child);
                };
                findCallout (*editor);

                expect (callout != nullptr,
                        "VOICE call-out is a child of the editor (parented, not a bare "
                        "desktop peer)");

                // VoicePanel is anonymous-namespace-local to
                // SPASynthEditor.cpp, so it can't be dynamic_cast by name
                // here -- but it's CallOutBox's one and only content child
                // (juce::CallOutBox's ctor: addAndMakeVisible (content)).
                juce::Component* voicePanel = callout != nullptr && callout->getNumChildComponents() == 1
                                                 ? callout->getChildComponent (0) : nullptr;
                expect (voicePanel != nullptr, "VoicePanel found inside the call-out");
                if (voicePanel != nullptr)
                    expect (voicePanel->getWantsKeyboardFocus(),
                            "VoicePanel itself wants keyboard focus, so CallOutBox's own "
                            "enterModalState(true, ...) grab has a real target");

                // Sweep the call-out's own subtree (not the whole editor --
                // that's presetBrowserFocusGrabTest's job, with its own
                // documented allowlist) for anything still grabbing focus on
                // click. Same zero-tolerance shape as that test, with
                // exactly one exception: VoicePanel itself, the root of the
                // subtree, per the comment on setWantsKeyboardFocus in
                // SPASynthEditor.cpp's VoicePanel.
                if (callout != nullptr)
                {
                    int offenders = 0;
                    std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
                    {
                        const bool allowed = &c == voicePanel;
                        if (! allowed && c.getMouseClickGrabsKeyboardFocus())
                        {
                            ++offenders;
                            std::cout << "  FAIL   focus-grab left on: " << typeid (c).name() << "\n";
                        }
                        for (auto* child : c.getChildren())
                            walk (*child);
                    };
                    walk (*callout);

                    expect (offenders == 0,
                            juce::String (offenders) + " control(s) inside the VOICE call-out "
                            "(other than the panel itself) still grab keyboard focus on click");
                }

                if (callout != nullptr)
                    callout->dismiss();
                pumpFor (50);
            }

            editor->removeFromDesktop();
        }

        // --- behavioral: with a real OS peer, opening the call-out gives it
        // real focus, and closing it hands focus back to the on-screen
        // keyboard when the keyboard strip is showing -- same
        // "gotRealFocus" best-effort pattern as presetBrowserKeyboardFocusTest,
        // since headless CI can't always grant real OS keyboard focus.
        {
            spa::SPASynthProcessor proc;
            proc.prepareToPlay (48000.0, 512);
            proc.getAPVTS().state.setProperty ("uiKeyboardVisible", true, nullptr);

            std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
            editor->setSize (spa::ui::metrics::baseWidth,
                             spa::ui::metrics::baseHeight + spa::ui::metrics::keyboardStripHeight);
            editor->addToDesktop (0);
            editor->setVisible (true);
            pumpFor (200);

            juce::MidiKeyboardComponent* keyboard = nullptr;
            std::function<void (juce::Component&)> findKeyboard = [&] (juce::Component& c)
            {
                if (keyboard == nullptr)
                    keyboard = dynamic_cast<juce::MidiKeyboardComponent*> (&c);
                for (auto* child : c.getChildren())
                    findKeyboard (*child);
            };
            findKeyboard (*editor);

            auto* voiceButton = findByTooltip (*editor,
                "Voice mode: Poly / Mono / Duo / Paraphonic / Unison");
            expect (keyboard != nullptr && voiceButton != nullptr,
                    "keyboard + VOICE button found (real-peer editor)");

            if (keyboard != nullptr && voiceButton != nullptr)
            {
                keyboard->grabKeyboardFocus();
                pumpFor (50);
                const bool gotRealFocus = keyboard->hasKeyboardFocus (false);

                if (! gotRealFocus)
                {
                    std::cout << "  ..   couldn't obtain real OS keyboard focus in this "
                                 "environment -- skipping the behavioral half, covered "
                                 "structurally above\n";
                }
                else
                {
                    voiceButton->triggerClick();
                    pumpFor (150);

                    juce::CallOutBox* callout = nullptr;
                    std::function<void (juce::Component&)> findCallout = [&] (juce::Component& c)
                    {
                        if (callout == nullptr)
                            callout = dynamic_cast<juce::CallOutBox*> (&c);
                        for (auto* child : c.getChildren())
                            findCallout (*child);
                    };
                    findCallout (*editor);
                    expect (callout != nullptr, "call-out opened (real-peer editor)");

                    if (callout != nullptr)
                    {
                        callout->dismiss();
                        pumpFor (300);

                        expect (keyboard->hasKeyboardFocus (false),
                                "on-screen keyboard has real focus back after the VOICE "
                                "call-out closes");
                    }
                }
            }

            editor->removeFromDesktop();
        }
    }

    // Regression for a bug Mike hit in Logic: the ENV/LFO tab bars rendered
    // with generous, evenly-spaced default widths, then snapped to a
    // condensed/bunched-left layout the instant another tab was clicked.
    // Root cause: ContentComponent used to give itself its one-and-only real
    // (untransformed) layout pass INSIDE ITS OWN CONSTRUCTOR, before
    // SPASynthEditor ever parented it -- so every TabbedButtonBar's
    // getLookAndFeel() fell through to JUCE's global default LookAndFeel
    // (Component::getLookAndFeel() walks the live parent chain and falls
    // back when it finds no ancestor with one set) for that first paint,
    // instead of SPASynthLookAndFeel. SPASynthEditor::resized() only ever
    // applies an AffineTransform to `content` for the fixed-aspect scaling
    // shell -- it never calls content->setSize()/setBounds() again -- so
    // that wrong-LookAndFeel layout silently stuck until something forced a
    // fresh TabbedButtonBar::resized(), e.g. TabbedButtonBar::setCurrentTabIndex()
    // (unconditional resized() regardless of whether bounds changed), which
    // by then correctly resolved SPASynthLookAndFeel and condensed the tabs
    // to its (narrower, text-fit-only) widths. Fixed two ways: (1)
    // ContentComponent's constructor no longer calls setSize() on itself --
    // SPASynthEditor's constructor does, AFTER addAndMakeVisible(*content),
    // so the one real layout pass always resolves the correct LookAndFeel;
    // (2) SPASynthLookAndFeel::getTabButtonBestWidth now floors at
    // tabDepth*2 (matching JUCE's own LookAndFeel_V2 default convention)
    // instead of a bare 36px, so short tab names keep the generous look
    // deterministically rather than by accident.
    static void tabLayoutInvarianceTest()
    {
        std::cout << "tabLayoutInvarianceTest\n";

        spa::SPASynthProcessor proc;
        proc.prepareToPlay (48000.0, 512);

        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
        editor->setSize (spa::ui::metrics::baseWidth, spa::ui::metrics::baseHeight);
        editor->addToDesktop (0);
        editor->setVisible (true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (200);

        juce::TabbedComponent* envTabs = nullptr;
        juce::TabbedComponent* lfoTabs = nullptr;
        juce::TabbedComponent* filterTabs = nullptr;
        juce::TabbedComponent* fxTabs = nullptr;
        std::function<void (juce::Component&)> find = [&] (juce::Component& c)
        {
            if (auto* t = dynamic_cast<juce::TabbedComponent*> (&c))
            {
                if (t->getTabNames().contains ("ENV 2"))
                    envTabs = t;
                if (t->getTabNames().contains ("LFO 2"))
                    lfoTabs = t;
                if (t->getTabNames().contains ("FILTER 2"))
                    filterTabs = t;
                if (t->getTabNames().contains ("TREM/VIB"))
                    fxTabs = t;
            }
            for (auto* child : c.getChildren())
                find (*child);
        };
        find (*editor);

        expect (envTabs != nullptr && lfoTabs != nullptr && filterTabs != nullptr && fxTabs != nullptr,
                "all four tab bars found (envTabs/lfoTabs/filterTabs/fxTabs)");
        if (envTabs == nullptr || lfoTabs == nullptr || filterTabs == nullptr || fxTabs == nullptr)
            return;

        auto snapshotBounds = [] (juce::TabbedComponent& tabs)
        {
            std::vector<juce::Rectangle<int>> bounds;
            auto& bar = tabs.getTabbedButtonBar();
            for (int i = 0; i < bar.getNumTabs(); ++i)
                bounds.push_back (bar.getTabButton (i) != nullptr
                                       ? bar.getTabButton (i)->getBounds() : juce::Rectangle<int>());
            return bounds;
        };

        const auto envBefore = snapshotBounds (*envTabs);
        const auto lfoBefore = snapshotBounds (*lfoTabs);
        const auto filterBefore = snapshotBounds (*filterTabs);
        const auto fxBefore = snapshotBounds (*fxTabs);

        // Every tab in every bar must have a real (non-empty) width right
        // from the first show -- the whole point is that the default IS the
        // final layout, not a placeholder that later "settles".
        for (const auto& b : envBefore)
            expect (b.getWidth() > 0, "envTabs tab has a real width before any selection change");

        envTabs->setCurrentTabIndex (1);
        lfoTabs->setCurrentTabIndex (2);
        filterTabs->setCurrentTabIndex (1);
        fxTabs->setCurrentTabIndex (fxTabs->getTabNames().indexOf ("TREM/VIB"));
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

        auto expectUnchanged = [&] (const char* name, juce::TabbedComponent& tabs,
                                    const std::vector<juce::Rectangle<int>>& before)
        {
            const auto after = snapshotBounds (tabs);
            bool same = after.size() == before.size();
            for (size_t i = 0; same && i < before.size(); ++i)
                same = after[i] == before[i];
            if (! same)
            {
                std::cout << "  " << name << " bounds changed after selection:\n";
                for (size_t i = 0; i < before.size(); ++i)
                    std::cout << "    tab " << i << " before=" << before[i].toString()
                              << " after=" << (i < after.size() ? after[i].toString() : "?") << "\n";
            }
            expect (same, juce::String (name) + " tab bounds unchanged after switching the selected tab");
        };

        expectUnchanged ("envTabs", *envTabs, envBefore);
        expectUnchanged ("lfoTabs", *lfoTabs, lfoBefore);
        expectUnchanged ("filterTabs", *filterTabs, filterBefore);
        expectUnchanged ("fxTabs", *fxTabs, fxBefore);

        // Switch back to the original tabs too -- bounds must be identical
        // both ways, not just stable after the first click.
        envTabs->setCurrentTabIndex (0);
        lfoTabs->setCurrentTabIndex (0);
        filterTabs->setCurrentTabIndex (0);
        fxTabs->setCurrentTabIndex (0);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

        expectUnchanged ("envTabs (back to original)", *envTabs, envBefore);
        expectUnchanged ("lfoTabs (back to original)", *lfoTabs, lfoBefore);
        expectUnchanged ("filterTabs (back to original)", *filterTabs, filterBefore);
        expectUnchanged ("fxTabs (back to original)", *fxTabs, fxBefore);

        editor->removeFromDesktop();
    }

    // Regression for a second restyle bug: TREM/VIB's bottom control row
    // (TREM SHAPE / TREM STEREO / TREM MIX / VIB RATE) had its caption
    // labels rendered half-clipped at the base window size. Root cause:
    // FXPanel::resized() capped the SectionPanel grid's height at
    // area.getHeight()-44 to always leave the FXDisplay scope a minimum, so
    // when a section needed two full control rows the grid got LESS height
    // than SectionPanel::heightForWidth() said it needed -- SectionPanel
    // itself lays out fixed-cellHeight rows from the top with no awareness
    // of whether it actually got enough room, so the second row (and its
    // bottom-anchored caption labels) rendered past the panel's own bottom
    // edge and got clipped there. Fix: FXPanel::resized() now always gives
    // the control grid its full needed height; the scope/display shrinks
    // into whatever remains instead (Mike's call: visualizers may shrink,
    // caption labels never clip).
    static void fxPanelLabelClippingTest()
    {
        std::cout << "fxPanelLabelClippingTest\n";

        spa::SPASynthProcessor proc;
        proc.prepareToPlay (48000.0, 512);

        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
        editor->setSize (spa::ui::metrics::baseWidth, spa::ui::metrics::baseHeight);

        juce::TabbedComponent* fxTabs = nullptr;
        std::function<void (juce::Component&)> find = [&] (juce::Component& c)
        {
            if (auto* t = dynamic_cast<juce::TabbedComponent*> (&c))
                if (t->getTabNames().contains ("TREM/VIB"))
                    fxTabs = t;
            for (auto* child : c.getChildren())
                find (*child);
        };
        find (*editor);

        expect (fxTabs != nullptr, "fxTabs found");
        if (fxTabs == nullptr)
            return;

        // Every FXPanel-backed tab (the ones with an auto-built SectionPanel
        // grid, per the FXPanel::resized() contract above) -- the bespoke
        // panels (EQ, LIMIT, CONV) have no SectionPanel and are skipped.
        const auto minLabelHeight = spa::ui::metrics::smallFont().getHeight() - 1.0f;

        for (const auto& tabName : { "DIST", "CHORUS", "DELAY", "REVERB", "MOD", "TREM/VIB" })
        {
            const auto index = fxTabs->getTabNames().indexOf (tabName);
            expect (index >= 0, juce::String (tabName) + " tab found");
            if (index < 0)
                continue;

            auto* panel = fxTabs->getTabContentComponent (index);
            expect (panel != nullptr, juce::String (tabName) + " panel content found");
            if (panel == nullptr)
                continue;

            spa::ui::SectionPanel* controls = nullptr;
            std::function<void (juce::Component&)> findSection = [&] (juce::Component& c)
            {
                if (controls == nullptr)
                    controls = dynamic_cast<spa::ui::SectionPanel*> (&c);
                for (auto* child : c.getChildren())
                    if (controls == nullptr)
                        findSection (*child);
            };
            findSection (*panel);

            expect (controls != nullptr, juce::String (tabName) + " SectionPanel found");
            if (controls == nullptr)
                continue;

            int labelsChecked = 0;
            for (auto* child : controls->getChildren())
            {
                auto* label = dynamic_cast<juce::Label*> (child);
                if (label == nullptr || ! label->isVisible())
                    continue;

                ++labelsChecked;
                const auto bottomOk = label->getBottom() <= controls->getHeight();
                const auto heightOk = (float) label->getHeight() >= minLabelHeight;
                if (! bottomOk || ! heightOk)
                    std::cout << "  " << tabName << " label '" << label->getText()
                              << "' bounds=" << label->getBounds().toString()
                              << " panelHeight=" << controls->getHeight() << "\n";
                expect (bottomOk, juce::String (tabName) + " label '" + label->getText()
                                       + "' bottom is within the panel's bounds");
                expect (heightOk, juce::String (tabName) + " label '" + label->getText()
                                       + "' has its full font height (not squashed)");
            }
            expect (labelsChecked > 0, juce::String (tabName) + " had caption labels to check");
        }
    }

    // ORGANIC CHAOS display: paints without crashing and actually draws
    // something (not a uniform image) after a chaos-active audio run feeds
    // the telemetry trace ring.
    static void chaosDisplayPaintTest()
    {
        std::cout << "chaosDisplayPaintTest\n";
        namespace id = spa::params::id;

        spa::SPASynthProcessor proc;
        proc.prepareToPlay (48000.0, 512);

        setParam (proc, id::chaos::enable, 1.0f);
        setParam (proc, id::chaos::depth, 1.0f);
        setParam (proc, id::chaos::rate, 8.0f);

        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        for (int b = 0; b < (int) (1.0 * 48000.0 / 512); ++b)
        {
            proc.processBlock (buffer, midi);
            midi.clear();
        }

        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
        editor->setSize (spa::ui::metrics::baseWidth, spa::ui::metrics::baseHeight);

        spa::ui::ChaosDisplay* chaos = nullptr;
        std::function<void (juce::Component&)> find = [&] (juce::Component& c)
        {
            if (chaos == nullptr)
                chaos = dynamic_cast<spa::ui::ChaosDisplay*> (&c);
            for (auto* child : c.getChildren())
                if (chaos == nullptr)
                    find (*child);
        };
        find (*editor);

        expect (chaos != nullptr, "ChaosDisplay found");
        if (chaos == nullptr)
            return;

        juce::Image image (juce::Image::ARGB, juce::jmax (1, chaos->getWidth()),
                           juce::jmax (1, chaos->getHeight()), true);
        juce::Graphics g (image);
        chaos->paintEntireComponent (g, false);

        // Sample a handful of pixels across the width; a real trace should
        // not leave the image a single uniform colour.
        std::set<juce::uint32> seen;
        for (int x = 0; x < image.getWidth(); x += juce::jmax (1, image.getWidth() / 20))
            for (int y = 0; y < image.getHeight(); y += juce::jmax (1, image.getHeight() / 6))
                seen.insert (image.getPixelAt (x, y).getARGB());

        expect (seen.size() > 1, "chaos display paints non-uniform content");
    }

    // Tester request: enabled FX tabs bold their label so the user can see at
    // a glance which effects are engaged. isTabEngaged() is the generic hook
    // (ContentComponent maps tab name -> enable param id(s)); this exercises
    // it end to end through real APVTS parameter changes, and asserts tab
    // widths never move when the weight flips (getTabButtonBestWidth always
    // measures with the bold font -- see SPASynthLookAndFeel.cpp).
    static void fxTabEngagedBoldTest()
    {
        std::cout << "fxTabEngagedBoldTest\n";
        namespace id = spa::params::id;

        spa::SPASynthProcessor proc;
        proc.prepareToPlay (48000.0, 512);

        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
        editor->setSize (spa::ui::metrics::baseWidth, spa::ui::metrics::baseHeight);
        editor->addToDesktop (0);
        editor->setVisible (true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (200);

        spa::ui::DraggableTabs* fxTabs = nullptr;
        std::function<void (juce::Component&)> find = [&] (juce::Component& c)
        {
            if (auto* t = dynamic_cast<spa::ui::DraggableTabs*> (&c))
                fxTabs = t;
            for (auto* child : c.getChildren())
                find (*child);
        };
        find (*editor);

        expect (fxTabs != nullptr, "fxTabs (DraggableTabs) found");
        if (fxTabs == nullptr)
            return;
        expect (fxTabs->isTabEngaged != nullptr, "fxTabs.isTabEngaged hook wired up");
        if (fxTabs->isTabEngaged == nullptr)
            return;

        auto snapshotWidths = [&]
        {
            std::vector<int> widths;
            auto& bar = fxTabs->getTabbedButtonBar();
            for (int i = 0; i < bar.getNumTabs(); ++i)
                widths.push_back (bar.getTabButton (i) != nullptr ? bar.getTabButton (i)->getWidth() : -1);
            return widths;
        };

        // All effects start disabled by default -- no tab should read engaged.
        expect (! fxTabs->isTabEngaged ("DIST"), "DIST starts disengaged");
        expect (! fxTabs->isTabEngaged ("REVERB"), "REVERB starts disengaged");
        expect (! fxTabs->isTabEngaged ("TREM/VIB"), "TREM/VIB starts disengaged");

        const auto widthsBefore = snapshotWidths();

        setParam (proc, id::fx::reverbEnable, 1.0f);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

        expect (fxTabs->isTabEngaged ("REVERB"), "REVERB engaged after enabling fxReverb.enable");
        expect (! fxTabs->isTabEngaged ("DIST"), "DIST still disengaged (only REVERB was toggled)");

        const auto widthsAfterReverb = snapshotWidths();
        expect (widthsAfterReverb == widthsBefore,
                "tab widths unchanged after REVERB goes bold (getTabButtonBestWidth is weight-independent)");

        // TREM/VIB is one tab for two effects -- either one enables it.
        setParam (proc, id::fx::tremEnable, 1.0f);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        expect (fxTabs->isTabEngaged ("TREM/VIB"), "TREM/VIB engaged when trem alone is on");

        setParam (proc, id::fx::tremEnable, 0.0f);
        setParam (proc, id::fx::vibEnable, 1.0f);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        expect (fxTabs->isTabEngaged ("TREM/VIB"), "TREM/VIB engaged when vib alone is on");

        setParam (proc, id::fx::vibEnable, 0.0f);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        expect (! fxTabs->isTabEngaged ("TREM/VIB"), "TREM/VIB disengaged once both trem and vib are off");

        // Toggle REVERB back off -- engagement clears and widths still hold.
        setParam (proc, id::fx::reverbEnable, 0.0f);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        expect (! fxTabs->isTabEngaged ("REVERB"), "REVERB disengaged after turning fxReverb.enable back off");

        const auto widthsAfter = snapshotWidths();
        expect (widthsAfter == widthsBefore, "tab widths unchanged after the full enable/disable round trip");

        editor->removeFromDesktop();
    }

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    if (argc >= 3 && juce::String (argv[1]) == "--snapshot")
    {
        renderEditorSnapshots (juce::File (argv[2]));
        return 0;
    }

    renderSmokeTest();
    multiSlotUnisonTest();
    wavetableLoaderTest();
    modMatrixMacroTest();
    lfoModulationTest();
    velocityRouteTest();
    chaosMixBypassTest();
    chaosMatrixSourceTest();
    chaosTraceTest();
    samplePlaybackTest();
    granularTest();
    quickSwapTest();
    sfxFollowerTest();
    fxDelayReverbTest();
    convolveTailLengthTest();
    reverbMixTest();
    reverbStabilityTest();
    distCrushTest();
    parametricEqTest();
    voiceModeTest();
    oversamplingTest();
    panicTest();
    bypassTailTest();
    midiClockTest();
    fxOrderTest();
    fxEQDistortionTest();
    fxToggleBlastTest();
    randomizerTest();
    randomizerProducesSoundTest();
    randomizeLoudnessGuardTest();
    editorHitTestProbe();
    midiLearnTest();
    arpeggiatorTest();
    arpStuckNoteTest();
    arpZeroSampleBlockTest();
    arpNonFinitePpqTest();
    arpNegativePpqTest();
    arpChanceTest();
    extraEnginesTest();
    pluckLazyAllocTest();
    filterExtrasTest();
    dualFilterTest();
    filter1EnableTest();
    glideTest();
    libraryScanTest();
    libraryDiscoveryTest();
    looseWavLibraryTest();
    libraryRootPersistsWhenEmptyTest();
    presetRoundTripTest();
    presetBankTest();
    malformedPresetTest();
    presetResetToDefaultTest();
    presetLoadNoiseBurstTest();
    factoryPresetGenerationTest();
    factoryPresetRootPackTest();
    presetBrowserFilterTest();
    licenseLineTest();
    dependentEnableTest();
    presetBrowserFocusGrabTest();
    presetBrowserKeyboardFocusTest();
    voicePanelCallOutFocusTest();
    voicePanelEditorCloseTest();
    tabLayoutInvarianceTest();
    fxPanelLabelClippingTest();
    chaosDisplayPaintTest();
    fxTabEngagedBoldTest();

    std::cout << (failures == 0 ? "ALL PASS" : juce::String (failures) + " FAILURES") << "\n";
    return failures == 0 ? 0 : 1;
}
