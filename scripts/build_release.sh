#!/bin/zsh
# One-shot release build: plugin (universal Release), tests, macOS installer,
# library packages, and the assembled Shopify deliverable folders.
#
#   ./scripts/build_release.sh [<library folder>]
#
# <library folder> defaults to ./library (the build_library.sh output). Pass
# "-" to skip library packaging (much faster; installers + docs only).
#
# Output layout (dist/):
#   installers/SPASynth-<v>-macOS.pkg        (signed if identities are set —
#                                             see installers/macos/build_installer.sh)
#   library/packs/<Pack>.zip                 88 add-on packs
#   library/SPASynth Starter Library.zip
#   shopify/SPASynth-Standard-<v>/           ready-to-zip download folders
#   shopify/SPASynth-Pro-<v>/
#
# The Windows installer is built by CI (windows job, Inno Setup) — download
# the artifact and drop it into both shopify folders before uploading.

set -e -u

REPO_ROOT="${0:A:h:h}"
cd "$REPO_ROOT"

LIBRARY="${1:-$REPO_ROOT/library}"
VERSION=$(sed -n 's/^project(SPASynth VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
DIST="$REPO_ROOT/dist"
BUILD="$REPO_ROOT/build-release"

echo "=== SPASynth $VERSION release build ==="

# --- 0. Clear any dev-build shadow copy -----------------------------------------
# Dev/auval builds (SPASYNTH_COPY_PLUGIN=ON by default) copy plugins into the
# user's ~/Library/Audio/Plug-Ins/. macOS's AudioComponent lookup prefers the
# user domain over the system domain (/Library, where the signed release
# installs), so a leftover dev copy silently shadows every subsequent signed
# install in any DAW, however recent or correctly signed it is. This bit us
# repeatedly (1.0.4, 1.0.8) whenever a dev copy from earlier verification work
# wasn't manually cleared before staging a release. Unconditional and
# automatic here so it can never again depend on remembering a manual step.
rm -rf ~/Library/Audio/Plug-Ins/Components/SPASynth.component \
       ~/Library/Audio/Plug-Ins/VST3/SPASynth.vst3
echo "cleared any ~/Library dev-build shadow copy"

# --- 1. Plugin (universal Release) + tests ------------------------------------
cmake -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DSPASYNTH_UNIVERSAL_BINARY=ON -DSPASYNTH_COPY_PLUGIN=OFF
cmake --build "$BUILD"
"$BUILD/SPASynthTests_artefacts/Release/SPASynthTests"

# --- 2. macOS installer ---------------------------------------------------------
mkdir -p "$DIST/installers"
"$REPO_ROOT/installers/macos/build_installer.sh" "$BUILD" "$DIST/installers"

# --- 3. Library packages --------------------------------------------------------
if [[ "$LIBRARY" != "-" ]]; then
    "$REPO_ROOT/scripts/package_library.sh" "$LIBRARY" "$DIST/library"
fi

# --- 4. Shopify download folders ------------------------------------------------
for sku in Standard Pro; do
    folder="$DIST/shopify/SPASynth-$sku-$VERSION"
    mkdir -p "$folder/Library"
    cp "$DIST/installers/SPASynth-$VERSION-macOS.pkg" "$folder/"
    cp packaging/docs/README.txt packaging/docs/QUICKSTART.txt \
       packaging/docs/EULA.txt "$folder/"

    if [[ "$LIBRARY" != "-" ]]; then
        if [[ "$sku" == "Standard" ]]; then
            cp "$DIST/library/SPASynth Starter Library.zip" "$folder/Library/"
        else
            cp "$DIST/library/SPASynth Pro Library"*.zip "$folder/Library/"
        fi
    fi
done

echo ""
echo "=== done ==="
echo ""
echo "Upload to Shopify Digital Downloads as individual file attachments"
echo "(every file is under the 5 GB cap; don't wrap them in one giant zip):"
echo ""
echo "  'SPASynth Standard' product — attach:"
echo "     shopify/SPASynth-Standard-$VERSION/SPASynth-$VERSION-macOS.pkg"
echo "     shopify/SPASynth-Standard-$VERSION/SPASynth-$VERSION-Windows.exe   (from CI)"
echo "     shopify/SPASynth-Standard-$VERSION/Library/SPASynth Starter Library.zip"
echo "     shopify/SPASynth-Standard-$VERSION/{README,QUICKSTART,EULA}.txt"
echo ""
echo "  'SPASynth Pro' product — attach:"
echo "     shopify/SPASynth-Pro-$VERSION/SPASynth-$VERSION-macOS.pkg"
echo "     shopify/SPASynth-Pro-$VERSION/SPASynth-$VERSION-Windows.exe        (from CI)"
echo "     shopify/SPASynth-Pro-$VERSION/Library/SPASynth Pro Library (Part N).zip  (all parts)"
echo "     shopify/SPASynth-Pro-$VERSION/{README,QUICKSTART,EULA}.txt"
echo ""
echo "  'Standard -> Pro Upgrade' product — attach:"
echo "     the same Pro Library part zips (library only; they already own the synth)"
echo ""
echo "  Add-on pack products (later): library/packs/<Pack>.zip, one per product"
echo ""
echo "Remaining manual steps:"
echo "  1. Download the spasynth-installer-Windows CI artifact into both shopify folders"
[[ -z "${SPASYNTH_INSTALLER_IDENTITY:-}" ]] \
    && echo "  2. (pkg is UNSIGNED - set SPASYNTH_*_IDENTITY env vars and re-run)"
exit 0
