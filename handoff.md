# SPASynth handoff (2026-09-04)

Quick "start here" for the next session. Full detail lives in `CLAUDE.md`;
this is the short version. Check `git branch --show-current` first — active
work is on `audit-hardening`.

## Where we are

- **Nothing after 1.0.8 has ever been sent to Paul and Phil.** 1.0.9-1.0.12
  were each built and staged but superseded before going out (Mike bumps the
  number each time even unsent — his call, follow it).
- **v1.0.12 (main) is the staged release candidate**: full faceplate visual
  redesign + loop-point display + every fix batch (QWERTY focus incl. the
  VOICE call-out, library folder, dimming, silent preset switching, banks,
  1.0.9 hardening). Both installers staged in
  `dist/shopify/SPASynth-{Standard,Pro}-1.0.12/`, hashes verified.
  **CORRECTION of earlier docs: Mike has NEVER tested the 1.0.12 pkg** — the
  1.0.12 pkg was never installed (`/Library` still holds 1.0.11) and his
  attempts to test the latest work are blocked by the Logic issue below.
- **Branch `audit-hardening` (at `4d4fd95`, version 1.0.13)** answers an
  external Codex "NO-GO" audit: two hardening waves (concurrency/RT-safety,
  MIDI-Learn notify deferral, FX buffer scaling, atomic preset writes,
  hermetic tests, CI gates, bounded cancellable content loading). Suite
  385→431 ALL PASS, full-suite TSan clean, auval clean. Rejected audit items
  and the reasoning are in CLAUDE.md's 2026-09-04 section. NOT merged; Mike
  decides whether it ships as 1.0.13 before launch or lands after.

## Logic loading saga — RESOLVED 2026-09-04

Logic was reusing a cached "failed" verdict for version 1.0.13 and never
re-validating; two same-identity copies with different versions (1.0.13
dev copy in ~/Library, 1.0.11 in /Library) were registered at once. Fix:
signed+notarized 1.0.13 pkg from `audit-hardening` (6af7243) installed to
/Library (dev copies cleared), then Plug-in Manager → Reset & Rescan
Selection + relaunch Logic. **Mike confirmed 1.0.13 loads and is "good to
go".** pkg md5 `ce6e9c81630b8b222293fb62eb3339fa`, staged in
`dist/installers/` and `dist/shopify/SPASynth-{Standard,Pro}-1.0.13/`.
No Windows exe yet — CI builds only on pushes to `main`, so it needs the
merge. Full diagnosis in CLAUDE.md's 2026-09-04 section.

## New non-negotiable rules (added after this saga)

1. **Dev plugin copies (`~/Library/Audio/Plug-Ins/`) get rebuilt ONLY as a
   deliberate final step right before Mike tests** — never during agent
   verification passes. Logic scanning a half-relinked bundle poisons its
   per-version verdict.
2. **Never push any commit between pushing a release sha and fetching its CI
   Windows exe** (cancel-in-progress kills the run).
3. Notary profile (`SPASYNTH_NOTARY`) vanishes periodically; recreate in
   Terminal.app, NOT via the `!` prefix (instant 401 there).

## Mike's open decisions / tasks

- 1.0.13 is installed and loads in Logic. Still to actually exercise:
  audit-branch regression pass (rapid sample auditioning, MIDI Learn feel,
  Pluck automation, 8x oversampling flanger/vibrato) + the whole 1.0.12
  gauntlet (QWERTY everywhere, loose-WAV folder, dimming, silent preset
  clicks, banks, loop markers, redesign, soft bypass).
- Merge call: audit-hardening → main as 1.0.13 (recommended — it is the
  build Mike now has installed, and the Windows exe can only come from a
  push to main) vs. ship 1.0.12 and merge later.
- Business/legal (from the audit, only Mike can close): JUCE commercial
  license tier, Steinberg VST3 distribution agreement, EULA +
  Kenzora-vs-Silverplatter entity naming. Windows Authenticode remains his
  standing "no".
- Then the launch checklist: tester round vs announce, Shopify build-out,
  launch email/social posts (all written and current).

## Ritual reminders

- Verification ritual + invariants: CLAUDE.md. Suite:
  `build/SPASynthTests_artefacts/SPASynthTests` (431 assertions, hermetic —
  never touches real presets).
- Windows exe comes from CI draft releases: `scripts/fetch_windows_build.sh
  <sha7>` (explicit sha REQUIRED).
- Memory (`~/.claude/.../memory/`) mirrors all of this; update both when
  state changes.
