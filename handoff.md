# SPASynth handoff (2026-09-05)

Quick "start here" for the next session. Full detail lives in `CLAUDE.md`; this
is the short version.

## Where we are

- **2026-09-05: 1.0.12 is installed in /Library and confirmed working by
  Mike in Logic (his own session, playback clean).** The `audit-hardening`
  branch (1.0.13) is ABANDONED — its build produced reverb-triggered pulsing
  noise bursts that 1.0.12 does not; cause never found. Do not merge it.
  Full story + the Logic-loading lessons in CLAUDE.md's 2026-09-05 section.
  Remaining: finish the 1.0.12 gauntlet, send to Paul and Phil, Shopify,
  announce. Delete the obsolete 1.0.13 files under dist/ before uploading.
  Everything below this bullet is older history.

- **v1.0.8 is confirmed working and shipped to Paul and Phil.** Fixed QWERTY
  (computer-keyboard) note input dying the instant a knob/dropdown was
  touched. The first attempt (`setWantsKeyboardFocus(false)`) was **wrong**
  — Mike tested it and reported it back broken. The correct flag is
  `setMouseClickGrabsKeyboardFocus(false)` (JUCE grabs focus on every mouse
  click unconditionally, via a flag separate from "wants focus" — traced
  through JUCE's actual source before landing the real fix). **Remember this
  for any future focus-stealing bug in this codebase.**
- **v1.0.9 is built, signed, staged — NOT yet tested by Mike.** A pre-launch
  performance/hardening batch (soft bypass, convolution efficiency, reverb
  CPU reduction, EQ analyzer gating, mod-dest capacity guard, `$LIB$` path
  clamp, lazy pluck buffers, CI permissions), done by Mike's request while
  there was time before launch, not in response to a bug.
- **A second focus-steal bug was found AFTER 1.0.9 was staged, and the fix is
  sitting UNCOMMITTED in the working tree right now.** The 1.0.8 fix only
  covered parameter controls (knobs/dropdowns) — it missed every plain
  action button (RANDOMIZE ALL, SAVE, preset nav, settings, panic, etc.),
  which have the identical defect. Fixed all ~25 of them (same
  `setMouseClickGrabsKeyboardFocus(false)` fix) across `SPASynthEditor.cpp`,
  `ModulePanels.cpp`, `PresetBrowser.cpp`. Build is clean, tests pass, a dev
  build is installed on Mike's machine for him to test — **but this has not
  been committed, and Mike has not yet confirmed it fixes RANDOMIZE ALL (or
  that nothing else broke).** Check `git status` first thing if picking this
  up — don't assume it's landed.
- **GitHub Actions storage alert (2026-08-25) — resolved as a non-issue,
  nothing to fix.** The quota is account-wide across all ~21 of Mike's
  repos, not per-repo; live storage everywhere totals ~11MB (all in
  spasynth, already correctly capped). The alert reflects a cycle-peak
  measurement, not current usage — resets 2026-09-01 on its own.

## What's actually left before launch

1. **Get the uncommitted focus-fix confirmed by Mike**, then commit it and
   fold it into whatever the next build is (1.0.9 if nothing's shipped yet,
   otherwise bump per the versioning rule below).
2. Mike test-drives 1.0.9's own hardening changes (separate ask from #1) —
   low risk, nothing user-facing except bypass behavior, just unverified.
3. Decide: one more tester round, or send the announcement directly once
   Mike's happy with his own testing.
4. Shopify build-out per `docs/shopify-setup-guide.md`.
5. Send `docs/launch-email.md` / `docs/social-posts.md` (now current, reflect
   the full shipping feature set) — marketing site is already confirmed live
   and accurate by Mike.

Windows real-DAW smoke test is **done** — Paul and Phil both tested Windows
on 1.0.8, no issues. Marketing site is **done** — confirmed live/accurate by
Mike directly.

## Versioning rule (Mike's call)

Bump the version the moment a build has been SENT to anyone, testers
included. If a build never left Mike's machine, overwrite it in place at the
same version number instead (happened once: 1.0.8's broken→corrected fix).

## How to rebuild after a code fix

Signing + notary are set up on Mike's machine (Developer ID certs in the
login keychain, `SPASYNTH_NOTARY` profile). Per fix:

```
export SPASYNTH_CODESIGN_IDENTITY="Developer ID Application: Kenzora Games (7K9WY5T49S)"
export SPASYNTH_INSTALLER_IDENTITY="Developer ID Installer: Kenzora Games (7K9WY5T49S)"
export SPASYNTH_NOTARIZE_PROFILE="SPASYNTH_NOTARY"
./scripts/build_release.sh -            # "-" skips the slow library repackage (unchanged)
```

This now **automatically clears any dev-build shadow copy** from
`~/Library/Audio/Plug-Ins/` as its first step (fixed after this bit us twice
— 1.0.4 and 1.0.8 — a leftover dev/auval build there silently shadows the
signed release in Logic since macOS prefers the user domain over
`/Library`). No longer a manual habit to remember for release builds
specifically, but still do it by hand after any ad hoc `cmake --build
... SPASynth_AU SPASynth_VST3` dev/auval run:
```
rm -rf ~/Library/Audio/Plug-Ins/Components/SPASynth.component ~/Library/Audio/Plug-Ins/VST3/SPASynth.vst3
```

Then Windows: push `main` to trigger the Windows-only-on-push CI, `gh run
download <id> -n spasynth-installer-Windows`, copy the pkg + exe into both
`dist/shopify` folders. Verify: one distinct md5 per installer across all
locations, `otool -l <standalone> | grep minos` -> `minos 11.0`, `spctl -a -t
install <pkg>` -> accepted. Ask Mike before rebuilding (he batches findings).

## Gotchas learned

- **`setMouseClickGrabsKeyboardFocus`, not `setWantsKeyboardFocus`, for any
  focus-stealing bug.** See the 1.0.8 section above — this cost a whole
  extra round-trip once already.
- **Dev-build shadow copies in `~/Library` silently override the installed
  release in Logic** (macOS prefers user domain over system domain for AU
  lookup). `build_release.sh` now clears this automatically; still do it by
  hand after any dev/auval build outside that script.
- **A push right after flipping the repo public can silently fail to
  trigger CI** (no run appears, no error) — retry with an empty
  `ci: trigger` commit a bit later. Happened on 1.0.7.
- **Repo is currently PUBLIC, left that way on purpose** (Mike's call as of
  2026-08-21, to avoid blocking agent progress) — don't prompt him to
  re-private it unless he asks.
- **GitHub Actions storage quota is account-wide**, not per-repo, and the
  usage-alert email reflects cycle-peak/cumulative usage, not a live
  snapshot — check `gh api repos/{owner}/{repo}/actions/artifacts` and
  `.../actions/cache/usage` across *all* repos before assuming spasynth is
  the cause.
- Notary profile has vanished twice; recovery = Mike recreates it
  interactively, then `xcrun notarytool submit <pkg> --keychain-profile
  SPASYNTH_NOTARY --wait` + `xcrun stapler staple <pkg>` — the signed pkg
  does NOT need rebuilding.
- Test binary: `build/SPASynthTests_artefacts/SPASynthTests` (no `Debug/`
  subdir — `build/` was reconfigured without `CMAKE_BUILD_TYPE`).
- Verification ritual for every change: build `SPASynthTests` and run it
  (expect ALL PASS, 185+ assertions), look at `--snapshot` renders for UI
  changes, then `auval` (+ `pluginval` strictness-8 if available) for
  anything touching the audio thread.
- Load-bearing invariants (do not break): `CMAKE_OSX_DEPLOYMENT_TARGET=11.0`;
  append-only choice orders (FX module ids, EQ band types, voice/reverb/EQ
  character modes); RT-safety on the audio thread; per-preset `fxOrder`
  packed atomic. See CLAUDE.md's invariants section.

## House style (customer-facing copy)

First-person company voice ("we"/"our"/Silverplatter Audio), never name
individuals, **no em dashes**, sound count 11,474.
