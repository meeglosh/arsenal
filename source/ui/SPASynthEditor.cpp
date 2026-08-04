#include "SPASynthEditor.h"
#include "../SPASynthProcessor.h"
#include "../library/Library.h"
#include "EqEditor.h"
#include "LimiterDisplay.h"

#include "BinaryData.h"

namespace spa
{

namespace ui
{

namespace
{
    // Drop-down colour picker for the two accents. Changes apply (and
    // persist) live as the user drags around the colour space.
    class AccentPicker : public juce::Component,
                         private juce::ChangeListener
    {
    public:
        explicit AccentPicker (std::function<void()> changed)
            : onChanged (std::move (changed))
        {
            const auto& t = currentTheme();

            const auto setUpLabel = [this] (juce::Label& l, const juce::String& text)
            {
                l.setText (text, juce::dontSendNotification);
                l.setFont (metrics::sectionFont());
                l.setJustificationType (juce::Justification::centred);
                addAndMakeVisible (l);
            };
            setUpLabel (audioLabel, "AUDIO ACCENT");
            setUpLabel (modLabel, "MOD ACCENT");

            audioSelector.setCurrentColour (t.accent, juce::dontSendNotification);
            modSelector.setCurrentColour (t.accentMod, juce::dontSendNotification);
            for (auto* selector : { &audioSelector, &modSelector })
            {
                selector->addChangeListener (this);
                addAndMakeVisible (*selector);
            }

            // LINK ties both accents to the one selector, for a single-colour UI.
            linkButton.setClickingTogglesState (true);
            linkButton.setToggleState (library::getAccentsLinked(),
                                       juce::dontSendNotification);
            linkButton.setTooltip ("Use one color for both accents");
            linkButton.onClick = [this]
            {
                library::setAccentsLinked (linkButton.getToggleState());
                if (linkButton.getToggleState())
                    applyColors();   // snap the mod accent to the audio pick
                resized();
                repaint();
            };
            addAndMakeVisible (linkButton);

            resetButton.onClick = [this]
            {
                // Both accents default to one colour, so reset also re-links.
                linkButton.setToggleState (true, juce::dontSendNotification);
                library::setAccentsLinked (true);
                resetAccentColors();
                const auto& theme = currentTheme();
                audioSelector.setCurrentColour (theme.accent, juce::dontSendNotification);
                modSelector.setCurrentColour (theme.accentMod, juce::dontSendNotification);
                resized();
                repaint();
                if (onChanged)
                    onChanged();
            };
            addAndMakeVisible (resetButton);

            setSize (480, 260);
        }

        ~AccentPicker() override
        {
            audioSelector.removeChangeListener (this);
            modSelector.removeChangeListener (this);
        }

        void resized() override
        {
            const auto linked = linkButton.getToggleState();
            modLabel.setVisible (! linked);
            modSelector.setVisible (! linked);
            audioLabel.setText (linked ? "ACCENT COLOR" : "AUDIO ACCENT",
                                juce::dontSendNotification);

            auto area = getLocalBounds().reduced (8);
            auto footer = area.removeFromBottom (26);
            linkButton.setBounds (footer.removeFromLeft (64).withHeight (24));
            resetButton.setBounds (footer.withSizeKeepingCentre (120, 24));
            area.removeFromBottom (6);

            if (linked)
            {
                // One selector rules both — give it the full width.
                audioLabel.setBounds (area.removeFromTop (16));
                audioSelector.setBounds (area.withSizeKeepingCentre (
                    juce::jmin (area.getWidth(), 300), area.getHeight()));
                return;
            }

            auto left = area.removeFromLeft (area.getWidth() / 2 - 4);
            audioLabel.setBounds (left.removeFromTop (16));
            audioSelector.setBounds (left);

            area.removeFromLeft (8);
            modLabel.setBounds (area.removeFromTop (16));
            modSelector.setBounds (area);
        }

    private:
        void applyColors()
        {
            const auto audio = audioSelector.getCurrentColour().withAlpha (1.0f);
            const auto mod = linkButton.getToggleState()
                           ? audio : modSelector.getCurrentColour().withAlpha (1.0f);
            if (linkButton.getToggleState())
                modSelector.setCurrentColour (mod, juce::dontSendNotification);

            setAccentColors (audio, mod);
            if (onChanged)
                onChanged();
        }

        void changeListenerCallback (juce::ChangeBroadcaster*) override
        {
            applyColors();
        }

        static constexpr int selectorFlags = juce::ColourSelector::showColourAtTop
                                           | juce::ColourSelector::showColourspace;

        std::function<void()> onChanged;
        juce::Label audioLabel, modLabel;
        juce::ColourSelector audioSelector { selectorFlags }, modSelector { selectorFlags };
        juce::TextButton linkButton { "LINK" };
        juce::TextButton resetButton { "RESET TO DEFAULT" };
    };
}

void ContentComponent::AccentButton::paintButton (juce::Graphics& g,
                                                  bool highlighted, bool down)
{
    const auto& t = currentTheme();
    auto bounds = getLocalBounds().toFloat();
    const auto diameter = juce::jmin (bounds.getWidth(), bounds.getHeight()) - 4.0f;
    const auto circle = bounds.withSizeKeepingCentre (diameter, diameter);

    juce::Path half;
    half.addPieSegment (circle, 0.0f, juce::MathConstants<float>::pi, 0.0f);
    g.setColour (t.accent);
    g.fillPath (half);

    half.clear();
    half.addPieSegment (circle, juce::MathConstants<float>::pi,
                        juce::MathConstants<float>::twoPi, 0.0f);
    g.setColour (t.accentMod);
    g.fillPath (half);

    g.setColour (t.outline);
    g.drawEllipse (circle, 1.0f);

    if (highlighted || down)
    {
        g.setColour (juce::Colours::white.withAlpha (down ? 0.25f : 0.12f));
        g.fillEllipse (circle);
    }
}

void ContentComponent::SettingsButton::paintButton (juce::Graphics& g,
                                                    bool highlighted, bool down)
{
    // The logo is painted by the parent behind this transparent overlay; only
    // add a hover/press highlight to signal it is clickable.
    if (highlighted || down)
    {
        g.setColour (juce::Colours::white.withAlpha (down ? 0.10f : 0.05f));
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (3.0f), 5.0f);
    }
}

void ContentComponent::KeyboardButton::paintButton (juce::Graphics& g,
                                                    bool highlighted, bool down)
{
    const auto& t = currentTheme();
    const bool on = getToggleState();
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const auto col = on ? t.accent
                        : (highlighted || down ? t.textPrimary : t.textSecondary);

    // A little piano: outlined body, white-key dividers, three black keys. Lit
    // in the accent colour while the keyboard strip is showing.
    g.setColour (col);
    g.drawRoundedRectangle (r, 1.5f, 1.0f);

    constexpr int whiteKeys = 5;
    const float kw = r.getWidth() / (float) whiteKeys;
    for (int i = 1; i < whiteKeys; ++i)
    {
        const float x = r.getX() + (float) i * kw;
        g.drawLine (x, r.getY(), x, r.getBottom(), 1.0f);
    }
    const float bh = r.getHeight() * 0.58f;
    const float bw = kw * 0.54f;
    for (int i : { 1, 2, 4 })
        g.fillRect (r.getX() + (float) i * kw - bw * 0.5f, r.getY(), bw, bh);
}

void ContentComponent::PanicButton::paintButton (juce::Graphics& g,
                                                 bool highlighted, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    const auto d = juce::jmin (r.getWidth(), r.getHeight());
    const auto circle = r.withSizeKeepingCentre (d, d);

    // Muted red at rest so it reads as the emergency stop; bright on hover/press.
    const auto red = juce::Colour (0xffff4d40);
    g.setColour ((highlighted || down) ? red : red.withAlpha (0.55f));

    g.drawEllipse (circle, 1.4f);
    const auto cx = circle.getCentreX();
    g.drawLine (cx, circle.getY() + d * 0.26f, cx, circle.getY() + d * 0.56f, 1.8f);  // ! stem
    const auto dot = d * 0.13f;
    g.fillEllipse (cx - dot * 0.5f, circle.getY() + d * 0.64f, dot, dot);             // ! dot
}

namespace
{
// Standalone-only tempo bar: internal BPM (editable), tap tempo, and an
// INT/EXT sync toggle. In EXT mode the field shows the incoming MIDI clock.
class TempoBar : public juce::Component,
                 private juce::Timer
{
public:
    explicit TempoBar (SPASynthProcessor& p) : processor (p)
    {
        tempo.setSliderStyle (juce::Slider::IncDecButtons);
        tempo.setRange (20.0, 300.0, 1.0);
        tempo.setValue (processor.getInternalBpm(), juce::dontSendNotification);
        tempo.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 44, 18);
        tempo.setTooltip ("Tempo (BPM) for the standalone");
        tempo.onValueChange = [this]
        {
            if (processor.getTempoSyncMode() == 0)
                processor.setInternalBpm (tempo.getValue());
        };
        addAndMakeVisible (tempo);

        tap.setButtonText ("TAP");
        tap.setTooltip ("Tap tempo");
        tap.onClick = [this] { onTap(); };
        addAndMakeVisible (tap);

        sync.setTooltip ("Tempo source: INT = internal clock, EXT = external MIDI clock");
        sync.onClick = [this] { toggleSync(); };
        addAndMakeVisible (sync);

        applyMode();
        startTimerHz (8);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        sync.setBounds (r.removeFromLeft (34).reduced (1));
        tap.setBounds (r.removeFromRight (34).reduced (1));
        tempo.setBounds (r.reduced (2, 1));
    }

private:
    void onTap()
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();
        if (lastTap > 0.0 && now - lastTap < 2000.0)
        {
            const auto bpm = 60000.0 / (now - lastTap);
            tapBpm = tapBpm > 0.0 ? tapBpm * 0.5 + bpm * 0.5 : bpm;
            tempo.setValue (juce::jlimit (20.0, 300.0, tapBpm), juce::sendNotification);
        }
        else tapBpm = 0.0;
        lastTap = now;
    }

    void toggleSync()
    {
        const int mode = processor.getTempoSyncMode() == 1 ? 0 : 1;
        processor.setTempoSyncMode (mode);
        applyMode();
    }

    void applyMode()
    {
        const bool ext = processor.getTempoSyncMode() == 1;
        sync.setButtonText (ext ? "EXT" : "INT");
        tempo.setEnabled (! ext);   // external MIDI clock drives the value
    }

    void timerCallback() override
    {
        if (processor.getTempoSyncMode() == 1)
            tempo.setValue (juce::roundToInt (processor.getCurrentBpm()),
                            juce::dontSendNotification);
    }

    SPASynthProcessor& processor;
    juce::Slider tempo;
    juce::TextButton tap, sync;
    double lastTap = 0.0, tapBpm = 0.0;
};

// Shaped-IR waveform view for the Convolve tab: the impulse envelope with the
// pre-delay shown as a leading gap. Reflects decay/damping (which reshape the
// IR) and pre-delay in ~real time.
class ConvolveDisplay : public juce::Component,
                        private juce::Timer
{
public:
    explicit ConvolveDisplay (SPASynthProcessor& p) : processor (p)
    {
        setInterceptsMouseClicks (false, false);
        startTimerHz (20);
    }
    ~ConvolveDisplay() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        const auto& t = currentTheme();
        auto area = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (t.display);
        g.fillRoundedRectangle (area, 4.0f);
        g.setColour (t.outline.withAlpha (0.5f));
        g.drawHorizontalLine ((int) area.getCentreY(), area.getX(), area.getRight());

        if (! processor.hasConvolutionIR())
        {
            g.setColour (t.textSecondary.withAlpha (0.5f));
            g.setFont (metrics::smallFont());
            g.drawText ("No impulse loaded - pick one above", area, juce::Justification::centred);
            g.setColour (t.outline);
            g.drawRoundedRectangle (area, 4.0f, 1.0f);
            return;
        }

        const auto& env = processor.getConvolutionEnvelope();
        float peak = 1.0e-6f;
        for (auto v : env) peak = juce::jmax (peak, v);

        const float preMs = processor.getAPVTS()
                                .getRawParameterValue (params::id::fx::convPreDelay)->load();
        const float gapFrac = juce::jlimit (0.0f, 0.4f, preMs / 400.0f);
        const float x0 = area.getX() + gapFrac * area.getWidth();
        const float w = area.getRight() - x0;
        const float cy = area.getCentreY();
        const float halfH = area.getHeight() * 0.45f;
        const int N = (int) env.size();

        juce::Path top;
        for (int i = 0; i < N; ++i)
        {
            const float a = env[(size_t) i] / peak * halfH;
            const float x = x0 + w * (float) i / (float) (N - 1);
            if (i == 0) top.startNewSubPath (x, cy - a); else top.lineTo (x, cy - a);
        }
        juce::Path fill = top;
        for (int i = N - 1; i >= 0; --i)
        {
            const float a = env[(size_t) i] / peak * halfH;
            fill.lineTo (x0 + w * (float) i / (float) (N - 1), cy + a);
        }
        fill.closeSubPath();
        g.setColour (t.accent.withAlpha (0.22f));
        g.fillPath (fill);
        g.setColour (t.accent);
        g.strokePath (top, juce::PathStrokeType (1.2f));

        if (gapFrac > 0.001f)   // pre-delay marker
        {
            g.setColour (t.textSecondary.withAlpha (0.5f));
            g.drawVerticalLine ((int) x0, area.getY(), area.getBottom());
        }

        g.setColour (t.outline);
        g.drawRoundedRectangle (area, 4.0f, 1.0f);
    }

    void timerCallback() override { repaint(); }

private:
    SPASynthProcessor& processor;
};

// Convolve tab: IR pickers, the waveform display, and the shaping controls.
class ConvolvePanel : public juce::Component,
                      private juce::ChangeListener
{
public:
    explicit ConvolvePanel (SPASynthProcessor& p)
        : processor (p), display (p),
          enable   (p.getAPVTS(), params::id::fx::convEnable, "ON"),
          mix      (p.getAPVTS(), params::id::fx::convMix, "Mix"),
          predelay (p.getAPVTS(), params::id::fx::convPreDelay, "Pre"),
          decay    (p.getAPVTS(), params::id::fx::convDecay, "Decay"),
          damping  (p.getAPVTS(), params::id::fx::convDamping, "Damp"),
          width    (p.getAPVTS(), params::id::fx::convWidth, "Width")
    {
        libraryButton.setTooltip ("Pick an impulse from a pack in your loaded library");
        libraryButton.onClick = [this] { chooseLibraryPack(); };
        irButton.setTooltip ("Load any WAV file as an impulse");
        irButton.onClick = [this] { chooseIR(); };
        title.setText ("CONVOLVE", juce::dontSendNotification);
        title.setFont (metrics::sectionFont());
        title.setColour (juce::Label::textColourId, currentTheme().accent);
        updateLabel();
        addAndMakeVisible (title);
        addAndMakeVisible (libraryButton);
        addAndMakeVisible (irButton);
        addAndMakeVisible (display);
        for (auto* c : std::initializer_list<juce::Component*> {
                 &enable, &mix, &predelay, &decay, &damping, &width })
            addAndMakeVisible (*c);
        processor.addChangeListener (this);
    }

    ~ConvolvePanel() override { processor.removeChangeListener (this); }

    void paint (juce::Graphics& g) override { g.fillAll (currentTheme().panel); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (7, 5);
        title.setBounds (r.removeFromTop (16));
        r.removeFromTop (2);

        auto top = r.removeFromTop (24);
        libraryButton.setBounds (top.removeFromLeft (128).reduced (0, 1));
        top.removeFromLeft (5);
        irButton.setBounds (top.reduced (0, 1));
        r.removeFromTop (4);

        auto strip = r.removeFromBottom (58);
        display.setBounds (r.reduced (0, 2));

        enable.setBounds (strip.removeFromLeft (54).reduced (2, 20));
        strip.removeFromLeft (4);
        for (auto* k : { &mix, &predelay, &decay, &damping, &width })
        {
            k->setBounds (strip.removeFromLeft (juce::jmin (60, strip.getWidth() / 5)));
            strip.removeFromLeft (2);
        }
    }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { updateLabel(); }

    void updateLabel()
    {
        const auto n = processor.getConvolutionIRName();
        libraryButton.setButtonText ("From library...");
        irButton.setButtonText (n.isEmpty() ? "Load WAV..." : "IR: " + n);
    }

    // Two-step browse (one folder scanned at a time, so it stays snappy on a
    // big library): pick a pack, then a sample from it.
    void chooseLibraryPack()
    {
        const auto root = library::findLibraryRoot();
        if (! root.isDirectory()) return;
        auto packs = root.findChildFiles (juce::File::findDirectories, false);
        std::sort (packs.begin(), packs.end(), [] (const juce::File& a, const juce::File& b)
                   { return a.getFileName().compareIgnoreCase (b.getFileName()) < 0; });

        juce::Component::SafePointer<ConvolvePanel> safe (this);
        juce::PopupMenu menu;
        for (const auto& folder : packs)
        {
            const auto f = folder;
            menu.addItem (folder.getFileName(), [safe, f]
            {
                if (safe != nullptr)
                    safe->chooseLibrarySample (f);
            });
        }
        if (menu.getNumItems() == 0)
            menu.addItem ("(no library found)", false, false, nullptr);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&libraryButton));
    }

    void chooseLibrarySample (const juce::File& packFolder)
    {
        auto wavs = packFolder.findChildFiles (juce::File::findFiles, false, "*.wav;*.WAV");
        std::sort (wavs.begin(), wavs.end(), [] (const juce::File& a, const juce::File& b)
                   { return a.getFileName().compareIgnoreCase (b.getFileName()) < 0; });

        juce::Component::SafePointer<ConvolvePanel> safe (this);
        juce::PopupMenu menu;
        for (const auto& wav : wavs)
        {
            const auto f = wav;
            menu.addItem (wav.getFileNameWithoutExtension(),
                          [safe, f]
            {
                if (safe != nullptr)
                {
                    safe->processor.loadConvolutionIR (f);
                    safe->updateLabel();
                }
            });
        }
        if (menu.getNumItems() == 0)
            menu.addItem ("(no samples in this pack)", false, false, nullptr);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&libraryButton));
    }

    void chooseIR()
    {
        const auto lastFolder = library::getLastIRFolder();
        const auto libraryRoot = library::findLibraryRoot();
        fileChooser = std::make_unique<juce::FileChooser> (
            "Choose an impulse (SFX or WAV)",
            lastFolder.isDirectory() ? lastFolder
                : libraryRoot.isDirectory() ? libraryRoot
                                            : juce::File::getSpecialLocation (juce::File::userHomeDirectory),
            "*.wav;*.WAV;*.aif;*.aiff;*.flac");
        fileChooser->launchAsync (
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc)
            {
                const auto f = fc.getResult();
                if (f.existsAsFile())
                {
                    library::setLastIRFolder (f);
                    processor.loadConvolutionIR (f);
                    updateLabel();
                }
            });
    }

    SPASynthProcessor& processor;
    juce::Label title;
    juce::TextButton libraryButton { "library" };
    juce::TextButton irButton { "browser" };
    ConvolveDisplay display;
    Toggle enable;
    Knob mix, predelay, decay, damping, width;
    std::unique_ptr<juce::FileChooser> fileChooser;
};

// Voice-allocation controls, shown in a call-out from the header VOICE button:
// mode + note priority, plus the unison voice/detune/width knobs.
class VoicePanel : public juce::Component
{
public:
    explicit VoicePanel (juce::AudioProcessorValueTreeState& apvts)
        : mode (apvts, params::id::voiceMode),
          priority (apvts, params::id::notePriority),
          voices (apvts, params::id::unisonVoices, "Voices"),
          detune (apvts, params::id::unisonDetune, "Detune"),
          width (apvts, params::id::unisonWidth, "Width")
    {
        modeLabel.setText ("MODE", juce::dontSendNotification);
        priorityLabel.setText ("PRIORITY", juce::dontSendNotification);
        for (auto* l : { &modeLabel, &priorityLabel })
        {
            l->setFont (metrics::smallFont());
            l->setJustificationType (juce::Justification::centredLeft);
            addAndMakeVisible (*l);
        }
        for (auto* c : std::initializer_list<juce::Component*> { &mode, &priority, &voices, &detune, &width })
            addAndMakeVisible (*c);
        setSize (244, 150);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (currentTheme().panel);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 8);
        auto row1 = r.removeFromTop (24);
        modeLabel.setBounds (row1.removeFromLeft (66));
        mode.setBounds (row1);
        r.removeFromTop (6);
        auto row2 = r.removeFromTop (24);
        priorityLabel.setBounds (row2.removeFromLeft (66));
        priority.setBounds (row2);
        r.removeFromTop (8);
        auto knobs = r;
        const int w = knobs.getWidth() / 3;
        voices.setBounds (knobs.removeFromLeft (w));
        detune.setBounds (knobs.removeFromLeft (w));
        width.setBounds (knobs);
    }

private:
    Choice mode, priority;
    Knob voices, detune, width;
    juce::Label modeLabel, priorityLabel;
};
} // namespace

ContentComponent::ContentComponent (SPASynthProcessor& p, std::function<void()> themeChanged)
    : processor (p), onThemeChanged (std::move (themeChanged)),
      keyboard (p.getKeyboardState(), juce::MidiKeyboardComponent::horizontalKeyboard),
      chaosPanel (p),
      arpPanel (p.getAPVTS()),
      matrixPanel (p.getAPVTS()),
      outputMeter (p.getTelemetry())
{
    logoDark = juce::Drawable::createFromImageData (SPAAssets::SPAudio_logo_white_svg,
                                                    SPAAssets::SPAudio_logo_white_svgSize);
    logoLight = juce::Drawable::createFromImageData (SPAAssets::SPAudio_logo_white_svg,
                                                     SPAAssets::SPAudio_logo_white_svgSize);

    // Logo (top-left) opens the SPASynth settings menu. Works in the plugin too,
    // unlike the standalone wrapper's audio-device "Options" menu.
    settingsButton.setTooltip ("Settings: library folder, accent colors, keyboard, MIDI Learn");
    settingsButton.onClick = [this] { showSettingsMenu(); };
    addAndMakeVisible (settingsButton);

    keyboard.setKeyWidth (28.0f);
    keyboard.setAvailableRange (21, 108);        // A0..C8
    keyboard.setLowestVisibleKey (48);           // opens around C3
    keyboard.setWantsKeyboardFocus (true);       // computer-keyboard (QWERTY) playing
    addChildComponent (keyboard);                // visibility follows keyboardVisible
    keyboardVisible = (bool) processor.getAPVTS().state.getProperty ("uiKeyboardVisible", false);
    keyboard.setVisible (keyboardVisible);

    keyboardButton.setTooltip ("Show or hide the on-screen keyboard");
    keyboardButton.onClick = [this] { setKeyboardVisible (! keyboardVisible); };
    keyboardButton.setToggleState (keyboardVisible, juce::dontSendNotification);
    addAndMakeVisible (keyboardButton);

    panicButton.setTooltip ("Panic: stop all sound and clear stuck notes");
    panicButton.onClick = [this] { processor.panic(); };
    addAndMakeVisible (panicButton);

    // The standalone has no host tempo, so it gets a tempo bar (internal BPM /
    // tap / external MIDI clock). The plugin follows the host, so no bar.
    if (processor.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
    {
        tempoBar = std::make_unique<TempoBar> (processor);
        addAndMakeVisible (*tempoBar);
    }

    prevPresetButton.setComponentID ("navPrev");   // drawn as a left chevron
    prevPresetButton.setTooltip ("Previous preset");
    prevPresetButton.onClick = [this] { processor.getPresetManager().loadPrevious(); };
    addAndMakeVisible (prevPresetButton);
    nextPresetButton.setComponentID ("navNext");   // drawn as a right chevron
    nextPresetButton.setTooltip ("Next preset");
    nextPresetButton.onClick = [this] { processor.getPresetManager().loadNext(); };
    addAndMakeVisible (nextPresetButton);
    presetNameButton.onClick = [this] { togglePresetBrowser(); };
    presetNameButton.setTooltip ("Browse presets");
    addAndMakeVisible (presetNameButton);
    savePresetButton.onClick = [this] { saveUserPreset(); };
    addAndMakeVisible (savePresetButton);

    randomizeButton.setComponentID ("primary");
    randomizeButton.onClick = [this] { processor.randomizeAll(); };
    addAndMakeVisible (randomizeButton);

    wildnessSlider.setComponentID ("wild");   // value ring heats teal -> red with amount
    wildnessSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    wildnessSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    wildnessSlider.setRange (0.0, 1.0, 0.0);
    wildnessSlider.setValue (processor.getRandomWildness(), juce::dontSendNotification);
    wildnessSlider.onValueChange = [this]
    {
        processor.setRandomWildness ((float) wildnessSlider.getValue());
        if (wildnessSlider.isMouseButtonDown())
            wildnessLabel.setText (juce::String (juce::roundToInt (
                wildnessSlider.getValue() * 100.0)) + "%", juce::dontSendNotification);
    };
    wildnessSlider.onDragEnd = [this]
    {
        wildnessLabel.setText ("WILD", juce::dontSendNotification);
    };
    wildnessSlider.setTooltip ("Chaos amount: how wild RANDOMIZE ALL rolls");
    addAndMakeVisible (wildnessSlider);

    wildnessLabel.setText ("WILD", juce::dontSendNotification);
    wildnessLabel.setFont (metrics::smallFont());
    wildnessLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (wildnessLabel);

    glideModeBox.setTooltip ("Portamento: Always, or Legato (only while a key is held)");
    glideModeBox.getProperties().set ("paramID", juce::String (params::id::glideMode));
    if (const auto* def = params::find (params::id::glideMode))
        glideModeBox.addItemList (def->choices, 1);
    glideModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        processor.getAPVTS(), params::id::glideMode, glideModeBox);
    addAndMakeVisible (glideModeBox);

    glideSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    glideSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    glideSlider.getProperties().set ("inlineValueSuffix", " ms");
    glideSlider.setTooltip ("Glide time");
    glideSlider.getProperties().set ("paramID", juce::String (params::id::glideTime));
    glideAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.getAPVTS(), params::id::glideTime, glideSlider);
    addAndMakeVisible (glideSlider);

    glideLabel.setText ("GLIDE", juce::dontSendNotification);
    glideLabel.setFont (metrics::smallFont());
    glideLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (glideLabel);

    voiceButton.setButtonText ("VOICE");
    voiceButton.setTooltip ("Voice mode: Poly / Mono / Duo / Paraphonic / Unison");
    voiceButton.onClick = [this]
    {
        auto panel = std::make_unique<VoicePanel> (processor.getAPVTS());
        juce::CallOutBox::launchAsynchronously (std::move (panel),
                                                voiceButton.getScreenBounds(), nullptr);
    };
    addAndMakeVisible (voiceButton);

    accentButton.setTooltip ("Customize the accent colors");
    accentButton.onClick = [this] { showAccentPicker(); };
    addAndMakeVisible (accentButton);

    masterSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    masterSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    masterSlider.getProperties().set ("inlineValueSuffix", " dB");  // LnF chip on drag
    masterSlider.setTooltip ("Master volume");
    masterSlider.getProperties().set ("paramID", juce::String (params::id::masterGain));
    masterAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.getAPVTS(), params::id::masterGain, masterSlider);
    addAndMakeVisible (masterSlider);

    for (int g = 0; g < params::numLockGroups; ++g)
    {
        auto& button = lockButtons[(size_t) g];
        button.setButtonText (params::lockGroupName ((params::LockGroup) g));
        button.setComponentID ("lock");   // draws a padlock in the locked state
        button.setClickingTogglesState (true);
        button.setToggleState (processor.isLockGroupLocked (g), juce::dontSendNotification);
        button.setTooltip ("Lock this section: RANDOMIZE ALL keeps its current settings");
        button.onClick = [this, g]
        {
            processor.setLockGroupLocked (g, lockButtons[(size_t) g].getToggleState());
        };
        addAndMakeVisible (button);

        // Macros have no panel to lock; the enum slot stays (persisted lock
        // masks keep their bit layout) but the button is hidden.
        if ((params::LockGroup) g == params::LockGroup::macros)
            button.setVisible (false);
    }

    for (int s = 0; s < params::numOscSlots; ++s)
    {
        oscStrips[(size_t) s] = std::make_unique<OscStrip> (processor, s);
        addAndMakeVisible (*oscStrips[(size_t) s]);
    }

    const auto tabBg = juce::Colours::transparentBlack;
    envTabs.addTab ("AMP", tabBg, new EnvPanel (processor, "ampEnv", 0), true);
    envTabs.addTab ("ENV 2", tabBg, new EnvPanel (processor, "env2", 1), true);
    envTabs.addTab ("ENV 3", tabBg, new EnvPanel (processor, "env3", 2), true);
    addAndMakeVisible (envTabs);

    for (int i = 0; i < params::numLFOs; ++i)
        lfoTabs.addTab ("LFO " + juce::String (i + 1), tabBg,
                        new LFOPanel (processor, i), true);
    addAndMakeVisible (lfoTabs);

    filterTabs.addTab ("FILTER 1", juce::Colours::transparentBlack,
                       new FilterPanel (processor, 1), true);
    filterTabs.addTab ("FILTER 2", juce::Colours::transparentBlack,
                       new FilterPanel (processor, 2), true);
    addAndMakeVisible (filterTabs);
    addAndMakeVisible (chaosPanel);
    addAndMakeVisible (arpPanel);

    fxTabs.addTab ("DIST", tabBg, new FXPanel (processor.getAPVTS(),
                   FXDisplay::Kind::distortion, params::Section::fxDist, "Distortion"), true);
    fxTabs.addTab ("CHORUS", tabBg, new FXPanel (processor.getAPVTS(),
                   FXDisplay::Kind::chorus, params::Section::fxChorus, "Chorus"), true);
    fxTabs.addTab ("DELAY", tabBg, new FXPanel (processor.getAPVTS(),
                   FXDisplay::Kind::delay, params::Section::fxDelay, "Delay"), true);
    fxTabs.addTab ("REVERB", tabBg, new FXPanel (processor.getAPVTS(),
                   FXDisplay::Kind::reverb, params::Section::fxReverb, "Reverb"), true);
    fxTabs.addTab ("EQ", tabBg, new EqEditor (processor.getAPVTS(), processor.getTelemetry(),
                   [&proc = processor] { return proc.getSampleRate(); }), true);
    fxTabs.addTab ("MOD", tabBg, new FXPanel (processor.getAPVTS(),
                   FXDisplay::Kind::chorus, params::Section::fxMod, "Modulation"), true);
    fxTabs.addTab ("TREM/VIB", tabBg, new FXPanel (processor.getAPVTS(),
                   FXDisplay::Kind::chorus, params::Section::fxTremVib, "Trem / Vib"), true);
    fxTabs.addTab ("LIMIT", tabBg,
                   new LimiterPanel (processor.getAPVTS(), processor.getTelemetry()), true);
    fxTabs.addTab ("CONV", tabBg, new ConvolvePanel (processor), true);
    addAndMakeVisible (fxTabs);

    // Tab names by FXChain::Module id (DIST=0 .. LIMIT=7, CONV=8). Drag reorders
    // the tabs and the FX chain together; restore the saved order.
    fxTabs.setModuleNames ({ "DIST", "CHORUS", "DELAY", "REVERB", "EQ",
                             "MOD", "TREM/VIB", "LIMIT", "CONV" });
    fxTabs.applyOrder (processor.getFxOrder());
    fxTabs.onOrderChanged = [this] { processor.setFxOrder (fxTabs.currentOrder()); };

    addAndMakeVisible (matrixPanel);
    addAndMakeVisible (outputMeter);

    // Added last so the drawer slides over every module.
    presetBrowser = std::make_unique<PresetBrowser> (
        processor,
        [this] { togglePresetBrowser(); },
        [this] { chooseLibraryFolder(); });
    addChildComponent (*presetBrowser);

    processor.addChangeListener (this);
    processor.getPresetManager().addChangeListener (this);
    refreshAll();

    // Right-clicks anywhere inside get routed here for MIDI Learn.
    addMouseListener (this, true);

    setSize (metrics::baseWidth, getContentBaseHeight());
}

int ContentComponent::getContentBaseHeight() const
{
    return metrics::baseHeight
         + (keyboardVisible ? metrics::keyboardStripHeight : 0);
}

void ContentComponent::mouseDown (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu())
        return;

    // Find the parameter under the click (the control or an ancestor).
    juce::String paramID;
    for (auto* c = e.eventComponent; c != nullptr && c != this; c = c->getParentComponent())
    {
        const auto value = c->getProperties()["paramID"];
        if (! value.isVoid())
        {
            paramID = value.toString();
            break;
        }
    }

    if (paramID.isEmpty())
        return;

    auto& learn = processor.getMidiLearn();
    auto* param = processor.getAPVTS().getParameter (paramID);
    const auto assignedCC = learn.getAssignedCC (paramID);
    const auto armedHere = learn.isArmed() && learn.getArmedParamID() == paramID;

    juce::PopupMenu menu;
    menu.addSectionHeader (param != nullptr ? param->getName (48) : paramID);

    if (armedHere)
        menu.addItem (3, "Cancel MIDI Learn (move a hardware control...)");
    else
        menu.addItem (1, "MIDI Learn");

    if (assignedCC >= 0)
        menu.addItem (2, "Remove assignment (CC " + juce::String (assignedCC) + ")");

    juce::Component::SafePointer<ContentComponent> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(),
                        [safe, paramID] (int result)
    {
        if (safe == nullptr)
            return;
        auto& midiLearn = safe->processor.getMidiLearn();
        if (result == 1)
            midiLearn.armLearn (paramID);
        else if (result == 2)
            midiLearn.clearAssignment (paramID);
        else if (result == 3)
            midiLearn.cancelLearn();
    });
}

ContentComponent::~ContentComponent()
{
    processor.getPresetManager().removeChangeListener (this);
    processor.removeChangeListener (this);
}

void ContentComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshAll();
}

void ContentComponent::refreshAll()
{
    // Keep the FX tab order in sync with the processor (e.g. after RANDOMIZE ALL
    // shuffles the chain). No-op when it already matches.
    fxTabs.applyOrder (processor.getFxOrder());

    const auto& t = currentTheme();

    wildnessLabel.setColour (juce::Label::textColourId, t.textSecondary);
    glideLabel.setColour (juce::Label::textColourId, t.textSecondary);
    randomizeButton.setColour (juce::TextButton::buttonColourId, t.accent);
    randomizeButton.setColour (juce::TextButton::textColourOffId, t.display);

    const auto presetName = processor.getPresetManager().getCurrentName();
    presetNameButton.setButtonText (
        presetName == "Init" && ! library::findLibraryRoot().isDirectory()
            ? "Set library folder..." : presetName);

    if (presetBrowser != nullptr)
        presetBrowser->refresh();   // theme colours

    licenseLine = library::getLicenseLine();

    repaint();
}

void ContentComponent::paint (juce::Graphics& g)
{
    const auto& t = currentTheme();
    g.fillAll (t.background);

    // Brand band: the big tracked wordmark, centred (per the redesign mock).
    auto band = getLocalBounds().removeFromTop (metrics::brandBandHeight);
    g.setColour (t.header.darker (0.25f));
    g.fillRect (band);
    // Tracked text carries trailing kern space, so plain centred drawText
    // shifts the visible glyphs off-centre (worse the bigger the tracking).
    // GlyphArrangement's bounding box is advance-based and inherits the same
    // phantom tail, so measure the true ink instead: render the glyphs to a
    // path and centre the path's bounds. (Measured on the 1380px snapshot:
    // advance-based centring left the wordmark 5.5px left of centre and the
    // two lines 3.5px out of agreement with each other.)
    const auto drawTrackedCentred = [&g] (const juce::Font& font, const juce::String& text,
                                          juce::Rectangle<int> area, juce::Colour colour)
    {
        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText (font, text, 0.0f, 0.0f);

        juce::Path ink;
        glyphs.createPath (ink);
        const auto box = ink.getBounds();
        ink.applyTransform (juce::AffineTransform::translation (
            (float) area.getCentreX() - box.getCentreX(),
            (float) area.getCentreY() - box.getCentreY()));

        g.setColour (colour);
        g.fillPath (ink);
    };

    // The brand band is always dark, so its ink is always light.
    drawTrackedCentred (metrics::wordmarkFont(), "SPASYNTH",
                        band.withTrimmedBottom (9), juce::Colour (0xffe7ecef));
    drawTrackedCentred (metrics::brandSubFont(), "SILVERPLATTER AUDIO",
                        band.withTrimmedTop (21), juce::Colour (0xff7f8d97));

    // Header strip.
    auto header = getLocalBounds().withTrimmedTop (metrics::brandBandHeight)
                      .removeFromTop (metrics::headerHeight);
    g.setColour (t.header);
    g.fillRect (header);

    if (logoDark != nullptr)
        logoDark->drawWithin (g, header.removeFromLeft (52).reduced (10).toFloat(),
                              juce::RectanglePlacement::centred, 1.0f);

    // Footer strip.
    auto footer = getLocalBounds().removeFromBottom (metrics::footerHeight);
    g.setColour (t.header);
    g.fillRect (footer);
    g.setColour (t.textSecondary);
    g.setFont (metrics::smallFont());
    // The ownership stamp takes the brand echo's spot when a license.txt is
    // installed (informational only — nothing is ever gated on it).
    g.drawText (licenseLine.isNotEmpty()
                    ? licenseLine
                    : juce::String::fromUTF8 ("SPASYNTH  \xc2\xb7  SILVERPLATTER AUDIO"),
                footer.reduced (10, 0).withTrimmedRight (52),   // clear the keyboard button + grip
                juce::Justification::centredRight);
    g.drawText ("v" SPASYNTH_VERSION, footer.reduced (10, 0), juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xffe7ecef).withAlpha (0.8f));
    g.setFont (metrics::labelFont());
    g.drawText ("SPASynth", footer, juce::Justification::centred);

    // Caption for the randomizer lock strip.
    auto lockCaption = getLocalBounds()
                           .withTrimmedTop (metrics::brandBandHeight + metrics::headerHeight)
                           .removeFromTop (metrics::lockRowHeight)
                           .reduced (metrics::unit, 0).removeFromLeft (44);
    g.setColour (t.textSecondary);
    g.setFont (metrics::smallFont());
    g.drawText ("LOCKS", lockCaption, juce::Justification::centredLeft);
}

void ContentComponent::resized()
{
    auto bounds = getLocalBounds();
    auto brandBand = bounds.removeFromTop (metrics::brandBandHeight);
    if (tempoBar != nullptr)   // standalone tempo bar, top-left of the brand band
        tempoBar->setBounds (brandBand.removeFromLeft (188).reduced (8, 5));

    // --- Header -------------------------------------------------------------
    auto header = bounds.removeFromTop (metrics::headerHeight);
    settingsButton.setBounds (header.removeFromLeft (52));  // logo doubles as menu

    auto right = header.removeFromRight (524).reduced (0, 9);
    right.removeFromRight (12);   // padding so the meter clears the window edge
    outputMeter.setBounds (right.removeFromRight (14).reduced (0, 2));
    right.removeFromRight (4);
    masterSlider.setBounds (right.removeFromRight (40));
    right.removeFromRight (4);
    panicButton.setBounds (right.removeFromRight (22).reduced (0, 5));   // by the meter
    accentButton.setBounds (right.removeFromRight (32).reduced (2, 6));
    // Glide: knob first (left), then the behavior dropdown (right).
    glideModeBox.setBounds (right.removeFromRight (66).reduced (0, 5));
    auto glideArea = right.removeFromRight (44);
    glideLabel.setBounds (glideArea.removeFromBottom (11));
    glideSlider.setBounds (glideArea);
    right.removeFromRight (6);
    voiceButton.setBounds (right.removeFromRight (46).reduced (0, 5));
    right.removeFromRight (6);
    // WILD sits right beside RANDOMIZE ALL — it shapes what the button rolls.
    auto wildArea = right.removeFromRight (44);
    wildnessLabel.setBounds (wildArea.removeFromBottom (11));
    wildnessSlider.setBounds (wildArea);
    randomizeButton.setBounds (right.reduced (4, 3));

    auto presetArea = header.reduced (metrics::unit, 12);
    prevPresetButton.setBounds (presetArea.removeFromLeft (26));
    savePresetButton.setBounds (presetArea.removeFromRight (52));
    presetArea.removeFromRight (8);   // breathing room between > and SAVE
    nextPresetButton.setBounds (presetArea.removeFromRight (26));
    presetNameButton.setBounds (presetArea.reduced (3, 0));

    // --- Lock strip -----------------------------------------------------------
    auto lockRow = bounds.removeFromTop (metrics::lockRowHeight).reduced (metrics::unit, 2);
    lockRow.removeFromLeft (46);  // "LOCKS" caption painted behind
    int visibleLocks = 0;
    for (auto& button : lockButtons)
        visibleLocks += button.isVisible() ? 1 : 0;
    const auto lockWidth = lockRow.getWidth() / juce::jmax (1, visibleLocks);
    for (auto& button : lockButtons)
        if (button.isVisible())
            button.setBounds (lockRow.removeFromLeft (lockWidth).reduced (2, 1));

    bounds.removeFromBottom (metrics::footerHeight);

    // Keyboard toggle button: bottom-right of the footer (Kontakt-style), kept
    // clear of the window resize grip that sits in the very corner.
    {
        auto footerRow = getLocalBounds().removeFromBottom (metrics::footerHeight);
        footerRow.removeFromRight (24);   // clearance for the corner resize grip
        keyboardButton.setBounds (footerRow.removeFromRight (26).reduced (4, 5));
    }

    // On-screen keyboard sits just above the footer. The base height grows by
    // exactly this strip when shown, so the module grid below is unchanged.
    if (keyboardVisible)
        keyboard.setBounds (bounds.removeFromBottom (metrics::keyboardStripHeight)
                                .reduced (metrics::unit, 6));

    // --- Module grid ----------------------------------------------------------
    auto main = bounds.reduced (metrics::unit, 4);
    constexpr int gap = 6;

    // Row 1: three oscillators + filter.
    auto row1 = main.removeFromTop (juce::roundToInt ((float) main.getHeight() * 0.40f));
    const auto oscW = (row1.getWidth() - 3 * gap) * 26 / 100;
    for (int s = 0; s < params::numOscSlots; ++s)
    {
        oscStrips[(size_t) s]->setBounds (row1.removeFromLeft (oscW));
        row1.removeFromLeft (gap);
    }
    filterTabs.setBounds (row1);
    main.removeFromTop (gap);

    // Row 2: envelopes, LFOs, chaos, macros.
    auto row2 = main.removeFromTop (juce::roundToInt ((float) main.getHeight() * 0.48f));
    envTabs.setBounds (row2.removeFromLeft (row2.getWidth() * 22 / 100));
    row2.removeFromLeft (gap);
    lfoTabs.setBounds (row2.removeFromLeft (row2.getWidth() * 28 / 100));
    row2.removeFromLeft (gap);
    chaosPanel.setBounds (row2.removeFromLeft (row2.getWidth() * 52 / 100));
    row2.removeFromLeft (gap);
    arpPanel.setBounds (row2);
    main.removeFromTop (gap);

    // Row 3: FX + matrix.
    auto row3 = main;
    fxTabs.setBounds (row3.removeFromLeft (row3.getWidth() * 44 / 100));
    row3.removeFromLeft (gap);
    matrixPanel.setBounds (row3);

    // --- Preset drawer (overlay, left) ----------------------------------------
    const auto drawerArea = getLocalBounds()
                                .withTrimmedTop (metrics::brandBandHeight + metrics::headerHeight)
                                .withTrimmedBottom (metrics::footerHeight
                                    + (keyboardVisible ? metrics::keyboardStripHeight : 0))
                                .removeFromLeft (320);
    presetBrowser->setOpenBounds (drawerArea);
    presetBrowser->setBounds (presetBrowserOpen
                                  ? drawerArea
                                  : drawerArea.translated (-drawerArea.getWidth() - 12, 0));
}

void ContentComponent::showAccentPicker()
{
    auto picker = std::make_unique<AccentPicker> ([this]
    {
        if (onThemeChanged)
            onThemeChanged();
    });

    // Parent to the editor shell (outside this component's scale transform).
    if (auto* top = getTopLevelComponent())
        juce::CallOutBox::launchAsynchronously (
            std::move (picker),
            top->getLocalArea (&accentButton, accentButton.getLocalBounds()),
            top);
}

void ContentComponent::showSettingsMenu()
{
    juce::Component::SafePointer<ContentComponent> safe (this);
    juce::PopupMenu m;
    m.addSectionHeader ("SPASynth v" SPASYNTH_VERSION);
    m.addItem ("Set Library Folder...", [safe] { if (safe != nullptr) safe->chooseLibraryFolder(); });
    m.addItem ("Rescan Library", [safe] { if (safe != nullptr) safe->rescanLibrary(); });
    m.addSeparator();
    m.addItem ("Accent Colors...", [safe] { if (safe != nullptr) safe->showAccentPicker(); });
    m.addItem ("Show Keyboard", true, keyboardVisible,
               [safe] { if (safe != nullptr) safe->setKeyboardVisible (! safe->keyboardVisible); });
    m.addSeparator();

    // Whole-synth oversampling (Off / 2x / 4x / 8x). Off by default; higher
    // settings cost CPU but reduce aliasing on bright/nonlinear patches.
    if (auto* osParam = processor.getAPVTS().getParameter (params::id::oversampling))
    {
        const int current = (int) osParam->convertFrom0to1 (osParam->getValue());
        juce::PopupMenu os;
        const char* names[] = { "Off", "2x", "4x", "8x" };
        for (int i = 0; i < 4; ++i)
            os.addItem (names[i], true, current == i, [osParam, i]
            {
                osParam->setValueNotifyingHost (osParam->convertTo0to1 ((float) i));
            });
        m.addSubMenu ("Oversampling", os);
    }

    m.addSeparator();
    m.addItem ("Reset to Default", [safe] { if (safe != nullptr) safe->processor.getPresetManager().resetToDefault(); });
    m.addItem ("Clear All MIDI Learn", [safe] { if (safe != nullptr) safe->processor.getMidiLearn().clearAll(); });

    m.showMenuAsync (juce::PopupMenu::Options()
                         .withTargetComponent (&settingsButton)
                         .withMinimumWidth (190));
}

void ContentComponent::setKeyboardVisible (bool shouldShow)
{
    if (keyboardVisible == shouldShow)
        return;

    keyboardVisible = shouldShow;
    keyboard.setVisible (keyboardVisible);
    keyboardButton.setToggleState (keyboardVisible, juce::dontSendNotification);
    processor.getAPVTS().state.setProperty ("uiKeyboardVisible", keyboardVisible, nullptr);

    // Grow/shrink our base height; this re-lays-out our children (below), then
    // the shell resizes the window to the new aspect ratio.
    setSize (metrics::baseWidth, getContentBaseHeight());
    if (onKeyboardToggled)
        onKeyboardToggled();
    if (keyboardVisible)
        keyboard.grabKeyboardFocus();   // enable QWERTY playing right away
}

void ContentComponent::togglePresetBrowser()
{
    presetBrowserOpen = ! presetBrowserOpen;

    const auto open = presetBrowser->getOpenBounds();
    const auto closed = open.translated (-open.getWidth() - 12, 0);

    presetBrowser->setVisible (true);
    presetBrowser->toFront (false);
    juce::Desktop::getInstance().getAnimator().animateComponent (
        presetBrowser.get(), presetBrowserOpen ? open : closed,
        1.0f, 170, false, 1.0, 0.7);

    if (presetBrowserOpen)
        presetBrowser->grabKeyboardFocus();   // Esc closes
}

void ContentComponent::rescanLibrary()
{
    if (processor.refreshLibrary())
    {
        refreshAll();
        return;
    }

    // Most common cause: the configured library folder lived on a drive
    // that's no longer connected. Send the user straight to the picker
    // instead of leaving Rescan looking like it silently did nothing.
    juce::NativeMessageBox::showOkCancelBox (
        juce::MessageBoxIconType::WarningIcon,
        "Library Not Found",
        "The library folder could not be found. It may be on a drive that's "
        "no longer connected.\n\nChoose a new library folder now?",
        this,
        juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<ContentComponent> (this)] (int result)
        {
            if (result != 0 && safe != nullptr)
                safe->chooseLibraryFolder();
        }));
}

void ContentComponent::chooseLibraryFolder()
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Locate the Silverplatter Audio library folder",
        juce::File::getSpecialLocation (juce::File::userHomeDirectory));

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                            | juce::FileBrowserComponent::canSelectDirectories,
                              [this] (const juce::FileChooser& fc)
    {
        const auto folder = fc.getResult();
        if (! folder.isDirectory())
            return;

        library::setLibraryRoot (folder);
        processor.refreshLibrary();
        refreshAll();
    });
}

void ContentComponent::saveUserPreset()
{
    auto& pm = processor.getPresetManager();
    pm.getUserPresetFolder().createDirectory();

    fileChooser = std::make_unique<juce::FileChooser> (
        "Save preset",
        pm.getUserPresetFolder().getChildFile ("My Preset"
            + juce::String (library::PresetManager::presetExtension)),
        "*" + juce::String (library::PresetManager::presetExtension));

    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode
                            | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
    {
        if (fc.getResult() != juce::File())
            processor.getPresetManager().saveUserPreset (
                fc.getResult().getFileNameWithoutExtension());
    });
}

} // namespace ui

// ============================ SPASynthEditor ================================

SPASynthEditor::SPASynthEditor (SPASynthProcessor& p)
    : juce::AudioProcessorEditor (p), arsenalProcessor (p)
{
    setLookAndFeel (&lookAndFeel);

    content = std::make_unique<ui::ContentComponent> (p, [this] { applyTheme(); });
    content->onKeyboardToggled = [this] { keyboardToggled(); };
    addAndMakeVisible (*content);

    constexpr auto baseW = ui::metrics::baseWidth;
    // Base height includes the keyboard strip if it was left open last session.
    const auto baseH = content->getContentBaseHeight();

    setResizable (true, true);
    configureConstrainer();

    // Restore the remembered window scale, clamped so the whole editor
    // (including the resize corner) always fits the host's screen — on small
    // displays the default must shrink, never open with edges off-screen.
    auto maxScale = 2.0;
    if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
    {
        const auto usable = display->userBounds;
        maxScale = juce::jmin (
            2.0,
            (double) (usable.getWidth() - 40) / baseW,
            (double) (usable.getHeight() - 140) / baseH);   // headroom for DAW chrome
    }

    const auto saved = (double) arsenalProcessor.getAPVTS().state.getProperty ("uiScale", 1.0);
    const auto scale = juce::jlimit (0.4, juce::jmax (0.4, maxScale), saved);
    setSize (juce::roundToInt (baseW * scale), juce::roundToInt (baseH * scale));
}

// Fixes the window aspect ratio and size limits to the content's current base
// size (which grows/shrinks with the keyboard strip).
void SPASynthEditor::configureConstrainer()
{
    if (auto* constrainer = getConstrainer())
    {
        constexpr auto baseW = ui::metrics::baseWidth;
        const auto baseH = content->getContentBaseHeight();
        constrainer->setFixedAspectRatio ((double) baseW / baseH);
        constrainer->setSizeLimits (baseW * 40 / 100, baseH * 40 / 100,
                                    baseW * 2, baseH * 2);
    }
}

// Keyboard shown/hidden: re-fix the aspect and resize the window to match at
// the current width, so the modules keep their size and the strip is added.
void SPASynthEditor::keyboardToggled()
{
    configureConstrainer();
    constexpr auto baseW = ui::metrics::baseWidth;
    const auto baseH = content->getContentBaseHeight();
    setSize (getWidth(), juce::roundToInt ((float) getWidth() * (float) baseH / (float) baseW));
}

SPASynthEditor::~SPASynthEditor()
{
    setLookAndFeel (nullptr);
}

void SPASynthEditor::applyTheme()
{
    lookAndFeel.refreshPalette();
    sendLookAndFeelChange();
    content->refreshAll();
    repaint();
}

void SPASynthEditor::resized()
{
    if (content == nullptr)
        return;

    const auto scale = (float) getWidth() / (float) ui::metrics::baseWidth;
    content->setTransform (juce::AffineTransform::scale (scale));
    content->setTopLeftPosition (0, 0);

    arsenalProcessor.getAPVTS().state.setProperty ("uiScale", (double) scale, nullptr);
}

void SPASynthEditor::parentHierarchyChanged()
{
   #if JUCE_MAC
    // macOS Tahoe's AUHostingService (Logic/GarageBand only — REAPER and the
    // standalone are fine) can open the editor with a stale hit-test region:
    // parts of the UI, typically the top strip, receive no mouse events until
    // the host renegotiates the view — users see the preset button dead until
    // they wiggle any knob. Known Apple bug, documented on the JUCE forum for
    // Tahoe 26.0-26.5 with no upstream fix as of JUCE 8.0.14 (hits non-JUCE
    // plugins too). Once the editor is actually on screen, nudge the view
    // size by one pixel and back — the resize round-trip makes the hosting
    // service rebuild its geometry — and repaint fully afterwards. A resize
    // is automation-safe, unlike faking a parameter gesture.
    if (hostViewWakeupDone
        || arsenalProcessor.wrapperType != juce::AudioProcessor::wrapperType_AudioUnit
        || ! isShowing())
        return;

    hostViewWakeupDone = true;

    juce::Component::SafePointer<SPASynthEditor> safe (this);
    juce::Timer::callAfterDelay (150, [safe]
    {
        if (safe == nullptr)
            return;

        const auto w = safe->getWidth(), h = safe->getHeight();
        safe->setSize (w + 1, h);

        juce::Timer::callAfterDelay (60, [safe, w, h]
        {
            if (safe == nullptr)
                return;

            safe->setSize (w, h);
            safe->repaint();
        });
    });
   #endif
}

} // namespace spa
