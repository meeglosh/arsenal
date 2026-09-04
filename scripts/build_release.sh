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
# Blow away only THIS version's staging dirs before recreating them (never
# touch other version folders — e.g. the 1.0.2/1.0.3 folders intentionally
# keep their old library zips per docs/shopify-setup-guide.md) so a rerun of
# this script can't leave stale files (an old .pkg, docs from a previous
# pass, a partial Library/) mixed in with the fresh ones. mkdir -p right
# after recreates the empty Library/ subdir by design (installer-iteration
# folders — library zips get cloned in separately at actual upload time, or
# copied below when a real $LIBRARY was given).
for sku in Standard Pro; do
    folder="$DIST/shopify/SPASynth-$sku-$VERSION"
    rm -rf "$folder"
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

# --- Loud unsigned-build warning ------------------------------------------------
# Signing is opt-in via env vars in installers/macos/build_installer.sh
# (SPASYNTH_CODESIGN_IDENTITY for the bundles, SPASYNTH_INSTALLER_IDENTITY
# for the pkg, SPASYNTH_NOTARIZE_PROFILE for notarization). That script
# already fails closed on a real signing/notarization error: it runs under
# `set -e -u` and every codesign/productsign/notarytool/stapler call is a
# plain foreground command with no `|| true` escape hatch, so a failure
# there aborts build_installer.sh non-zero, which (this script also being
# `set -e -u`) aborts this script too — a broken signing step can never
# silently fall through to an unsigned pkg. This banner instead covers the
# OTHER case: identities correctly left unset on purpose (local/dev
# testing), where build_installer.sh's own behavior is to quietly `cp` an
# unsigned pkg and print one "note:" line. That's easy to miss when this
# script's output scrolls by, and an unsigned pkg is never something that
# should reach a Shopify folder unnoticed — so make it impossible to miss.
if [[ -z "${SPASYNTH_CODESIGN_IDENTITY:-}" || -z "${SPASYNTH_INSTALLER_IDENTITY:-}" ]]; then
    echo ""
    echo "############################################################"
    echo "##                                                        ##"
    echo "##   WARNING: THIS IS AN UNSIGNED BUILD                   ##"
    echo "##                                                        ##"
    echo "##   The staged .pkg in dist/installers/ and both         ##"
    echo "##   dist/shopify/SPASynth-*-$VERSION/ folders is NOT     ##"
    echo "##   signed/notarized. Do not upload it to Shopify or     ##"
    echo "##   send it to testers.                                  ##"
    echo "##                                                        ##"
    echo "##   Set SPASYNTH_CODESIGN_IDENTITY, SPASYNTH_INSTALLER_IDENTITY"
    echo "##   (and SPASYNTH_NOTARIZE_PROFILE to also notarize),    ##"
    echo "##   then re-run this script.                             ##"
    echo "##                                                        ##"
    echo "############################################################"
    echo ""
else
    echo ""
    echo "Build is signed (SPASYNTH_CODESIGN_IDENTITY + SPASYNTH_INSTALLER_IDENTITY set)."
    [[ -z "${SPASYNTH_NOTARIZE_PROFILE:-}" ]] \
        && echo "NOTE: SPASYNTH_NOTARIZE_PROFILE is not set -- the pkg is signed but NOT notarized/stapled."
fi

exit 0
