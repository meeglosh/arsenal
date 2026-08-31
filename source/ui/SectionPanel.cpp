#include "SectionPanel.h"
#include "Controls.h"

namespace spa::ui
{

namespace
{
    // Strip the slot/LFO prefix registry names carry for host lists — the
    // panel title already gives that context.
    juce::String displayName (const params::ParamDef& def)
    {
        auto name = def.name;
        for (const char* prefix : { "A ", "B ", "C ", "L1 ", "L2 ", "L3 " })
            if (name.startsWith (prefix))
                return name.fromFirstOccurrenceOf (" ", false, false);
        return name;
    }
}

SectionPanel::SectionPanel (juce::AudioProcessorValueTreeState& apvts,
                            params::Section section,
                            const juce::String& title,
                            const juce::StringArray& excludeIDs,
                            bool drawFrame)
    : cellHeight (drawFrame ? 72 : 60),
      panelTitle (title.isNotEmpty() ? title : params::sectionName (section)),
      framed (drawFrame)
{
    for (const auto& def : params::all())
    {
        if (def.section != section || excludeIDs.contains (def.id))
            continue;

        Control control;

        control.label = std::make_unique<juce::Label>();
        control.label->setText (displayName (def).toUpperCase(), juce::dontSendNotification);
        control.label->setFont (metrics::smallFont());
        control.label->setJustificationType (juce::Justification::centred);
        control.label->setInterceptsMouseClicks (false, false);

        switch (def.kind)
        {
            case params::ParamKind::boolParam:
            {
                auto toggle = std::make_unique<juce::ToggleButton> (displayName (def).toUpperCase());
                control.buttonAttachment =
                    std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                        apvts, def.id, *toggle);
                control.component = std::move (toggle);
                control.label = nullptr;  // toggle draws its own text
                control.wide = true;
                break;
            }
            case params::ParamKind::choiceParam:
            {
                auto combo = std::make_unique<juce::ComboBox>();
                combo->addItemList (def.choices, 1);
                control.comboAttachment =
                    std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
                        apvts, def.id, *combo);
                control.component = std::move (combo);
                control.wide = true;
                break;
            }
            case params::ParamKind::intParam:
            case params::ParamKind::floatParam:
            {
                auto knob = std::make_unique<juce::Slider> (
                    juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox);
                control.sliderAttachment =
                    std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                        apvts, def.id, *knob);
                control.label->setMinimumHorizontalScale (0.6f);
                Knob::wireDragReadout (*knob, *control.label, apvts.getParameter (def.id),
                                       control.label->getText(), false);
                control.component = std::move (knob);
                break;
            }
        }

        control.component->getProperties().set ("paramID", def.id);
        addAndMakeVisible (*control.component);
        if (control.label != nullptr)
            addAndMakeVisible (*control.label);

        // Unlike Controls.h's Knob/Choice/Toggle, these controls are raw
        // JUCE widgets built straight off the parameter registry, so they
        // never picked up the click-grabs-keyboard-focus fix (CLAUDE.md's
        // 1.0.8/1.0.10 notes) -- every ToggleButton and ComboBox this loop
        // builds, for every registry-driven section in the app, was still
        // capable of stealing focus from the on-screen keyboard. Sweep the
        // whole control (covers a ComboBox's internal Label too) and its
        // caption label. disableMouseClickFocusGrab is the shared helper
        // in Controls.h.
        disableMouseClickFocusGrab (*control.component);
        if (control.label != nullptr)
            disableMouseClickFocusGrab (*control.label);

        controls.push_back (std::move (control));
    }
}

void SectionPanel::paint (juce::Graphics& g)
{
    if (! framed)
        return;

    draw::panel (g, getLocalBounds().toFloat());
    draw::sectionHeader (g, getLocalBounds(), panelTitle, {}, currentTheme().accent);
}

int SectionPanel::heightForWidth (int width) const
{
    const auto columns = juce::jmax (1, (width - 12) / cellWidth);
    int cellsUsed = 0;
    for (const auto& c : controls)
        cellsUsed += c.wide ? 2 : 1;
    const auto rows = (cellsUsed + columns - 1) / columns;
    return (framed ? headerHeight : 0) + rows * cellHeight + 8;
}

void SectionPanel::resized()
{
    const auto area = getLocalBounds().withTrimmedTop (framed ? headerHeight : 0).reduced (6, 0);
    const auto columns = juce::jmax (1, area.getWidth() / cellWidth);

    int cellsUsed = 0;
    for (const auto& c : controls)
        cellsUsed += c.wide ? 2 : 1;
    const auto rows = juce::jmax (1, (cellsUsed + columns - 1) / columns);

    // Caption labels get their full height reserved first, always -- never
    // less than this, whatever else happens. The knob/control area is what
    // actually adapts: rowHeight normally matches cellHeight (the "ideal"
    // heightForWidth() spacing above), but shrinks -- down to just enough
    // for the label -- when this panel was given less real height than
    // that (e.g. FXPanel already collapsed its display to nothing and a
    // section still has more rows than fit at full cellHeight). This is
    // what actually keeps captions from clipping off the bottom on a
    // control-heavy tab like TREM/VIB: the caller (FXPanel) shrinks the
    // display as its first line of defense, but for sections with enough
    // rows even that isn't enough, so the row height itself must adapt too.
    const auto labelH = framed ? 16 : 13;
    const auto rowHeight = juce::jlimit (labelH, cellHeight, area.getHeight() / rows);

    int cell = 0;
    for (auto& control : controls)
    {
        const auto span = control.wide ? 2 : 1;
        // Wrap early if a wide control would split across rows.
        if ((cell % columns) + span > columns)
            cell += columns - (cell % columns);

        const auto col = cell % columns;
        const auto row = cell / columns;
        auto cellBounds = juce::Rectangle<int> (area.getX() + col * cellWidth,
                                                area.getY() + row * rowHeight,
                                                cellWidth * span, rowHeight);
        cell += span;

        if (control.label != nullptr)
        {
            control.label->setBounds (cellBounds.removeFromBottom (labelH));
            control.component->setBounds (cellBounds.reduced (framed ? 4 : 2));
        }
        else if (dynamic_cast<juce::ComboBox*> (control.component.get()) != nullptr)
        {
            control.component->setBounds (cellBounds.withSizeKeepingCentre (
                cellBounds.getWidth() - 10, juce::jmin (24, cellBounds.getHeight())));
        }
        else  // toggle
        {
            control.component->setBounds (cellBounds.withSizeKeepingCentre (
                cellBounds.getWidth() - 10, juce::jmin (22, cellBounds.getHeight())));
        }
    }
}

std::vector<juce::Component*> SectionPanel::findControlComponents (const juce::String& paramID) const
{
    for (auto& c : controls)
    {
        if (c.component->getProperties()["paramID"].toString() != paramID)
            continue;

        std::vector<juce::Component*> result { c.component.get() };
        if (c.label != nullptr)
            result.push_back (c.label.get());
        return result;
    }
    return {};
}

} // namespace spa::ui
