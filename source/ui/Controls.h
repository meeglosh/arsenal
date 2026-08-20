#pragma once

#include "Theme.h"
#include "../params/ParameterRegistry.h"

namespace spa::ui
{

// A thin-ring knob with its label underneath — the atomic control of the UI.
// Set modColoured for modulation-domain knobs (cyan ring).
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramID,
          const juce::String& labelText, bool modColoured = false)
        : parameter (apvts.getParameter (paramID)),
          restingText (labelText.toUpperCase()),
          modAccent (modColoured)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        // Sliders want keyboard focus by default (arrow-key nudging); losing
        // that is a fair trade for not silently stealing it away from the
        // on-screen keyboard's QWERTY note input every time a knob is touched.
        slider.setWantsKeyboardFocus (false);
        slider.getProperties().set ("paramID", paramID);      // for MIDI Learn
        if (modColoured)
            slider.setComponentID ("mod");
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            apvts, paramID, slider);
        addAndMakeVisible (slider);

        label.setText (restingText, juce::dontSendNotification);
        label.setFont (metrics::smallFont());
        label.setJustificationType (juce::Justification::centred);
        label.setInterceptsMouseClicks (false, false);
        label.setMinimumHorizontalScale (0.6f);
        addAndMakeVisible (label);

        // Press state: the label becomes the live value readout.
        wireDragReadout (slider, label, parameter, restingText, modAccent);
    }

    // Shared wiring: while dragging, `label` shows the parameter's value in
    // the accent colour; on release it reverts. Guarded so programmatic
    // value changes never hijack the label.
    static void wireDragReadout (juce::Slider& s, juce::Label& l,
                                 juce::RangedAudioParameter* param,
                                 const juce::String& restingText, bool modAccent)
    {
        const auto showValue = [&l, param, modAccent]
        {
            if (param == nullptr)
                return;
            const auto& t = currentTheme();
            l.setColour (juce::Label::textColourId, modAccent ? t.accentMod : t.accent);
            auto text = param->getCurrentValueAsText();
            if (param->getLabel().isNotEmpty())
                text << " " << param->getLabel();
            l.setText (text, juce::dontSendNotification);
        };

        s.onDragStart = showValue;
        s.onValueChange = [&s, showValue]
        {
            if (s.isMouseButtonDown())
                showValue();
        };
        s.onDragEnd = [&l, restingText]
        {
            l.removeColour (juce::Label::textColourId);
            l.setText (restingText, juce::dontSendNotification);
        };
    }

    void resized() override
    {
        auto area = getLocalBounds();
        label.setBounds (area.removeFromBottom (13));
        slider.setBounds (area);
    }

    juce::Slider slider;
    juce::Label label;

private:
    juce::RangedAudioParameter* parameter = nullptr;
    juce::String restingText;
    bool modAccent = false;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// Labelled combo box row.
class Choice : public juce::Component
{
public:
    Choice (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramID)
    {
        combo.setWantsKeyboardFocus (false);   // see Knob's comment
        combo.getProperties().set ("paramID", paramID);
        if (const auto* def = params::find (paramID))
            combo.addItemList (def->choices, 1);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
            apvts, paramID, combo);
        addAndMakeVisible (combo);
    }

    void resized() override { combo.setBounds (getLocalBounds()); }

    juce::ComboBox combo;

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};

// Pill toggle bound to a bool parameter.
class Toggle : public juce::Component
{
public:
    Toggle (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramID,
            const juce::String& text)
        : button (text)
    {
        button.setWantsKeyboardFocus (false);   // see Knob's comment
        button.getProperties().set ("paramID", paramID);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            apvts, paramID, button);
        addAndMakeVisible (button);
    }

    void resized() override { button.setBounds (getLocalBounds()); }

    juce::ToggleButton button;

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};

} // namespace spa::ui
