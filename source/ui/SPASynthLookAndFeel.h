#pragma once

#include "Theme.h"

namespace spa::ui
{

// Bespoke control rendering: minimal arc knobs, pill toggles, soft rounded
// surfaces. All colours come from the active Theme so light/dark switching
// is a palette refresh.
class SPASynthLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SPASynthLookAndFeel();

    // Re-reads the active theme into the LookAndFeel colour palette.
    void refreshPalette();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider&) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&,
                               const juce::Colour& backgroundColour,
                               bool highlighted, bool down) override;

    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool highlighted, bool down) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool highlighted, bool down) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox&) override;

    // ComboBox rebuilds its internal text Label from scratch on every
    // lookAndFeelChanged() (juce_ComboBox.cpp's lookAndFeelChanged(), which
    // our accent colour picker triggers app-wide via sendLookAndFeelChange()
    // -- see setAccentColors()). A fresh Label defaults to grabbing keyboard
    // focus on click, which would silently reintroduce the QWERTY focus-
    // steal bug (CLAUDE.md's 1.0.8/1.0.10 notes) on every combo box in the
    // app the next time someone changes accents. Fixing it here, once, at
    // the point every ComboBox's Label is created covers all of them --
    // present and future -- instead of needing a listener at every call
    // site.
    juce::Label* createComboBoxTextBox (juce::ComboBox&) override;

    // The preset browser runs 2pt larger than the module grid; its controls
    // opt in via the "browser" (and "chip") componentIDs.
    static juce::Font boosted (juce::Font f) { return f.withHeight (f.getHeight() + 2.0f); }

    juce::Font getComboBoxFont (juce::ComboBox& c) override
    {
        return c.getComponentID() == "browser" ? boosted (metrics::labelFont())
                                               : metrics::labelFont();
    }
    juce::Font getPopupMenuFont() override { return metrics::labelFont(); }
    juce::Font getTextButtonFont (juce::TextButton& b, int) override
    {
        // Filter chips (preset browser) are tighter than regular buttons.
        if (b.getComponentID() == "chip")
            return boosted (metrics::smallFont());
        if (b.getComponentID() == "browser")
            return boosted (metrics::labelFont());
        return metrics::labelFont();
    }
    juce::Font getLabelFont (juce::Label&) override { return metrics::labelFont(); }

    int getTabButtonBestWidth (juce::TabBarButton&, int tabDepth) override;
    void drawTabButton (juce::TabBarButton&, juce::Graphics&,
                        bool isMouseOver, bool isMouseDown) override;
    void drawTabbedButtonBarBackground (juce::TabbedButtonBar&, juce::Graphics&) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SPASynthLookAndFeel)
};

} // namespace spa::ui
