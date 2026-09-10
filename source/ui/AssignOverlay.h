#pragma once

#include "Theme.h"
#include "Controls.h"
#include "../params/ParameterRegistry.h"

namespace spa::ui
{

// ASSIGN mode overlay: a single transparent full-bounds child, added last to
// ContentComponent, that owns every click while assign mode is active.
//
// Targets fall into four kinds:
//  - destination : a control whose paramID is a mod-matrix destination
//                   (params::modDestIndex >= 0), found by walking the tree
//                   for the "paramID" component property.
//  - source      : a visual home for a mod source (LFO/ENV tab buttons, the
//                   Organic Chaos panel), tagged with the int "modSource"
//                   property (a spa::params::ModSource value), or an osc
//                   strip tagged with "oscSlot" (offers a 2-item SFX Amp/
//                   Pitch popup on click instead of a fixed source value).
//  - destMenu    : a matrix row's DEST combo.
//  - sourceMenu  : a matrix row's SOURCE combo.
//
// Clicking a destination/source selects it (solid yellow); clicking a menu
// of the matching kind routes the current selection into that row (source or
// dest, overwriting whatever was there) and OVERWRITES via
// setValueNotifyingHost -- selection persists so the same object can be
// assigned into several rows. Esc or the ASSIGN button turning off clears
// selection and fades the glow out over ~300ms, then stops painting/hit-
// testing entirely.
class AssignOverlay : public juce::Component, private juce::Timer
{
public:
    explicit AssignOverlay (juce::AudioProcessorValueTreeState& apvtsIn) : apvts (apvtsIn)
    {
        setInterceptsMouseClicks (false, false);
        setVisible (false);
        // QWERTY rule (see Controls.h's Knob comment): never let this steal
        // focus from the on-screen keyboard, even though normal use never
        // clicks it directly (the button/menus/targets underneath do).
        setWantsKeyboardFocus (false);
        setMouseClickGrabsKeyboardFocus (false);
    }

    // Test hook: is `c` the current selection (destination or source)?
    bool isSelected (const juce::Component* c) const
    {
        return (selectedDest != nullptr && selectedDest->comp.getComponent() == c)
            || (selectedSource != nullptr && selectedSource->comp.getComponent() == c);
    }

    // `root` is walked to find targets; `excludeInRootSpace` (e.g. the
    // ASSIGN button's own bounds) is excluded from hit-testing so its own
    // click keeps working while the overlay sits on top of it.
    void setAssignMode (bool on, juce::Component& root, juce::Rectangle<int> excludeInRootSpace)
    {
        rootComponent = &root;
        excludeBounds = excludeInRootSpace;

        if (on)
        {
            active = true;
            fadingOut = false;
            fadeAlpha = 1.0f;
            selectedDest = nullptr;
            selectedSource = nullptr;
            rebuildTargets();
            setVisible (true);
            setInterceptsMouseClicks (true, false);
            startTimerHz (30);
        }
        else if (active || fadingOut)
        {
            active = false;
            fadingOut = true;
            fadeStartMs = juce::Time::getMillisecondCounter();
            selectedDest = nullptr;
            selectedSource = nullptr;
            setInterceptsMouseClicks (false, false);
            if (! isTimerRunning())
                startTimerHz (30);
        }
        repaint();
    }

    bool isAssignActive() const { return active; }
    bool isFadingOut() const { return fadingOut; }

    void paint (juce::Graphics& g) override
    {
        if (! active && ! fadingOut)
            return;

        const auto& t = currentTheme();
        const float alpha = fadeAlpha;

        for (auto& target : targets)
        {
            auto* c = target.comp.getComponent();
            if (c == nullptr)
                continue;
            auto bounds = getLocalArea (c, c->getLocalBounds()).toFloat();
            if (bounds.isEmpty())
                continue;

            const bool selected = (target.kind == Target::destination && &target == selectedDest)
                                || (target.kind == Target::source && &target == selectedSource);

            if (selected)
            {
                g.setColour (t.assignSelected.withAlpha (0.9f * alpha));
                g.drawRoundedRectangle (bounds.reduced (1.5f), 4.0f, 2.0f);
                g.setColour (t.assignSelected.withAlpha (0.18f * alpha));
                g.fillRoundedRectangle (bounds.reduced (1.5f), 4.0f);
            }
            else
            {
                const float pulse = 0.25f + 0.55f * pulsePhase01();
                g.setColour (t.assignGlow.withAlpha (pulse * alpha));
                g.drawRoundedRectangle (bounds.reduced (1.0f), 4.0f, 2.0f);
            }
        }
    }

    // Shared by mouseDown and tests. Point is in overlay-local coordinates.
    void handleClickAt (juce::Point<int> localPos)
    {
        if (! active)
            return;

        // Topmost-wins: targets are pushed source-before-menus but within a
        // kind, later == painted later == visually on top, so search back-
        // to-front.
        for (auto it = targets.rbegin(); it != targets.rend(); ++it)
        {
            auto* c = it->comp.getComponent();
            if (c == nullptr)
                continue;
            auto bounds = getLocalArea (c, c->getLocalBounds());
            if (! bounds.contains (localPos))
                continue;

            handleTargetClick (*it);
            return;
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        handleClickAt (e.getPosition());
    }

    bool hitTest (int x, int y) override
    {
        if (! (active || fadingOut))
            return false;
        if (excludeBounds.contains (x, y))
            return false;
        return true;
    }

private:
    struct Target
    {
        enum Kind { destination, source, destMenu, sourceMenu } kind;
        juce::Component::SafePointer<juce::Component> comp;
        juce::String paramID;        // destination
        int modSourceValue = -1;     // source (-1 => needs the osc SFX popup)
        int oscSlot = -1;            // source, osc strip
        int route = -1;              // destMenu / sourceMenu
    };

    void rebuildTargets()
    {
        targets.clear();
        if (rootComponent == nullptr)
            return;

        // Precompute the matrix row source/dest param IDs once.
        juce::StringArray sourceMenuIds, destMenuIds;
        for (int r = 0; r < params::numModRoutes; ++r)
        {
            sourceMenuIds.add (params::id::routeParam (r, params::id::route::source));
            destMenuIds.add (params::id::routeParam (r, params::id::route::dest));
        }

        // Scroll-clipped targets (e.g. matrix rows scrolled out of the
        // MatrixPanel viewport) must not glow off in the footer -- the
        // overlay isn't a child of the viewport, so its own paint isn't
        // naturally clipped to the visible scroll window the way the real
        // control's paint would be. Explicitly skip anything whose bounds
        // don't intersect its nearest Viewport ancestor's visible area.
        auto isScrolledIntoView = [] (juce::Component& c)
        {
            auto* vp = c.findParentComponentOfClass<juce::Viewport>();
            if (vp == nullptr)
                return true;
            auto* viewed = vp->getViewedComponent();
            if (viewed == nullptr)
                return true;
            const auto boundsInContent = viewed->getLocalArea (&c, c.getLocalBounds());
            return vp->getViewArea().intersects (boundsInContent);
        };

        std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
        {
            if (! isScrolledIntoView (c))
                return;

            const auto paramID = c.getProperties()["paramID"].toString();
            if (paramID.isNotEmpty())
            {
                const int menuRow = sourceMenuIds.indexOf (paramID);
                const int destRow = destMenuIds.indexOf (paramID);
                if (menuRow >= 0)
                {
                    Target t; t.kind = Target::sourceMenu; t.comp = &c; t.route = menuRow;
                    targets.push_back (t);
                }
                else if (destRow >= 0)
                {
                    Target t; t.kind = Target::destMenu; t.comp = &c; t.route = destRow;
                    targets.push_back (t);
                }
                else if (params::modDestIndex (paramID) >= 0)
                {
                    Target t; t.kind = Target::destination; t.comp = &c; t.paramID = paramID;
                    targets.push_back (t);
                }
            }
            else if (c.getProperties().contains ("modSource"))
            {
                Target t; t.kind = Target::source; t.comp = &c;
                t.modSourceValue = (int) c.getProperties()["modSource"];
                targets.push_back (t);
            }
            else if (c.getProperties().contains ("oscSlot"))
            {
                Target t; t.kind = Target::source; t.comp = &c;
                t.oscSlot = (int) c.getProperties()["oscSlot"];
                targets.push_back (t);
            }

            for (auto* child : c.getChildren())
                walk (*child);
        };
        walk (*rootComponent);
    }

    void handleTargetClick (Target& target)
    {
        switch (target.kind)
        {
            case Target::destination:
                selectedDest = &target;
                repaint();
                break;

            case Target::source:
                if (target.oscSlot >= 0)
                    showOscSourceMenu (target);
                else
                {
                    selectedSource = &target;
                    repaint();
                }
                break;

            case Target::destMenu:
                if (selectedDest != nullptr)
                    setRouteChoice (target.route, params::id::route::dest,
                                    params::modDestIndex (selectedDest->paramID) + 1);
                break;

            case Target::sourceMenu:
                if (selectedSource != nullptr && selectedSource->modSourceValue >= 0)
                    setRouteChoice (target.route, params::id::route::source,
                                    selectedSource->modSourceValue);
                break;
        }
    }

    void showOscSourceMenu (Target& oscTarget)
    {
        const int slot = oscTarget.oscSlot;
        const juce::String letter = juce::String::charToString ((juce::juce_wchar) ('A' + slot));

        juce::PopupMenu menu;
        menu.addItem (1, "SFX " + letter + " Amp");
        menu.addItem (2, "SFX " + letter + " Pitch");

        // Keep the target alive across the async popup via SafePointer on
        // `this` -- `oscTarget` itself lives in the `targets` vector, which
        // is only rebuilt on the next rebuildTargets() (assign mode toggled
        // on again), so capturing its address is safe as long as this
        // overlay itself still exists.
        juce::Component::SafePointer<AssignOverlay> safe (this);
        auto* targetPtr = &oscTarget;
        menu.showMenuAsync (juce::PopupMenu::Options(),
            [safe, targetPtr, slot] (int result)
            {
                if (safe == nullptr || result == 0)
                    return;
                const int base = params::sfxFollowerBase + 2 * slot;
                targetPtr->modSourceValue = base + (result == 1 ? 0 : 1);
                safe->selectedSource = targetPtr;
                safe->repaint();
            });
    }

    void setRouteChoice (int route, const char* key, int choiceIndex)
    {
        if (route < 0)
            return;
        const auto id = params::id::routeParam (route, key);
        auto* p = apvts.getParameter (id);
        if (p == nullptr)
            return;
        const auto range = p->getNormalisableRange();
        const float norm = range.convertTo0to1 ((float) choiceIndex);
        p->beginChangeGesture();
        p->setValueNotifyingHost (norm);
        p->endChangeGesture();
        repaint();
    }

    float pulsePhase01() const
    {
        // 0..1 sine cycle, ~1.2s period.
        constexpr double periodMs = 1200.0;
        const auto t = std::fmod ((double) pulseMs, periodMs) / periodMs;
        return 0.5f + 0.5f * (float) std::sin (t * juce::MathConstants<double>::twoPi);
    }

    void timerCallback() override
    {
        pulseMs += 1000 / 30;

        if (fadingOut)
        {
            const auto elapsed = juce::Time::getMillisecondCounter() - fadeStartMs;
            if (elapsed >= 300)
            {
                fadingOut = false;
                fadeAlpha = 0.0f;
                setVisible (false);
                stopTimer();
            }
            else
            {
                fadeAlpha = 1.0f - (float) elapsed / 300.0f;
            }
        }
        repaint();
    }

    juce::AudioProcessorValueTreeState& apvts;
    juce::Component* rootComponent = nullptr;
    juce::Rectangle<int> excludeBounds;
    std::vector<Target> targets;
    Target* selectedDest = nullptr;
    Target* selectedSource = nullptr;
    bool active = false;
    bool fadingOut = false;
    float fadeAlpha = 0.0f;
    juce::uint32 fadeStartMs = 0;
    int pulseMs = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AssignOverlay)
};

} // namespace spa::ui
