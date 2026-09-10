#!/bin/zsh
# Notarizes + staples an already-signed macOS installer pkg, independent of
# whatever notarytool keychain profile happens to exist on this machine.
#
#   ./scripts/notarize.sh <pkg>
#
# The SPASYNTH_NOTARY keychain profile has vanished repeatedly (six times as
# of 2026-09-09; `security find-generic-password -s com.apple.gke.notary.tool`
# finds nothing after each loss) with no known root cause. This script tries
# a credentials FILE first, since that has proven reliable, and only falls
# back to the keychain profile:
#
#   1. ~/.config/spasynth/notary.env, if present — a shell file (mode 600,
#      created by hand, NEVER inside the repo) defining:
#        SPASYNTH_NOTARY_APPLE_ID=you@example.com
#        SPASYNTH_NOTARY_TEAM_ID=7K9WY5T49S
#        SPASYNTH_NOTARY_PASSWORD=app-specific-password
#      submitted via `notarytool ... --apple-id --team-id --password`.
#   2. Otherwise, the SPASYNTH_NOTARIZE_PROFILE keychain profile (today's
#      behavior), via `notarytool ... --keychain-profile`.
#   3. If neither is usable, prints instructions and exits non-zero without
#      touching the pkg.
#
# The password is never echoed and this script never sets -x. Passing it as
# a notarytool argument (visible in `ps` for the instant the process runs)
# is accepted here since this is a single-user dev Mac.

set -e -u

PKG="${1:-}"
if [[ -z "$PKG" ]]; then
    echo "usage: $0 <pkg>" >&2
    exit 2
fi
if [[ ! -f "$PKG" ]]; then
    echo "error: no such file: $PKG" >&2
    exit 2
fi

ENV_FILE="$HOME/.config/spasynth/notary.env"

submitted=0
if [[ -f "$ENV_FILE" ]]; then
    set -a
    source "$ENV_FILE"
    set +a
    if [[ -n "${SPASYNTH_NOTARY_APPLE_ID:-}" && -n "${SPASYNTH_NOTARY_TEAM_ID:-}" \
          && -n "${SPASYNTH_NOTARY_PASSWORD:-}" ]]; then
        echo "notarizing via $ENV_FILE (this can take a few minutes)..."
        xcrun notarytool submit "$PKG" \
              --apple-id "$SPASYNTH_NOTARY_APPLE_ID" \
              --team-id "$SPASYNTH_NOTARY_TEAM_ID" \
              --password "$SPASYNTH_NOTARY_PASSWORD" \
              --wait
        submitted=1
    else
        echo "warning: $ENV_FILE exists but is missing one of" \
             "SPASYNTH_NOTARY_APPLE_ID/_TEAM_ID/_PASSWORD - ignoring it" >&2
    fi
fi

if [[ "$submitted" -eq 0 ]]; then
    if [[ -n "${SPASYNTH_NOTARIZE_PROFILE:-}" ]]; then
        echo "notarizing via keychain profile $SPASYNTH_NOTARIZE_PROFILE" \
             "(this can take a few minutes)..."
        xcrun notarytool submit "$PKG" \
              --keychain-profile "$SPASYNTH_NOTARIZE_PROFILE" --wait
        submitted=1
    fi
fi

if [[ "$submitted" -eq 0 ]]; then
    cat >&2 <<MSG
error: no working notarization credentials.

Fix one of these:
  1. (preferred) Create $ENV_FILE by hand, mode 600, with:
       SPASYNTH_NOTARY_APPLE_ID=your-apple-id@example.com
       SPASYNTH_NOTARY_TEAM_ID=7K9WY5T49S
       SPASYNTH_NOTARY_PASSWORD=an-app-specific-password
  2. Or recreate the keychain profile in Terminal.app (not this shell):
       xcrun notarytool store-credentials SPASYNTH_NOTARY \\
         --apple-id <id> --team-id 7K9WY5T49S
     and make sure SPASYNTH_NOTARIZE_PROFILE=SPASYNTH_NOTARY is exported.

Then re-run: $0 "$PKG"
MSG
    exit 69
fi

xcrun stapler staple "$PKG"
spctl -a -vv -t install "$PKG" 2>&1 | tee /tmp/spasynth-spctl-out.$$ >&2
grep -m1 '^source=' /tmp/spasynth-spctl-out.$$ || true
rm -f /tmp/spasynth-spctl-out.$$

echo "notarized + stapled: $PKG"
