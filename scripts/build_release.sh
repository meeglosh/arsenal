#!/bin/zsh
# One-shot release build: plugin (universal Release), tests, macOS installer,
# library packages, and the assembled Shopify deliverable folders.
#
#   ./scripts/build_release.sh [<library folder>]
#   ./scripts/build_release.sh --stage-only <version>
#
# <library folder> defaults to ./library (the build_library.sh output). Pass
# "-" to skip library packaging (much faster; installers + docs only).
#
# --stage-only <version> re-runs ONLY the Shopify-folder staging step (4)
# against an already-built dist/installers/SPASynth-<version>-macOS.pkg —
# use this after a notarization failure is fixed (see scripts/notarize.sh)
# without rebuilding the plugin.
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
#
# Notarization is a separate step, scripts/notarize.sh (see that file for
# why — the notarytool keychain profile has repeatedly vanished on this
# machine). This script signs the pkg here, then calls notarize.sh.

set -e -u

REPO_ROOT="${0:A:h:h}"
cd "$REPO_ROOT"

DIST="$REPO_ROOT/dist"
BUILD="$REPO_ROOT/build-release"

# --- Shopify folder staging (step 4), factored out so it can be re-run  -----
# standalone after a notarize-then-fix cycle, without rebuilding anything.
stage_shopify() {
    local VERSION="$1" LIBRARY="${2:--}"
    for sku in Standard Pro; do
        local folder="$DIST/shopify/SPASynth-$sku-$VERSION"
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
    echo "staged: dist/shopify/SPASynth-{Standard,Pro}-$VERSION"
    md5 "$DIST/installers/SPASynth-$VERSION-macOS.pkg" \
        "$DIST/shopify/SPASynth-Standard-$VERSION/SPASynth-$VERSION-macOS.pkg" \
        "$DIST/shopify/SPASynth-Pro-$VERSION/SPASynth-$VERSION-macOS.pkg"
}

if [[ "${1:-}" == "--stage-only" ]]; then
    [[ -n "${2:-}" ]] || { echo "usage: $0 --stage-only <version>" >&2; exit 2; }
    stage_shopify "$2" "-"
    exit 0
fi

LIBRARY="${1:-$REPO_ROOT/library}"
VERSION=$(sed -n 's/^project(SPASynth VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)

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

# --- 2. macOS installer (sign only here; notarize is a separate step) -----------
mkdir -p "$DIST/installers"
# build_installer.sh notarizes inline if SPASYNTH_NOTARIZE_PROFILE is set in
# the environment; hide it during the call so it only signs here, and we
# drive notarization ourselves below (via scripts/notarize.sh, which tries
# the ~/.config/spasynth/notary.env fallback before this same profile).
_saved_notarize_profile="${SPASYNTH_NOTARIZE_PROFILE:-}"
unset SPASYNTH_NOTARIZE_PROFILE
"$REPO_ROOT/installers/macos/build_installer.sh" "$BUILD" "$DIST/installers"
[[ -n "$_saved_notarize_profile" ]] && export SPASYNTH_NOTARIZE_PROFILE="$_saved_notarize_profile"

PKG="$DIST/installers/SPASynth-$VERSION-macOS.pkg"
if [[ -n "${SPASYNTH_INSTALLER_IDENTITY:-}" ]]; then
    if "$REPO_ROOT/scripts/notarize.sh" "$PKG"; then
        echo "notarized: $PKG"
    else
        status=$?
        echo ""
        echo "WARNING: notarization failed (exit $status) - $PKG is signed but"
        echo "NOT notarized/stapled. Fix credentials (see scripts/notarize.sh"
        echo "for the two options), then:"
        echo "  scripts/notarize.sh '$PKG'"
        echo "  scripts/build_release.sh --stage-only $VERSION"
        echo "Skipping Shopify folder staging for now."
        exit 69
    fi
else
    echo "note: unsigned pkg, skipping notarization (set SPASYNTH_INSTALLER_IDENTITY to sign)"
fi

# --- 3. Library packages --------------------------------------------------------
if [[ "$LIBRARY" != "-" ]]; then
    "$REPO_ROOT/scripts/package_library.sh" "$LIBRARY" "$DIST/library"
fi

# --- 4. Shopify download folders ------------------------------------------------
stage_shopify "$VERSION" "$LIBRARY"

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
