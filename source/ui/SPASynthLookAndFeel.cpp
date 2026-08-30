#include "SPASynthLookAndFeel.h"
#include "../library/Library.h"
#include "DraggableTabs.h"

namespace spa::ui
{

namespace
{
    // The one theme instance (message thread only). Accents load from the
    // user's saved preference on first access.
    Theme& mutableTheme()
    {
        static Theme theme = []
        {
            auto t = Theme::dark();
            t.accent = library::getAccentColor (t.accent);
            t.accentMod = library::getAccentModColor (t.accentMod);
            return t;
        }();
        return theme;
    }
}

const Theme& currentTheme()
{
    return mutableTheme();
}

void setAccentColors (juce::Colour audio, juce::Colour mod)
{
    mutableTheme().accent = audio;
    mutableTheme().accentMod = mod;
    library::setAccentColors (audio, mod);
}

void resetAccentColors()
{
    const auto defaults = Theme::dark();
    setAccentColors (defaults.accent, defaults.accentMod);
}

namespace draw
{

void panel (juce::Graphics&, juce::Rectangle<float>)
{
    // Faceplate restyle: modules no longer paint their own card. The
    // continuous surface (fill + texture + seams + row shadows) is painted
    // once behind everything by ContentComponent::paint, so it shows
    // through unbroken. Kept as a no-op (rather than deleting every call
    // site) so draw::panel(...) stays the single place to reintroduce a
    // module fill if that's ever needed again.
}

juce::Rectangle<int> sectionHeader (juce::Graphics& g, juce::Rectangle<int> bounds,
                                    const juce::String& title, const juce::String& readout,
                                    juce::Colour titleColour)
{
    const auto& t = currentTheme();
    auto header = bounds.removeFromTop (20);
    auto text = header.reduced (8, 0);

    if (titleColour == juce::Colour())
        titleColour = t.textPrimary;

    g.setFont (metrics::sectionFont());

    const auto titleText = title.toUpperCase();
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText (metrics::sectionFont(), titleText, 0.0f, 0.0f);
    const auto titleWidth = (int) std::ceil (glyphs.getBoundingBox (0, -1, true).getWidth());

    g.setColour (titleColour);
    g.drawText (titleText, text, juce::Justification::centredLeft);

    int readoutWidth = 0;
    if (readout.isNotEmpty())
    {
        const auto stringWidth = [] (const juce::String& s)
        {
            juce::GlyphArrangement glyphArrangement;
            glyphArrangement.addLineOfText (metrics::labelFont(), s, 0.0f, 0.0f);
            return (int) std::ceil (glyphArrangement.getBoundingBox (0, -1, true).getWidth());
        };

        // Never run under the title: fit into the space after title + a
        // minimum rule, ellipsizing the tail of long content names.
        const auto available = text.getWidth() - titleWidth - 8 - 14;
        auto fitted = readout;
        if (stringWidth (fitted) > available)
        {
            while (fitted.length() > 4 && stringWidth (fitted + "...") > available)
                fitted = fitted.dropLastCharacters (1).trimEnd();
            fitted += "...";
        }

        if (available > 20)
        {
            g.setColour (t.textSecondary);
            g.setFont (metrics::labelFont());
            g.drawText (fitted, text, juce::Justification::centredRight);
            readoutWidth = 8 + stringWidth (fitted);
        }
    }

    // Thin rule between title and readout.
    const auto ruleY = (float) header.getCentreY();
    g.setColour (t.outline);
    g.drawLine ((float) (text.getX() + titleWidth + 8), ruleY,
                (float) (text.getRight() - readoutWidth), ruleY, 1.0f);

    return bounds;
}

void displayWell (juce::Graphics& g, juce::Rectangle<float> bounds, bool centreLine)
{
    const auto& t = currentTheme();

    // Faceplate restyle: no LED-screen well any more — curves render
    // straight on the faceplate surface (glowStroke). Keep only an
    // extremely faint zero/centre reference line; several scopes (LFO,
    // bipolar wave, filter) are otherwise hard to read with no baseline.
    // Non-scope callers (e.g. a plain list container) can opt out — a
    // reference line has no meaning there and just bisects the content.
    if (! centreLine)
        return;

    g.setColour (t.outline.withAlpha (0.18f));
    g.drawHorizontalLine ((int) bounds.getCentreY(), bounds.getX() + 2.0f,
                          bounds.getRight() - 2.0f);
}

void glowStroke (juce::Graphics& g, const juce::Path& path, juce::Colour colour, float thickness)
{
    // Three feathered passes for the mock's neon glow.
    g.setColour (colour.withAlpha (0.10f));
    g.strokePath (path, juce::PathStrokeType (thickness * 6.0f,
                                              juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));
    g.setColour (colour.withAlpha (0.28f));
    g.strokePath (path, juce::PathStrokeType (thickness * 2.6f,
                                              juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));
    g.setColour (colour.brighter (0.05f));
    g.strokePath (path, juce::PathStrokeType (thickness,
                                              juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));
}

} // namespace draw

SPASynthLookAndFeel::SPASynthLookAndFeel()
{
    refreshPalette();
}

void SPASynthLookAndFeel::refreshPalette()
{
    const auto& t = currentTheme();

    setColour (juce::ResizableWindow::backgroundColourId, t.background);
    setColour (juce::Label::textColourId, t.textSecondary);
    setColour (juce::TextButton::buttonColourId, t.knobFace);
    setColour (juce::TextButton::buttonOnColourId, t.accentMod.withAlpha (0.85f));
    setColour (juce::TextButton::textColourOffId, t.textPrimary);
    setColour (juce::TextButton::textColourOnId, t.display);
    setColour (juce::ComboBox::backgroundColourId, t.display);
    setColour (juce::ComboBox::textColourId, t.textPrimary);
    setColour (juce::ComboBox::outlineColourId, t.outline);
    setColour (juce::ComboBox::arrowColourId, t.textSecondary);
    setColour (juce::PopupMenu::backgroundColourId, t.panel);
    setColour (juce::PopupMenu::textColourId, t.textPrimary);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, t.accent.withAlpha (0.30f));
    setColour (juce::PopupMenu::highlightedTextColourId, t.textPrimary);
    setColour (juce::Slider::textBoxTextColourId, t.textPrimary);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::thumbColourId, t.accent);
    setColour (juce::Slider::trackColourId, t.knobTrack);
    setColour (juce::Slider::backgroundColourId, t.knobTrack);
    setColour (juce::ScrollBar::thumbColourId, t.knobTrack);
    setColour (juce::TooltipWindow::backgroundColourId, t.panel);
    setColour (juce::TooltipWindow::textColourId, t.textPrimary);
    setColour (juce::TooltipWindow::outlineColourId, t.outline);
    setColour (juce::BubbleComponent::backgroundColourId, t.panel);
    setColour (juce::TabbedButtonBar::tabTextColourId, t.textSecondary);
    setColour (juce::TabbedButtonBar::frontTextColourId, t.textPrimary);
    // JUCE's TabbedComponent fills a 1px outline around its content area in
    // this colour whenever outlineThickness > 0 (the default); it was never
    // tokenized, so it painted with the stock LookAndFeel_V4 default rather
    // than any theme colour. Faceplate restyle has no card/frame around the
    // filter/env/lfo/fx tab content any more, so make it fully transparent
    // instead of chasing setOutline(0) on every TabbedComponent instance.
    setColour (juce::TabbedComponent::outlineColourId, juce::Colours::transparentBlack);
    setColour (juce::AlertWindow::backgroundColourId, t.panel);
    setColour (juce::AlertWindow::textColourId, t.textPrimary);
}

void SPASynthLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                           float sliderPos, float rotaryStartAngle,
                                           float rotaryEndAngle, juce::Slider& slider)
{
    const auto& t = currentTheme();
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (2.0f);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    // Flat, modern knob (per the Audio Damage reference): a thin track ring,
    // an accent value arc, and a small position dot. No disc, bevel, drop
    // shadow or needle. Sizes/placement are unchanged — only the paint.
    const auto lineW = juce::jlimit (1.6f, 2.6f, radius * 0.12f);
    const auto arcRadius = radius - lineW * 1.2f;   // inset so the dot stays in bounds

    // The WILD knob's ring heats from the accent (0%) up to bright red (100%)
    // so the chaos amount reads at a glance. Interpolate in HSV so the sweep
    // stays vivid (teal -> green -> amber -> red) rather than passing through
    // muddy greys, and land exactly on the accent at 0 and red at 1.
    const auto wildHeat = [&t] (float pos)
    {
        const juce::Colour red (0xffff2a24);
        const auto a = t.accent;
        pos = juce::jlimit (0.0f, 1.0f, pos);
        return juce::Colour::fromHSV (
            a.getHue()        + (red.getHue()        - a.getHue())        * pos,
            a.getSaturation() + (red.getSaturation() - a.getSaturation()) * pos,
            a.getBrightness() + (red.getBrightness() - a.getBrightness()) * pos,
            1.0f);
    };

    const auto accent  = slider.getComponentID() == "wild" ? wildHeat (sliderPos)
                       : slider.getComponentID() == "mod"  ? t.accentMod
                                                           : t.accent;
    const auto enabled = slider.isEnabled();
    const auto hot     = slider.isMouseOverOrDragging() && enabled;

    // Track ring (full sweep).
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                         rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (hot ? t.knobTrack.brighter (0.15f)
                     : t.knobTrack.withAlpha (enabled ? 0.9f : 0.5f));
    g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    // Value arc: grows from the centre for bipolar params, else from the start.
    const auto isBipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    const auto fillStart = isBipolar
                         ? rotaryStartAngle + 0.5f * (rotaryEndAngle - rotaryStartAngle)
                         : rotaryStartAngle;
    const auto arcColour = enabled ? accent : t.textSecondary.withAlpha (0.35f);

    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                         fillStart, angle, true);
    if (enabled)   // faint bloom keeps a hint of the accent glow, subtly
    {
        g.setColour (accent.withAlpha (hot ? 0.28f : 0.16f));
        g.strokePath (value, juce::PathStrokeType (lineW * 2.6f,
                                                   juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }
    g.setColour (arcColour);
    g.strokePath (value, juce::PathStrokeType (lineW, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    // Position dot riding the ring at the current angle.
    const auto dotR = lineW * (hot ? 1.35f : 1.15f);
    const auto dot  = centre.getPointOnCircumference (arcRadius, angle);
    g.setColour (enabled ? accent.brighter (0.15f) : t.textSecondary.withAlpha (0.4f));
    g.fillEllipse (dot.x - dotR, dot.y - dotR, dotR * 2.0f, dotR * 2.0f);

    // Press state for label-less knobs that opt in: a value chip across the
    // knob (used by the header master volume).
    if (slider.isMouseButtonDown()
        && slider.getProperties().contains ("inlineValueSuffix"))
    {
        const auto magnitude = std::abs (slider.getValue());
        const auto text = juce::String (slider.getValue(),
                                        magnitude >= 1000.0 ? 0 : 1)
                        + slider.getProperties()["inlineValueSuffix"].toString();
        g.setFont (metrics::smallFont());

        juce::GlyphArrangement glyphs;
        glyphs.addLineOfText (metrics::smallFont(), text, 0.0f, 0.0f);
        const auto w = glyphs.getBoundingBox (0, -1, true).getWidth() + 10.0f;
        const auto chip = juce::Rectangle<float> (w, 14.0f).withCentre (centre);

        g.setColour (t.display.withAlpha (0.92f));
        g.fillRoundedRectangle (chip, 3.0f);
        g.setColour (t.outline);
        g.drawRoundedRectangle (chip, 3.0f, 1.0f);
        g.setColour (t.textPrimary);
        g.drawText (text, chip.toNearestInt(), juce::Justification::centred);
    }
}

void SPASynthLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                           float sliderPos, float, float,
                                           juce::Slider::SliderStyle style, juce::Slider& slider)
{
    const auto& t = currentTheme();

    if (style == juce::Slider::LinearHorizontal || style == juce::Slider::LinearBar)
    {
        const auto track = juce::Rectangle<float> ((float) x, (float) y + (float) height * 0.5f - 1.5f,
                                                   (float) width, 3.0f);
        g.setColour (t.knobTrack);
        g.fillRoundedRectangle (track, 1.5f);

        const auto accent = slider.getComponentID() == "mod" ? t.accentMod : t.accent;
        const auto isBipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
        const auto zeroX = isBipolar ? (float) x + (float) width * 0.5f : (float) x;
        g.setColour (accent);
        g.fillRoundedRectangle (juce::Rectangle<float> (juce::jmin (zeroX, sliderPos),
                                                        track.getY(),
                                                        std::abs (sliderPos - zeroX), 3.0f),
                                1.5f);

        g.setColour (t.textPrimary);
        g.fillEllipse (sliderPos - 4.0f, (float) y + (float) height * 0.5f - 4.0f, 8.0f, 8.0f);

        // Press state: numeric readout inside the slider, on whichever side
        // of the thumb has room.
        if (slider.isMouseButtonDown())
        {
            const auto text = juce::String (slider.getValue(), 2);
            g.setFont (metrics::smallFont());
            g.setColour (t.textPrimary);
            const auto onLeft = sliderPos > (float) x + (float) width * 0.5f;
            const auto textArea = onLeft
                ? juce::Rectangle<int> (x + 2, y, (int) (sliderPos - (float) x) - 10, height)
                : juce::Rectangle<int> ((int) sliderPos + 10, y,
                                        x + width - (int) sliderPos - 12, height);
            g.drawText (text, textArea,
                        onLeft ? juce::Justification::centredLeft
                               : juce::Justification::centredRight);
        }
        return;
    }

    LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, 0, 0, style, slider);
}

void SPASynthLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                               const juce::Colour& backgroundColour,
                                               bool highlighted, bool down)
{
    const auto& t = currentTheme();
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);

    // Primary action: glossy accent gradient + glow (RANDOMIZE ALL).
    if (button.getComponentID() == "primary")
    {
        g.setColour (t.accent.withAlpha (down ? 0.15f : 0.30f));
        g.fillRoundedRectangle (bounds.expanded (2.5f), metrics::cornerRadius + 2.0f);

        auto top = t.accent.brighter (highlighted ? 0.22f : 0.14f);
        auto bottom = t.accent.darker (down ? 0.35f : 0.18f);
        g.setGradientFill (juce::ColourGradient (top, bounds.getX(), bounds.getY(),
                                                 bottom, bounds.getX(), bounds.getBottom(),
                                                 false));
        g.fillRoundedRectangle (bounds, metrics::cornerRadius);

        g.setColour (juce::Colours::white.withAlpha (0.25f));
        g.drawLine (bounds.getX() + metrics::cornerRadius, bounds.getY() + 1.0f,
                    bounds.getRight() - metrics::cornerRadius, bounds.getY() + 1.0f, 1.0f);
        return;
    }

    auto colour = backgroundColour;
    if (button.getToggleState())
        colour = findColour (juce::TextButton::buttonOnColourId);
    if (down)
        colour = colour.darker (0.15f);
    else if (highlighted)
        colour = colour.brighter (0.07f);

    g.setColour (colour);
    g.fillRoundedRectangle (bounds, metrics::cornerRadius);
    g.setColour (t.outline);
    g.drawRoundedRectangle (bounds, metrics::cornerRadius, 1.0f);
}

// Minimal padlock glyph inside rect r: a stroked shackle (inverted U) over a
// filled rounded-rect body. Used to mark a locked section button.
static void drawLockGlyph (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
{
    const float stroke = juce::jmax (1.0f, r.getHeight() * 0.13f);
    const float bodyH  = r.getHeight() * 0.56f;
    const auto  body   = juce::Rectangle<float> (r.getX(), r.getBottom() - bodyH,
                                                 r.getWidth(), bodyH);
    const float sr     = r.getWidth() * 0.30f;          // shackle radius
    const float scy    = body.getY() - sr * 0.30f;      // shackle arc centre
    const float legB   = body.getY() + stroke * 0.4f;   // legs sink into the body

    juce::Path shackle;
    shackle.startNewSubPath (r.getCentreX() - sr, legB);
    shackle.lineTo          (r.getCentreX() - sr, scy);
    shackle.addCentredArc    (r.getCentreX(), scy, sr, sr, 0.0f,
                              -juce::MathConstants<float>::halfPi,
                               juce::MathConstants<float>::halfPi, false);
    shackle.lineTo          (r.getCentreX() + sr, legB);

    g.setColour (c);
    g.strokePath (shackle, juce::PathStrokeType (stroke, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
    g.fillRoundedRectangle (body, r.getWidth() * 0.16f);
}

void SPASynthLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button,
                                          bool highlighted, bool down)
{
    // Preset prev/next: draw a solid left/right triangle caret instead of a
    // "<"/">" glyph.
    const auto navId = button.getComponentID();
    if (navId == "navPrev" || navId == "navNext")
    {
        const auto c = button.getLocalBounds().toFloat().getCentre();
        constexpr float w = 3.6f, h = 9.0f;   // triangle half-width / height
        juce::Path tri;
        if (navId == "navPrev")   // points left
            tri.addTriangle (c.x - w, c.y, c.x + w, c.y - h * 0.5f, c.x + w, c.y + h * 0.5f);
        else                      // points right
            tri.addTriangle (c.x + w, c.y, c.x - w, c.y - h * 0.5f, c.x - w, c.y + h * 0.5f);

        g.setColour (button.findColour (juce::TextButton::textColourOffId)
                         .withAlpha (down ? 0.55f : (highlighted ? 1.0f : 0.85f)));
        g.fillPath (tri);
        return;
    }

    // Locked section buttons show a small padlock beside the label so the
    // locked state reads as "locked", not just a colour change. Everything
    // else uses the default text rendering.
    if (button.getComponentID() != "lock" || ! button.getToggleState())
    {
        LookAndFeel_V4::drawButtonText (g, button, highlighted, down);
        return;
    }

    const auto font = getTextButtonFont (button, button.getHeight());
    const auto text = button.getButtonText();

    juce::GlyphArrangement ga;
    ga.addLineOfText (font, text, 0.0f, 0.0f);
    const auto textW = ga.getBoundingBox (0, -1, true).getWidth();

    const auto h      = (float) button.getHeight();
    const auto iconH  = juce::jmin (h * 0.5f, font.getHeight() * 0.95f);
    const auto iconW  = iconH * 0.74f;
    const auto gap    = 4.0f;
    const auto groupW = iconW + gap + textW;
    const auto startX = juce::jmax (2.0f, ((float) button.getWidth() - groupW) * 0.5f);

    const auto colour = button.findColour (juce::TextButton::textColourOnId);
    drawLockGlyph (g, { startX, (h - iconH) * 0.5f, iconW, iconH }, colour);

    g.setColour (colour);
    g.setFont (font);
    g.drawText (text, juce::Rectangle<float> (startX + iconW + gap, 0.0f,
                                              (float) button.getWidth() - (startX + iconW + gap),
                                              h).toNearestInt(),
                juce::Justification::centredLeft, true);
}

void SPASynthLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                           bool highlighted, bool)
{
    const auto& t = currentTheme();
    const auto bounds = button.getLocalBounds().toFloat();
    const auto pillH = juce::jmin (14.0f, bounds.getHeight() - 4.0f);
    const auto pillW = pillH * 1.9f;
    const auto pill = juce::Rectangle<float> (bounds.getX(),
                                              bounds.getCentreY() - pillH * 0.5f,
                                              pillW, pillH);
    const auto enabled = button.isEnabled();

    g.setColour ((button.getToggleState() ? t.accentMod
                                          : t.knobTrack.brighter (highlighted ? 0.08f : 0.0f))
                    .withMultipliedAlpha (enabled ? 1.0f : 0.5f));
    g.fillRoundedRectangle (pill, pillH * 0.5f);

    const auto knobX = button.getToggleState() ? pill.getRight() - pillH + 2.0f
                                               : pill.getX() + 2.0f;
    g.setColour (t.textPrimary.withAlpha (enabled ? 1.0f : 0.4f));
    g.fillEllipse (knobX, pill.getY() + 2.0f, pillH - 4.0f, pillH - 4.0f);

    g.setColour (t.textSecondary.withAlpha (enabled ? 1.0f : 0.4f));
    g.setFont (metrics::smallFont());
    g.drawText (button.getButtonText().toUpperCase(),
                bounds.withTrimmedLeft (pillW + 5.0f).toNearestInt(),
                juce::Justification::centredLeft);
}

void SPASynthLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                       int, int, int, int, juce::ComboBox& box)
{
    const auto& t = currentTheme();
    const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height)
                            .reduced (0.5f);
    const auto enabled = box.isEnabled();

    g.setColour (findColour (juce::ComboBox::backgroundColourId).withMultipliedAlpha (enabled ? 1.0f : 0.5f));
    g.fillRoundedRectangle (bounds, metrics::cornerRadius);
    g.setColour (t.outline.withAlpha (enabled ? 1.0f : 0.5f));
    g.drawRoundedRectangle (bounds, metrics::cornerRadius, 1.0f);

    juce::Path chevron;
    const auto cx = (float) width - 11.0f;
    const auto cy = (float) height * 0.5f;
    chevron.startNewSubPath (cx - 3.5f, cy - 1.8f);
    chevron.lineTo (cx, cy + 2.2f);
    chevron.lineTo (cx + 3.5f, cy - 1.8f);
    g.setColour (t.textSecondary.withAlpha (enabled ? 1.0f : 0.4f));
    g.strokePath (chevron, juce::PathStrokeType (1.4f));
}

juce::Label* SPASynthLookAndFeel::createComboBoxTextBox (juce::ComboBox&)
{
    // Matches LookAndFeel_V2's default (juce_LookAndFeel_V2.cpp) except for
    // the focus flag -- see this override's declaration comment.
    auto* label = new juce::Label();
    label->setMouseClickGrabsKeyboardFocus (false);
    return label;
}

// Draggable FX tabs draw a grip-dots handle at the left; reserve room for it so
// a re-laid-out (tight) bar never slides the centred text onto the grip.
static constexpr int tabGripReserve = 16;

int SPASynthLookAndFeel::getTabButtonBestWidth (juce::TabBarButton& button, int)
{
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText (metrics::smallFont(), button.getButtonText(), 0.0f, 0.0f);
    const int grip = dynamic_cast<DraggableTabButton*> (&button) != nullptr ? tabGripReserve : 0;
    return juce::jmax (36, (int) std::ceil (glyphs.getBoundingBox (0, -1, true).getWidth())
                              + 16 + grip);
}

void SPASynthLookAndFeel::drawTabButton (juce::TabBarButton& button, juce::Graphics& g,
                                        bool isMouseOver, bool)
{
    const auto& t = currentTheme();
    const auto bounds = button.getLocalBounds().toFloat().reduced (1.0f, 2.0f);
    const auto front = button.isFrontTab();

    if (front)
    {
        // Faceplate restyle: no display-black pill behind the front tab —
        // just the accent underline against the continuous surface, with
        // brighter text (below) carrying the "selected" read.
        auto underline = bounds;
        g.setColour (t.accent);
        g.fillRect (underline.removeFromBottom (2.0f).reduced (4.0f, 0.0f));
    }
    else if (isMouseOver)
    {
        g.setColour (t.seam.withAlpha (0.6f));
        g.fillRoundedRectangle (bounds, 2.0f);
    }

    g.setColour (front ? t.textPrimary : t.textSecondary);
    g.setFont (metrics::smallFont());
    auto textArea = button.getLocalBounds();
    if (dynamic_cast<DraggableTabButton*> (&button) != nullptr)
        textArea.removeFromLeft (tabGripReserve);   // keep the text clear of the grip
    g.drawText (button.getButtonText().toUpperCase(), textArea, juce::Justification::centred);
}

void SPASynthLookAndFeel::drawTabbedButtonBarBackground (juce::TabbedButtonBar&, juce::Graphics&)
{
}

} // namespace spa::ui
