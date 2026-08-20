#include "PresetBrowser.h"
#include "../SPASynthProcessor.h"
#include "../library/Library.h"

namespace spa::ui
{

namespace
{
    // Chip labels and the Filter::type each one selects.
    constexpr const char* chipLabels[] = { "ALL", "KEYS", "TEXTURE", "PULSE", "USER" };
    constexpr const char* chipTypes[]  = { "",    "Keys", "Texture", "Pulse", "User" };
    constexpr int chipRadioGroup = 0x5751;

    // The browser runs 2pt larger than the module grid (its LookAndFeel-drawn
    // controls get the same boost via the "browser"/"chip" componentIDs).
    juce::Font browserFont()
    {
        return metrics::labelFont().withHeight (metrics::labelFont().getHeight() + 2.0f);
    }

    juce::Font browserSmallFont()
    {
        return metrics::smallFont().withHeight (metrics::smallFont().getHeight() + 2.0f);
    }
}

juce::String PresetBrowser::typeOf (const library::PresetManager::PresetInfo& p)
{
    if (p.category == "User")
        return "User";

    for (const auto* t : { "Keys", "Texture", "Pulse" })
        if (p.name.endsWith (" " + juce::String (t)))
            return t;

    return {};
}

juce::String PresetBrowser::favoriteKey (const library::PresetManager::PresetInfo& p)
{
    return p.category + "/" + p.name;
}

std::vector<int> PresetBrowser::filterIndices (
    const std::vector<library::PresetManager::PresetInfo>& presets,
    const Filter& filter, const juce::StringArray& favoriteKeys)
{
    std::vector<int> out;

    for (size_t i = 0; i < presets.size(); ++i)
    {
        const auto& p = presets[i];

        if (filter.type.isNotEmpty() && typeOf (p) != filter.type)
            continue;
        if (filter.category.isNotEmpty() && p.category != filter.category)
            continue;
        if (filter.favoritesOnly && ! favoriteKeys.contains (favoriteKey (p)))
            continue;
        if (filter.search.isNotEmpty()
            && ! p.name.containsIgnoreCase (filter.search)
            && ! p.category.containsIgnoreCase (filter.search))
            continue;

        out.push_back ((int) i);
    }

    return out;
}

PresetBrowser::PresetBrowser (SPASynthProcessor& p,
                              std::function<void()> close,
                              std::function<void()> chooseLibrary)
    : processor (p), onClose (std::move (close)), onChooseLibrary (std::move (chooseLibrary))
{
    setComponentID ("presetBrowser");
    setWantsKeyboardFocus (true);

    closeButton.setComponentID ("browser");
    closeButton.setTooltip ("Close the preset browser (Esc)");
    closeButton.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (closeButton);

    searchBox.setSelectAllWhenFocused (true);
    searchBox.setEscapeAndReturnKeysConsumed (false);   // Esc bubbles up = close
    searchBox.onTextChange = [this] { applyFilter(); };
    addAndMakeVisible (searchBox);

    for (size_t i = 0; i < typeChips.size(); ++i)
    {
        auto& chip = typeChips[i];
        chip.setButtonText (chipLabels[i]);
        chip.setComponentID ("chip");
        chip.setClickingTogglesState (true);
        chip.setRadioGroupId (chipRadioGroup);
        chip.onClick = [this] { applyFilter(); };
        addAndMakeVisible (chip);
    }
    typeChips[0].setToggleState (true, juce::dontSendNotification);

    categoryBox.setComponentID ("browser");
    categoryBox.setWantsKeyboardFocus (false);   // see Controls.h's Knob
    categoryBox.setTextWhenNothingSelected ("All Packs");
    categoryBox.onChange = [this] { applyFilter(); };
    addAndMakeVisible (categoryBox);

    favoritesChip.setComponentID ("chip");
    favoritesChip.setClickingTogglesState (true);
    favoritesChip.setTooltip ("Show favorites only");
    favoritesChip.onClick = [this] { applyFilter(); };
    addAndMakeVisible (favoritesChip);

    list.setRowHeight (36);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (list);

    countLabel.setFont (browserSmallFont());
    countLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (countLabel);

    libraryButton.setComponentID ("browser");
    libraryButton.setTooltip ("Point SPASynth at the Silverplatter library folder");
    libraryButton.onClick = [this] { if (onChooseLibrary) onChooseLibrary(); };
    addAndMakeVisible (libraryButton);

    rescanButton.setComponentID ("browser");
    rescanButton.setTooltip ("Rescan the library and preset folders");
    rescanButton.onClick = [this] { processor.refreshLibrary(); };
    addAndMakeVisible (rescanButton);

    processor.getPresetManager().addChangeListener (this);
    refresh();
}

PresetBrowser::~PresetBrowser()
{
    processor.getPresetManager().removeChangeListener (this);
}

void PresetBrowser::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

void PresetBrowser::openImmediately()
{
    setVisible (true);
    if (! openBounds.isEmpty())
        setBounds (openBounds);
    toFront (false);
}

void PresetBrowser::refresh()
{
    const auto& t = currentTheme();

    presets = processor.getPresetManager().getPresets();
    favoriteKeys = library::getFavoritePresets();

    // Rebuild the pack combo, keeping the current pick when it still exists.
    const auto selectedCategory = categoryBox.getSelectedId() > 1 ? categoryBox.getText()
                                                                  : juce::String();
    categoryBox.clear (juce::dontSendNotification);
    categoryBox.addItem ("All Packs", 1);
    int itemID = 2;
    for (const auto& category : processor.getPresetManager().getCategories())
    {
        categoryBox.addItem (category, itemID);
        if (category == selectedCategory)
            categoryBox.setSelectedId (itemID, juce::dontSendNotification);
        ++itemID;
    }
    if (categoryBox.getSelectedId() == 0)
        categoryBox.setSelectedId (1, juce::dontSendNotification);

    searchBox.setFont (browserFont());
    searchBox.setColour (juce::TextEditor::backgroundColourId, t.display);
    searchBox.setColour (juce::TextEditor::textColourId, t.textPrimary);
    searchBox.setColour (juce::TextEditor::outlineColourId, t.outline);
    searchBox.setColour (juce::TextEditor::focusedOutlineColourId, t.accentMod);
    searchBox.setTextToShowWhenEmpty ("Search presets...", t.textSecondary);
    searchBox.applyColourToAllText (t.textPrimary);

    countLabel.setColour (juce::Label::textColourId, t.textSecondary);

    applyFilter();
}

void PresetBrowser::applyFilter()
{
    Filter filter;
    filter.search = searchBox.getText().trim();
    for (size_t i = 0; i < typeChips.size(); ++i)
        if (typeChips[i].getToggleState())
            filter.type = chipTypes[i];
    if (categoryBox.getSelectedId() > 1)
        filter.category = categoryBox.getText();
    filter.favoritesOnly = favoritesChip.getToggleState();

    filtered = filterIndices (presets, filter, favoriteKeys);

    countLabel.setText (juce::String (filtered.size()) + " of "
                            + juce::String (presets.size()) + " presets",
                        juce::dontSendNotification);

    list.updateContent();

    // Highlight (and reveal) the loaded preset when it survives the filter.
    const auto currentName = processor.getPresetManager().getCurrentName();
    int currentRow = -1;
    for (size_t row = 0; row < filtered.size(); ++row)
        if (presets[(size_t) filtered[row]].name == currentName)
            currentRow = (int) row;

    if (currentRow >= 0)
    {
        list.selectRow (currentRow);
        list.scrollToEnsureRowIsOnscreen (currentRow);
    }
    else
    {
        list.deselectAllRows();
    }

    repaint();
}

int PresetBrowser::getNumRows()
{
    return (int) filtered.size();
}

void PresetBrowser::paintListBoxItem (int row, juce::Graphics& g, int width, int height,
                                      bool rowIsSelected)
{
    if (row < 0 || row >= (int) filtered.size())
        return;

    const auto& t = currentTheme();
    const auto& p = presets[(size_t) filtered[(size_t) row]];
    auto r = juce::Rectangle<int> (0, 0, width, height);

    if (rowIsSelected)
    {
        g.setColour (t.accent.withAlpha (0.13f));
        g.fillRect (r);
        g.setColour (t.accent);
        g.fillRect (r.removeFromLeft (2));
    }

    const auto starZone = r.removeFromRight (30);
    const bool favorite = favoriteKeys.contains (favoriteKey (p));
    g.setColour (favorite ? t.accent : t.textSecondary.withAlpha (0.45f));
    g.setFont (juce::Font (juce::FontOptions (15.0f)));
    g.drawText (favorite ? juce::String::fromUTF8 ("\xe2\x98\x85")     // ★
                         : juce::String::fromUTF8 ("\xe2\x98\x86"),    // ☆
                starZone, juce::Justification::centred);

    auto text = r.reduced (8, 4);
    g.setColour (t.textPrimary);
    g.setFont (browserFont());
    g.drawText (p.name, text.removeFromTop (text.getHeight() / 2),
                juce::Justification::centredLeft, true);
    g.setColour (t.textSecondary);
    g.setFont (browserSmallFont());
    g.drawText (p.category, text, juce::Justification::centredLeft, true);

    g.setColour (t.outline.withAlpha (0.5f));
    g.fillRect (0, height - 1, width, 1);
}

void PresetBrowser::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (row < 0 || row >= (int) filtered.size())
        return;

    const auto& p = presets[(size_t) filtered[(size_t) row]];

    // Star zone toggles favourite instead of loading.
    if (e.getMouseDownX() > list.getWidth() - 34)
    {
        const auto key = favoriteKey (p);
        library::setPresetFavorite (key, ! favoriteKeys.contains (key));
        favoriteKeys = library::getFavoritePresets();

        if (favoritesChip.getToggleState())
            applyFilter();          // un-starring removes it from the list
        else
            list.repaintRow (row);
        return;
    }

    processor.getPresetManager().loadPresetFile (p.file);
}

bool PresetBrowser::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        if (onClose)
            onClose();
        return true;
    }

    return false;
}

void PresetBrowser::paint (juce::Graphics& g)
{
    const auto& t = currentTheme();
    auto bounds = getLocalBounds();
    const auto shadow = bounds.removeFromRight (shadowWidth);

    // Cast shadow onto the modules behind — sells the "drawer over UI" read.
    g.setGradientFill (juce::ColourGradient (
        juce::Colours::black.withAlpha (0.35f), (float) shadow.getX(), 0.0f,
        juce::Colours::transparentBlack, (float) shadow.getRight(), 0.0f, false));
    g.fillRect (shadow);

    g.setColour (t.panel);
    g.fillRect (bounds);
    g.setColour (t.outline);
    g.drawVerticalLine (bounds.getRight() - 1, 0.0f, (float) getHeight());

    g.setColour (t.textSecondary);
    g.setFont (metrics::sectionFont().withHeight (metrics::sectionFont().getHeight() + 2.0f));
    g.drawText ("PRESETS", titleArea, juce::Justification::centredLeft);

    draw::displayWell (g, listWell.toFloat().expanded (2.0f));
}

void PresetBrowser::resized()
{
    auto bounds = getLocalBounds();
    bounds.removeFromRight (shadowWidth);
    bounds.reduce (10, 10);

    auto header = bounds.removeFromTop (22);
    closeButton.setBounds (header.removeFromRight (22));
    titleArea = header;

    bounds.removeFromTop (8);
    searchBox.setBounds (bounds.removeFromTop (26));

    bounds.removeFromTop (6);
    auto chipRow = bounds.removeFromTop (20);
    const auto chipWidth = (chipRow.getWidth() - 3 * (int) typeChips.size() + 3)
                               / (int) typeChips.size();
    for (auto& chip : typeChips)
    {
        chip.setBounds (chipRow.removeFromLeft (chipWidth));
        chipRow.removeFromLeft (3);
    }

    bounds.removeFromTop (6);
    auto packRow = bounds.removeFromTop (24);
    favoritesChip.setBounds (packRow.removeFromRight (28));
    packRow.removeFromRight (4);
    categoryBox.setBounds (packRow);

    auto footer = bounds.removeFromBottom (24);
    rescanButton.setBounds (footer.removeFromRight (64));
    footer.removeFromRight (4);
    libraryButton.setBounds (footer.removeFromRight (96));
    countLabel.setBounds (footer);

    bounds.removeFromBottom (8);
    bounds.removeFromTop (8);
    listWell = bounds;
    list.setBounds (bounds.reduced (1));
}

} // namespace spa::ui
