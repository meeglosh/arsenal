#!/usr/bin/env bash
# Downloads the Windows installer .exe that CI published as an asset on a
# DRAFT GitHub Release (tag ci-windows-<short sha>). Windows CI stopped
# uploading via actions/upload-artifact because the account-wide Actions
# storage quota (0.5 GB, cumulative GB-month) was too small for the exe;
# release assets don't count against that quota (see .github/workflows/build.yml).
#
# Draft releases are NOT resolvable via `gh release download <tag>` (that
# uses the /releases/tags/<tag> endpoint, which does not return drafts) --
# so this lists releases via the API and downloads the asset by numeric id.
#
# Usage:
#   scripts/fetch_windows_build.sh <sha> [<outdir>]
#   scripts/fetch_windows_build.sh --latest [<outdir>]
#
#   <sha>       A commit SHA (full or short, e.g. the first 7 chars) to fetch
#               the matching ci-windows-<sha7> draft. REQUIRED -- there is no
#               default, on purpose (see below).
#   --latest    Explicitly opt in to grabbing the newest ci-windows-* draft
#               release instead of a specific commit.
#   <outdir>    Where to save the exe. Default: dist/installers/
#
# An explicit sha is required by default (no implicit "latest") so that a
# release build can't silently grab whatever the newest CI draft happens to
# be at the moment -- e.g. a build from a branch someone pushed after the
# commit actually being released, or a re-run that raced with an unrelated
# push. --latest still exists for convenience (e.g. quick local checks) but
# has to be asked for explicitly.
#
# Examples:
#   scripts/fetch_windows_build.sh a1b2c3d
#   scripts/fetch_windows_build.sh a1b2c3d4e5f6... dist/installers
#   scripts/fetch_windows_build.sh --latest
#   scripts/fetch_windows_build.sh --latest dist/installers

set -euo pipefail

usage() {
  cat >&2 <<'EOF'
usage: scripts/fetch_windows_build.sh <sha> [<outdir>]
       scripts/fetch_windows_build.sh --latest [<outdir>]

An explicit commit sha is required; pass --latest to opt in to fetching the
newest ci-windows-* draft release instead.
EOF
}

if [ "$#" -lt 1 ]; then
  echo "error: missing required <sha> (or --latest)." >&2
  usage
  exit 1
fi

if [ "$1" = "--latest" ]; then
  SHA_ARG="latest"
  OUT_DIR="${2:-dist/installers}"
elif [ "$1" = "latest" ]; then
  echo "error: 'latest' must now be requested explicitly via --latest, not as a bare argument." >&2
  usage
  exit 1
elif [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
  usage
  exit 0
else
  SHA_ARG="$1"
  OUT_DIR="${2:-dist/installers}"
fi

if ! command -v gh >/dev/null 2>&1; then
  echo "error: the GitHub CLI ('gh') is required but not found on PATH." >&2
  exit 1
fi
if ! command -v jq >/dev/null 2>&1; then
  echo "error: 'jq' is required but not found on PATH." >&2
  exit 1
fi

# Derive owner/repo: prefer `gh repo view` (works from anywhere gh is
# authenticated against the right repo), fall back to parsing the git
# remote (origin is https://github.com/meeglosh/SPASynth.git).
REPO=""
if REPO="$(gh repo view --json nameWithOwner --jq .nameWithOwner 2>/dev/null)"; then
  :
else
  REMOTE_URL="$(git remote get-url origin 2>/dev/null || true)"
  REPO="$(echo "$REMOTE_URL" | sed -E 's#^(https://github\.com/|git@github\.com:)##; s#\.git$##')"
fi

if [ -z "$REPO" ]; then
  echo "error: could not determine the GitHub owner/repo (checked 'gh repo view' and 'git remote get-url origin')." >&2
  exit 1
fi

if [ "$SHA_ARG" = "latest" ]; then
  TAG_FILTER='startswith("ci-windows-")'
else
  SHORT_SHA="${SHA_ARG:0:7}"
  TARGET_TAG="ci-windows-${SHORT_SHA}"
  TAG_FILTER=". == \"${TARGET_TAG}\""
fi

# Draft releases only show up via the list endpoint, not /releases/tags/<tag>.
RELEASE_JSON="$(gh api --paginate "repos/${REPO}/releases" \
  --jq "[.[] | select(.draft == true and (.tag_name | ${TAG_FILTER}))] | sort_by(.created_at) | reverse | .[0]")"

if [ -z "$RELEASE_JSON" ] || [ "$RELEASE_JSON" = "null" ]; then
  echo "error: no matching ci-windows-* draft release found for '${SHA_ARG}' in ${REPO}." >&2
  echo "CI may still be running: gh run list --limit 1" >&2
  exit 1
fi

FOUND_TAG="$(echo "$RELEASE_JSON" | jq -r '.tag_name')"
ASSET_ID="$(echo "$RELEASE_JSON" | jq -r '[.assets[] | select(.name | endswith(".exe"))][0].id // empty')"
ASSET_NAME="$(echo "$RELEASE_JSON" | jq -r '[.assets[] | select(.name | endswith(".exe"))][0].name // empty')"

if [ -z "$ASSET_ID" ] || [ -z "$ASSET_NAME" ]; then
  echo "error: draft release '${FOUND_TAG}' has no .exe asset attached." >&2
  echo "CI may still be running: gh run list --limit 1" >&2
  exit 1
fi

mkdir -p "$OUT_DIR"
OUT_PATH="${OUT_DIR%/}/${ASSET_NAME}"

# `gh release download` can't resolve a draft by tag -- fetch the asset
# directly by its numeric id instead.
gh api -H "Accept: application/octet-stream" "repos/${REPO}/releases/assets/${ASSET_ID}" > "$OUT_PATH"

if command -v md5 >/dev/null 2>&1; then
  MD5="$(md5 -q "$OUT_PATH")"
elif command -v md5sum >/dev/null 2>&1; then
  MD5="$(md5sum "$OUT_PATH" | awk '{print $1}')"
else
  MD5="(no md5/md5sum tool found)"
fi

echo "Fetched draft release: ${FOUND_TAG}"
echo "Saved to: ${OUT_PATH}"
echo "md5: ${MD5}"
