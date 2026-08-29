#pragma once

#include "Theme.h"
#include "Controls.h"
#include "../library/PresetManager.h"

namespace spa
{

class SPASynthProcessor;

namespace ui
{

// UAD-style preset drawer: slides in from the left over the module grid.
// Live search, type chips (factory presets come in Keys/Texture/Pulse
// flavours, plus User), a pack filter, and persisted favourites. Clicking a
// row loads it; the star toggles favourite. Esc or the X closes the drawer.
class PresetBrowser : public juce::Component,
                      private juce::ChangeListener,
                      private juce::ListBoxModel,
                      private juce::ComponentListener
{
public:
    // onRequestKeyboardFocus: called after a preset row click loads a preset
    // if the search box had keyboard focus at click time, so the caller can
    // hand focus to the on-screen keyboard (if visible) and resume QWERTY
    // note-play -- see listBoxItemClicked.
    PresetBrowser (SPASynthProcessor&,
                   std::function<void()> onClose,
                   std::function<void()> onChooseLibrary,
                   std::function<void()> onRequestKeyboardFocus);
    ~PresetBrowser() override;

    // --- pure filtering (testable without a UI) ------------------------------
    struct Filter
    {
        juce::String search;        // case-insensitive substring of name/pack
        juce::String type;          // "", "Keys", "Texture", "Pulse", "User"
        juce::String category;      // "" = all packs
        bool favoritesOnly = false;
    };

    static juce::String typeOf (const library::PresetManager::PresetInfo&);
    static juce::String favoriteKey (const library::PresetManager::PresetInfo&);
    static std::vector<int> filterIndices (
        const std::vector<library::PresetManager::PresetInfo>&,
        const Filter&, const juce::StringArray& favoriteKeys);

    // The parent stores the drawer's on-screen bounds every resized(); the
    // open/close animation slides between these and off-screen left.
    void setOpenBounds (juce::Rectangle<int> b) { openBounds = b; }
    juce::Rectangle<int> getOpenBounds() const { return openBounds; }
    void openImmediately();   // no animation — snapshot tests front the drawer

    void refresh();   // re-pull presets, categories, theme colours

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void applyFilter();

    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height,
                           bool rowIsSelected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;

    // ListBox rows are created lazily while scrolling -- new RowComponents
    // introduce fresh children after construction that would otherwise
    // revert to JUCE's click-grabs-focus default. Re-sweep whenever the
    // watched row container's children change. (ComboBox's internal Label
    // rebuild -- the other source of fresh post-construction children --
    // is handled once, application-wide, by SPASynthLookAndFeel::
    // createComboBoxTextBox instead of a listener here.)
    void componentChildrenChanged (juce::Component&) override;

    SPASynthProcessor& processor;
    std::function<void()> onClose, onChooseLibrary, onRequestKeyboardFocus;

    juce::TextButton closeButton { juce::String::fromUTF8 ("\xc3\x97") };   // ×
    juce::TextEditor searchBox;
    std::array<juce::TextButton, 5> typeChips;
    juce::ComboBox categoryBox;
    juce::TextButton favoritesChip { juce::String::fromUTF8 ("\xe2\x98\x85") };  // ★
    juce::ListBox list { {}, this };
    juce::Label countLabel;
    juce::TextButton libraryButton { "SET LIBRARY..." }, rescanButton { "RESCAN" };

    std::vector<library::PresetManager::PresetInfo> presets;   // snapshot
    std::vector<int> filtered;                                 // indices into presets
    juce::StringArray favoriteKeys;                            // cached settings
    juce::Rectangle<int> openBounds, titleArea, listWell;

    static constexpr int shadowWidth = 10;   // cast onto the content behind

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetBrowser)
};

} // namespace ui
} // namespace spa
