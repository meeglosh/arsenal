#include "Library.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace spa::library
{

namespace
{
    constexpr const char* portablePrefix = "$LIB$";

    // ONE PropertiesFile instance per process. Creating a fresh instance per
    // access (the previous scheme) meant each writer saved its own stale
    // snapshot of the file, clobbering keys other writers had set — the
    // classic "theme preference keeps resetting" bug.
    //
    // DeletedAtShutdown (not a plain static): PropertiesFile runs a save
    // timer, so it must be destroyed while JUCE's event system still exists.
    struct SettingsHolder : private juce::DeletedAtShutdown
    {
        SettingsHolder() = default;
        ~SettingsHolder() override { clearSingletonInstance(); }

        juce::PropertiesFile file { []
        {
            juce::PropertiesFile::Options options;
            options.applicationName = "SPASynth";
            options.folderName = "Silverplatter Audio/SPASynth";
            options.filenameSuffix = "settings";
            options.osxLibrarySubFolder = "Application Support";
            return options;
        }() };

        JUCE_DECLARE_SINGLETON (SettingsHolder, false)
    };

    JUCE_IMPLEMENT_SINGLETON (SettingsHolder)

    juce::PropertiesFile& settings()
    {
        return SettingsHolder::getInstance()->file;
    }
}

namespace
{
    void sortBySize (juce::Array<juce::File>& wavs)
    {
        std::sort (wavs.begin(), wavs.end(),
                   [] (const juce::File& a, const juce::File& b)
                   { return a.getSize() < b.getSize(); });
    }
}

std::vector<Pack> scanLibrary (const juce::File& root)
{
    std::vector<Pack> packs;

    if (! root.isDirectory())
        return packs;

    for (const auto& folder : root.findChildFiles (juce::File::findDirectories, false))
    {
        Pack pack;
        pack.name = folder.getFileName();
        pack.folder = folder;
        pack.wavs = folder.findChildFiles (juce::File::findFiles, true, "*.wav;*.WAV");

        if (pack.wavs.isEmpty())
            continue;

        sortBySize (pack.wavs);
        packs.push_back (std::move (pack));
    }

    // WAV files sitting directly in the root (no pack subfolder) count as a
    // pack of their own, named after the root folder itself. Covers the
    // common "customer points SPASynth at a plain folder of samples" case --
    // previously those files were invisible to the library entirely.
    // Non-recursive so this never re-counts anything already picked up by a
    // pack subfolder above.
    {
        auto loose = root.findChildFiles (juce::File::findFiles, false, "*.wav;*.WAV");
        if (! loose.isEmpty())
        {
            Pack pack;
            pack.name = root.getFileName();
            pack.folder = root;
            pack.wavs = std::move (loose);

            sortBySize (pack.wavs);
            packs.push_back (std::move (pack));
        }
    }

    std::sort (packs.begin(), packs.end(),
               [] (const Pack& a, const Pack& b)
               { return a.name.compareIgnoreCase (b.name) < 0; });

    return packs;
}

juce::File getLibraryRoot()
{
    const auto path = settings().getValue ("libraryRoot");
    return path.isNotEmpty() ? juce::File (path) : juce::File();
}

bool looksLikeLibrary (const juce::File& root)
{
    // Cheaper than scanLibrary() -- stops at the first hit instead of
    // building/sorting the whole pack list -- but must agree with it
    // exactly: a root "looks like a library" iff scanLibrary(root) would
    // yield at least one pack.
    if (! root.isDirectory())
        return false;

    if (! root.findChildFiles (juce::File::findFiles, false, "*.wav;*.WAV").isEmpty())
        return true;   // loose WAVs directly in the root -- the synthetic pack

    for (const auto& folder : root.findChildFiles (juce::File::findDirectories, false))
        if (! folder.findChildFiles (juce::File::findFiles, true, "*.wav;*.WAV").isEmpty())
            return true;

    return false;
}

std::vector<juce::File> defaultLibraryLocations()
{
    const juce::String company = "Silverplatter Audio";

    std::vector<juce::File> companyDirs;

    // Shared, user-writable, visible across accounts — the primary install
    // target (/Users/Shared on macOS, Public Documents on Windows).
    companyDirs.push_back (juce::File::getSpecialLocation (juce::File::commonDocumentsDirectory)
                               .getChildFile (company));

    // System-wide application data.
   #if JUCE_MAC
    companyDirs.push_back (juce::File ("/Library/Application Support").getChildFile (company));
   #else
    companyDirs.push_back (juce::File::getSpecialLocation (juce::File::commonApplicationDataDirectory)
                               .getChildFile (company));
   #endif

    // Per-user application data.
    companyDirs.push_back (juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                              #if JUCE_MAC
                               .getChildFile ("Application Support")
                              #endif
                               .getChildFile (company));

    // User documents (people drag content there more often than anywhere).
    companyDirs.push_back (juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                               .getChildFile (company));

    return expandLibraryCandidates (companyDirs, "SPASynth Library");
}

std::vector<juce::File> expandLibraryCandidates (const std::vector<juce::File>& companyDirs,
                                                 const juce::String& libraryName)
{
    std::vector<juce::File> candidates;

    for (const auto& dir : companyDirs)
        candidates.push_back (dir.getChildFile (libraryName));

    // Fallback: any other folder sitting in a company dir. Customers end up
    // here by dragging the inner folder out of the zip or renaming it — the
    // README promises "no pointing, no scanning dialogs", so probe those too.
    // discoverLibrary() vets each candidate with looksLikeLibrary(), and the
    // canonical names above always win when present.
    for (const auto& dir : companyDirs)
    {
        auto children = dir.findChildFiles (juce::File::findDirectories, false);
        std::sort (children.begin(), children.end(),
                   [] (const juce::File& a, const juce::File& b)
                   { return a.getFullPathName().compareIgnoreCase (b.getFullPathName()) < 0; });

        for (const auto& child : children)
            if (std::find (candidates.begin(), candidates.end(), child) == candidates.end())
                candidates.push_back (child);
    }

    return candidates;
}

juce::File discoverLibrary (const std::vector<juce::File>& candidates)
{
    for (const auto& candidate : candidates)
        if (looksLikeLibrary (candidate))
            return candidate;

    return {};
}

juce::File findLibraryRoot()
{
    const auto configured = getLibraryRoot();

    // A user-chosen root is never second-guessed once it exists on disk --
    // even if it currently has zero packs (an empty folder, or one they're
    // about to fill). Re-validating it against looksLikeLibrary() here used
    // to silently discard a folder the user had just picked (falling back to
    // rediscovering/overwriting with a default install location, with no
    // message shown) -- that was the actual bug. Auto-discovery only kicks
    // in when the configured path itself is gone (unplugged drive, deleted
    // folder) or was never set.
    if (configured.isDirectory())
        return configured;

    const auto discovered = discoverLibrary (defaultLibraryLocations());
    if (discovered.isDirectory())
        setLibraryRoot (discovered);
    else if (configured != juce::File())
        setLibraryRoot ({});   // stop returning a dead path (e.g. an unplugged drive)

    return discovered;
}

void setLibraryRoot (const juce::File& root)
{
    settings().setValue ("libraryRoot", root.getFullPathName());
    settings().saveIfNeeded();
}

namespace
{
    juce::File getLastFolder (const char* key)
    {
        const auto path = settings().getValue (key);
        const juce::File folder (path);
        return folder.isDirectory() ? folder : juce::File();
    }

    void setLastFolder (const char* key, const juce::File& pickedFile)
    {
        settings().setValue (key, pickedFile.getParentDirectory().getFullPathName());
        settings().saveIfNeeded();
    }
}

juce::File getLastContentFolder() { return getLastFolder ("lastContentFolder"); }
void setLastContentFolder (const juce::File& pickedFile) { setLastFolder ("lastContentFolder", pickedFile); }
juce::File getLastIRFolder() { return getLastFolder ("lastIRFolder"); }
void setLastIRFolder (const juce::File& pickedFile) { setLastFolder ("lastIRFolder", pickedFile); }

juce::Colour getAccentColor (juce::Colour fallback)
{
    const auto stored = settings().getValue ("accentColor");
    return stored.isNotEmpty() ? juce::Colour::fromString (stored) : fallback;
}

juce::Colour getAccentModColor (juce::Colour fallback)
{
    const auto stored = settings().getValue ("accentModColor");
    return stored.isNotEmpty() ? juce::Colour::fromString (stored) : fallback;
}

void setAccentColors (juce::Colour accent, juce::Colour accentMod)
{
    settings().setValue ("accentColor", accent.toString());
    settings().setValue ("accentModColor", accentMod.toString());
    settings().saveIfNeeded();
}

bool getAccentsLinked()
{
    // Linked by default: both accents ship as one colour (#51D0BF).
    return settings().getBoolValue ("accentsLinked", true);
}

void setAccentsLinked (bool linked)
{
    settings().setValue ("accentsLinked", linked);
    settings().saveIfNeeded();
}

juce::StringArray getFavoritePresets()
{
    auto keys = juce::StringArray::fromLines (settings().getValue ("favoritePresets"));
    keys.removeEmptyStrings();
    return keys;
}

bool isPresetFavorite (const juce::String& key)
{
    return getFavoritePresets().contains (key);
}

void setPresetFavorite (const juce::String& key, bool favorite)
{
    auto keys = getFavoritePresets();
    if (favorite)
        keys.addIfNotAlreadyThere (key);
    else
        keys.removeString (key);

    settings().setValue ("favoritePresets", keys.joinIntoString ("\n"));
    settings().saveIfNeeded();
}

namespace
{
    juce::File& presetsRootOverride()
    {
        static juce::File override;
        return override;
    }
}

void setPresetsRootOverride (const juce::File& root)
{
    presetsRootOverride() = root;
}

juce::File defaultPresetsRoot()
{
    if (presetsRootOverride() != juce::File())
        return presetsRootOverride();

    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Silverplatter Audio").getChildFile ("SPASynth")
        .getChildFile ("Presets");
}

juce::String licenseLineFromFile (const juce::File& file)
{
    if (! file.existsAsFile())
        return {};

    juce::StringArray lines;
    lines.addLines (file.loadFileAsString());
    for (const auto& line : lines)
        if (line.trim().isNotEmpty())
            return line.trim();

    return {};
}

juce::String getLicenseLine()
{
    for (const auto& candidate : { findLibraryRoot().getChildFile ("license.txt"),
                                   defaultPresetsRoot().getParentDirectory()
                                       .getChildFile ("license.txt") })
    {
        const auto line = licenseLineFromFile (candidate);
        if (line.isNotEmpty())
            return line;
    }

    return {};
}

juce::String toPortable (const juce::File& f, const juce::File& libraryRoot)
{
    if (libraryRoot.isDirectory() && f.isAChildOf (libraryRoot))
        return portablePrefix + f.getRelativePathFrom (libraryRoot)
                                  .replaceCharacter ('\\', '/');

    return f.getFullPathName();
}

juce::File fromPortable (const juce::String& s, const juce::File& libraryRoot)
{
    if (s.startsWith (portablePrefix))
    {
        const auto resolved = libraryRoot.getChildFile (
            s.fromFirstOccurrenceOf (portablePrefix, false, false));

        // Defence in depth only, not a new trust boundary: getChildFile()
        // resolves ".." segments, so a hand-crafted $LIB$/../../etc/hosts
        // preset could otherwise walk outside the library root. (Presets can
        // already reference arbitrary absolute paths via the branch below --
        // that's by design, so customers can point SPASynth at their own
        // existing sample folders -- this only tightens the $LIB$-prefixed
        // branch, which is supposed to stay inside the root.) Callers already
        // treat "file doesn't exist" as a normal case (missing/moved preset
        // content), so returning an invalid File here is handled the same way.
        if (libraryRoot.isDirectory()
            && resolved != libraryRoot
            && ! resolved.isAChildOf (libraryRoot))
            return {};

        return resolved;
    }

    return juce::File (s);
}

} // namespace spa::library
