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
                                                area.getY() + row * cellHeight,
                                                cellWidth * span, cellHeight);
        cell += span;

        if (control.label != nullptr)
        {
            control.label->setBounds (cellBounds.removeFromBottom (framed ? 16 : 13));
            control.component->setBounds (cellBounds.reduced (framed ? 4 : 2));
        }
        else if (dynamic_cast<juce::ComboBox*> (control.component.get()) != nullptr)
        {
            control.component->setBounds (cellBounds.withSizeKeepingCentre (
                cellBounds.getWidth() - 10, 24));
        }
        else  // toggle
        {
            control.component->setBounds (cellBounds.withSizeKeepingCentre (
                cellBounds.getWidth() - 10, 22));
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
