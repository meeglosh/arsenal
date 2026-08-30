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
    std::function<void()> onKeyboardToggled;   // shell re-sizes when this fires

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void togglePresetBrowser();
    void showAccentPicker();
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
    MatrixPanel matrixPanel;
    OutputMeter outputMeter;

    // Preset drawer: slides over the left side of the module grid.
    std::unique_ptr<PresetBrowser> presetBrowser;
    bool presetBrowserOpen = false;

    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::String licenseLine;   // footer ownership stamp (refreshed with the library)

    // Faceplate restyle: geometry captured in resized() so paint() can draw
    // the recessed vertical seams + horizontal shadow bands from the live
    // module grid rather than hardcoded pixel positions.
    std::array<int, 4> rowShadowYs {};               // y of each row-transition shadow band
    std::vector<juce::Rectangle<int>> moduleGutters; // gap rects between adjacent modules in a row
    juce::Image noiseTexture;                        // cached fine-grain texture tile (seeded once)

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

    SPASynthProcessor& arsenalProcessor;
    ui::SPASynthLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this };
    std::unique_ptr<ui::ContentComponent> content;
    bool hostViewWakeupDone = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SPASynthEditor)
};

} // namespace spa
