#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <juce_graphics/juce_graphics.h>
#include <vector>

namespace spa::library
{

// One pack folder = one preset category (the Silverplatter convention).
struct Pack
{
    juce::String name;
    juce::File folder;
    juce::Array<juce::File> wavs;   // sorted by file size, ascending
};

// Scans a library root: every direct subfolder containing WAVs is a pack.
// WAV files sitting directly in the root (no subfolder) also form a pack of
// their own, named after the root folder itself.
std::vector<Pack> scanLibrary (const juce::File& root);

// --- Machine-level settings (library location, not per-session) -----------
juce::File getLibraryRoot();
void setLibraryRoot (const juce::File&);

// Last folder browsed in the sample/wavetable file chooser, and separately
// for the convolution IR chooser (kept apart since these are typically
// browsed from very different locations, e.g. an SFX library vs. an
// impulse-response folder). Returns an invalid File when never set or the
// remembered folder is no longer reachable (e.g. its drive is unplugged).
juce::File getLastContentFolder();
void setLastContentFolder (const juce::File& pickedFile);
juce::File getLastIRFolder();
void setLastIRFolder (const juce::File& pickedFile);

// UI accent colour overrides, remembered across sessions. Getters return
// the fallback when the user has never customized.
juce::Colour getAccentColor (juce::Colour fallback);
juce::Colour getAccentModColor (juce::Colour fallback);
void setAccentColors (juce::Colour accent, juce::Colour accentMod);

// Whether the picker links both accents to one colour input.
bool getAccentsLinked();
void setAccentsLinked (bool linked);

// Favorite presets (machine preference, like the theme). Keys are
// "<category>/<name>" so they survive the presets root moving.
juce::StringArray getFavoritePresets();
bool isPresetFavorite (const juce::String& key);
void setPresetFavorite (const juce::String& key, bool favorite);

// Quick structural check: does this folder look like an SPASynth library
// (at least one pack subfolder containing WAVs, or WAVs directly in the
// root)? Agrees exactly with whether scanLibrary() would yield a pack.
bool looksLikeLibrary (const juce::File&);

// The standard install locations the content installers write to, most
// preferred first. The plugin probes these so users never have to point
// SPASynth at the library manually.
std::vector<juce::File> defaultLibraryLocations();

// Expands company dirs into an ordered candidate list: the canonical
// "<dir>/<libraryName>" for every dir first, then every other existing
// subfolder of each dir as a fallback (a renamed library, the starter
// library dragged out of its zip, a lone add-on pack). Pure given its
// inputs (testable); discoverLibrary() still vets every candidate.
std::vector<juce::File> expandLibraryCandidates (const std::vector<juce::File>& companyDirs,
                                                 const juce::String& libraryName);

// Pure discovery over a candidate list (testable).
juce::File discoverLibrary (const std::vector<juce::File>& candidates);

// The library root SPASynth should use right now: the configured root if it
// still exists on disk (even if it currently has zero packs -- a user's
// explicit folder choice is never second-guessed), otherwise the first valid
// default location (which is then persisted). Returns an invalid File only
// if nothing is found — the manual "Set Library Folder..." fallback covers
// that case.
juce::File findLibraryRoot();

// Where presets live: <app data>/Silverplatter Audio/SPASynth/Presets with
// Factory/<Category>/ and User/ underneath.
juce::File defaultPresetsRoot();

// Optional ownership stamp ("Licensed to name@example.com — Pro Edition"),
// shown in the editor footer when present. Purely informational — never
// gates anything (see EULA: no activation, no phone-home). Looked for as
// license.txt in the library root, then next to the presets folder.
juce::String getLicenseLine();
juce::String licenseLineFromFile (const juce::File&);   // pure parser (testable)

// --- Portable paths ---------------------------------------------------------
// Sample/wavetable paths inside the library are stored as "$LIB$/..." so
// presets and sessions survive the library living elsewhere on a customer's
// machine.
juce::String toPortable (const juce::File&, const juce::File& libraryRoot);
juce::File fromPortable (const juce::String&, const juce::File& libraryRoot);

} // namespace spa::library
