#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace spa::ui
{

// A tab button that draws a grip-dots handle and reorders its bar when dragged.
// The actual move is delegated to the owning DraggableTabs so both the button
// AND its content component move together (the bar's own moveTab would reorder
// only the buttons, desyncing them from the content array).
class DraggableTabButton : public juce::TabBarButton
{
public:
    DraggableTabButton (const juce::String& name, juce::TabbedButtonBar& bar,
                        std::function<void (int from, int to)> mover)
        : juce::TabBarButton (name, bar), onMove (std::move (mover))
    {
        // Don't steal focus from the on-screen keyboard's QWERTY note input
        // when a tab is clicked/dragged to reorder the FX chain -- see
        // Controls.h's Knob for the full explanation.
        setMouseClickGrabsKeyboardFocus (false);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto& bar = getTabbedButtonBar();
        const int idx = getIndex();
        const auto px = bar.getLocalPoint (this, e.position).x;

        int target = idx;   // the tab whose slot the pointer is over
        for (int i = 0; i < bar.getNumTabs(); ++i)
            if (auto* b = bar.getTabButton (i))
                if (px >= (float) b->getX() && px < (float) b->getRight()) { target = i; break; }

        if (target != idx && target >= 0 && onMove)
            onMove (idx, target);
    }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        juce::TabBarButton::paintButton (g, over, down);   // default LnF tab

        // Grip-dots handle at the left, so the drag is discoverable.
        const auto b = getLocalBounds().toFloat();
        const float x = b.getX() + 6.0f;
        const float cy = b.getCentreY();
        constexpr float d = 1.5f, gap = 3.0f;
        g.setColour (juce::Colours::white.withAlpha (over || down ? 0.55f : 0.30f));
        for (int col = 0; col < 2; ++col)
            for (int row = -1; row <= 1; ++row)
                g.fillEllipse (x + (float) col * gap, cy + (float) row * gap - d * 0.5f, d, d);
    }

private:
    std::function<void (int from, int to)> onMove;
};

// TabbedComponent whose tabs can be dragged to reorder. Tabs are identified by
// name; setModuleNames() maps each name (by index) to a stable module id so the
// current order and a saved order can be read/applied as module ids.
class DraggableTabs : public juce::TabbedComponent
{
public:
    DraggableTabs() : juce::TabbedComponent (juce::TabbedButtonBar::TabsAtTop) {}

    std::function<void()> onOrderChanged;   // fires after a drag reorder

    void setModuleNames (juce::StringArray namesByModuleId)
    {
        moduleNames = std::move (namesByModuleId);
    }

    juce::Array<int> currentOrder() const
    {
        juce::Array<int> ids;
        for (const auto& n : getTabNames())
            ids.add (moduleNames.indexOf (n));
        return ids;
    }

    void applyOrder (const juce::Array<int>& ids)
    {
        for (int pos = 0; pos < ids.size(); ++pos)
        {
            if (! juce::isPositiveAndBelow (ids[pos], moduleNames.size()))
                continue;
            const auto name = moduleNames[ids[pos]];
            const int cur = getTabNames().indexOf (name);
            if (cur >= 0 && cur != pos)
                moveTab (cur, pos, false);   // TabbedComponent::moveTab: button + content
        }
    }

    juce::TabBarButton* createTabButton (const juce::String& name, int /*index*/) override
    {
        return new DraggableTabButton (name, getTabbedButtonBar(),
                                       [this] (int from, int to)
                                       {
                                           moveTab (from, to, true);   // button + content
                                           if (onOrderChanged) onOrderChanged();
                                       });
    }

private:
    juce::StringArray moduleNames;   // index = module id
};

} // namespace spa::ui
