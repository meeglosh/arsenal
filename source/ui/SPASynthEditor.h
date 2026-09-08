#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "../params/ParameterRegistry.h"
#include "../params/Randomizer.h"
#include "SPASynthLookAndFeel.h"
#include "DraggableTabs.h"
#include "ModulePanels.h"
#include "SectionPanel.h"
#include "MatrixPanel.h"
#include "PresetBrowser.h"

namespace spa
{

class SPASynthProcessor;

namespace ui
{

// Everything inside the plugin window at base size; the editor shell scales
// this whole component for resizing.
class ContentComponent : public juce::Component,
                         private juce::ChangeListener
{
public:
    ContentComponent (SPASynthProcessor&, std::function<void()> onThemeChanged);
    ~ContentComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;   // right-click = MIDI Learn
    bool keyPressed (const juce::KeyPress&) override;    // Esc closes the preset browser
                                                          // even when focus stayed on the
                                                          // on-screen keyboard (see .cpp)
    void refreshAll();

    // Base height grows by the keyboard strip when it is shown; the editor
    // shell reads this to drive the window aspect ratio and scale.
    int getContentBaseHeight() const;
    // Base width grows by the preset drawer's column when it is open AND the
    // host let us actually widen the window (see browserOverlays below); the
    // editor shell reads this the same way it reads getContentBaseHeight().
    int getContentBaseWidth() const;
    std::function<void()> onKeyboardToggled;   // shell re-sizes when this fires
    std::function<void()> onBrowserToggled;    // shell re-sizes (width) when this fires

    // The shell calls this if it asked the host to widen the window for a
    // newly opened drawer and the host didn't actually honor it (fixed-size
    // host view): switches to the old overlay-over-the-grid behaviour and
    // shrinks our own base size back down to match (see resized()).
    // Sticky for the life of this editor -- once a host has shown it can't
    // resize us, later opens don't try again.
    void setBrowserOverlayMode (bool shouldOverlay);
    bool isBrowserOverlayMode() const { return browserOverlays; }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void togglePresetBrowser();
    void showAccentPicker();
    juce::Component* callOutParent();
    void showSettingsMenu();
    void setKeyboardVisible (bool shouldShow);
    void chooseLibraryFolder();
    void rescanLibrary();
    void saveUserPreset();

    // Header button showing the two accents as a split circle; clicking
    // drops down the colour picker.
    struct AccentButton : juce::Button
    {
        AccentButton() : juce::Button ("accentColors") {}
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    };

    // The top-left logo doubles as the settings menu button; this overlay adds
    // a hover highlight and opens the menu (the logo itself is painted behind).
    struct SettingsButton : juce::Button
    {
        SettingsButton() : juce::Button ("settings") {}
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    };

    // Bottom-right keyboard-strip toggle (Kontakt-style); draws a small piano
    // icon that lights up in the accent colour while the keyboard is shown.
    struct KeyboardButton : juce::Button
    {
        KeyboardButton() : juce::Button ("keyboard") {}
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    };

    // Header panic button: an alert badge, muted red by default and bright red
    // on hover, so it reads as "the emergency stop".
    struct PanicButton : juce::Button
    {
        PanicButton() : juce::Button ("panic") {}
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    };

    SPASynthProcessor& processor;
    std::function<void()> onThemeChanged;   // LnF palette refresh + repaint

    std::unique_ptr<juce::Drawable> logoDark, logoLight;
    SettingsButton settingsButton;   // over the top-left logo

    // On-screen keyboard strip (toggled from the settings menu or the
    // bottom-right keyboard button).
    juce::MidiKeyboardComponent keyboard;
    KeyboardButton keyboardButton;
    bool keyboardVisible = false;

    PanicButton panicButton;   // header top-right: stop all sound
    std::unique_ptr<juce::Component> tempoBar;   // standalone only (brand band)

    // Header.
    juce::TextButton prevPresetButton { "<" }, nextPresetButton { ">" };
    juce::TextButton presetNameButton, savePresetButton { "SAVE" };
    juce::TextButton randomizeButton { "RANDOMIZE ALL" };
    juce::Slider wildnessSlider;
    juce::Label wildnessLabel;
    juce::ComboBox glideModeBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> glideModeAttachment;
    juce::Slider glideSlider;
    juce::Label glideLabel;
    juce::TextButton voiceButton;   // opens the voice-mode call-out
    // The VOICE call-out's content panel while it is open. JUCE's modal
    // manager owns the call-out and deletes it asynchronously, so it can
    // outlive this editor; the destructor uses this to detach the panel from
    // the processor synchronously (see ~ContentComponent).
    juce::Component::SafePointer<juce::Component> openVoicePanel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> glideAttachment;
    // GLIDE knob only means anything once glideMode is off "Off".
    std::unique_ptr<DependentEnable> glideTimeEnable;
    AccentButton accentButton;
    juce::Slider masterSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> masterAttachment;
    std::array<juce::TextButton, params::numLockGroups> lockButtons;

    // Modules.
    std::array<std::unique_ptr<OscStrip>, params::numOscSlots> oscStrips;
    juce::TabbedComponent filterTabs { juce::TabbedButtonBar::TabsAtTop };
    juce::TabbedComponent envTabs { juce::TabbedButtonBar::TabsAtTop };
    juce::TabbedComponent lfoTabs { juce::TabbedButtonBar::TabsAtTop };
    ChaosPanel chaosPanel;
    ArpPanel arpPanel;
    DraggableTabs fxTabs;   // FX tabs are drag-reorderable -> chain order
    // Bolds a tab's label when its FX is enabled (fxTabs.isTabEngaged).
    std::unique_ptr<TabEngagementTracker> fxTabEngagement;
    MatrixPanel matrixPanel;
    OutputMeter outputMeter;

    // Preset drawer: normally widens the window and sits in a left column of
    // its own, beside (never over) the module grid -- see
    // getContentBaseWidth()/resized(). Falls back to the old overlay-over-
    // the-grid behaviour (browserOverlays) if a host refuses to actually
    // resize the editor for it.
    std::unique_ptr<PresetBrowser> presetBrowser;
    bool presetBrowserOpen = false;
    bool browserOverlays = false;
    // Bumped on every togglePresetBrowser() call; a deferred close-completion
    // callback (see togglePresetBrowser) captures the value current at its
    // own call and checks it still matches before shrinking the window, so a
    // later toggle that supersedes it is a no-op instead of a stale clobber.
    int browserAnimSeq = 0;

    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::String licenseLine;   // footer ownership stamp (refreshed with the library)

    // Faceplate restyle: geometry captured in resized() so paint() can draw
    // the recessed vertical seams + horizontal shadow bands from the live
    // module grid rather than hardcoded pixel positions.
    std::array<int, 4> rowShadowYs {};               // y of each row-transition shadow band
    std::vector<juce::Rectangle<int>> moduleGutters; // gap rects between adjacent modules in a row
                                                      // (x/width only -- paint() stretches each to its
                                                      // full row band via moduleGutterRows/rowShadowYs)
    std::vector<int> moduleGutterRows;               // row index (into rowShadowYs) each gutter belongs to
    int topNavRuleY = 0;                             // bottom edge of the lock-strip (top nav) band --
                                                      // separate from rowShadowYs[0] (which stays the true
                                                      // top of row 1, for the vertical seams) so the top nav
                                                      // row's own recessed-band rule can sit flush against
                                                      // it with no extra shadow doubling up nearby
    juce::Image noiseTexture;                        // cached fine-grain texture tile (seeded once)
    int moduleOriginX = 0;                           // left edge of the module area in resized()/
                                                      // paint() -- 0 normally, presetBrowserWidth
                                                      // when the drawer occupies its own column

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ContentComponent)
};

} // namespace ui

// Shell: hosts the fixed-layout content at base size and scales it
// proportionally — industry-standard plugin resizing. Window scale is
// remembered across sessions.
class SPASynthEditor : public juce::AudioProcessorEditor
{
public:
    explicit SPASynthEditor (SPASynthProcessor&);
    ~SPASynthEditor() override;

    void resized() override;
    void parentHierarchyChanged() override;

private:
    void applyTheme();
    void configureConstrainer();   // aspect + size limits from content base size
    void keyboardToggled();        // resize the shell when the keyboard strip toggles
    void browserToggled();         // resize (width) the shell when the drawer toggles

    SPASynthProcessor& arsenalProcessor;
    ui::SPASynthLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this };
    std::unique_ptr<ui::ContentComponent> content;
    bool hostViewWakeupDone = false;
    // The x delta (logical px, positive = moved left) actually applied by
    // the last successful native-window shift on drawer open -- see
    // NativeWindowShift.h. May be less than the requested width if clamped
    // at a screen edge; the close path undoes exactly this, not the nominal
    // drawer width, so a clamped open/close cycle never creeps the window.
    int nativeWindowShiftApplied = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SPASynthEditor)
};

} // namespace spa
