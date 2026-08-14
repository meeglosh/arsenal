# SPASynth handoff (2026-08-14)

Quick "start here" for the next session. Full detail lives in `CLAUDE.md`; this
is the short version.

## Where we are

- **v1.0.6 (`97d86e6`, 2026-08-05) WAS SENT to Paul and Phil** (Mike confirmed).
  Two tester-feedback fixes: FDN reverb wet-path gain normalization, and
  RANDOMIZE ALL headphone-safety guards.
- **v1.0.7 (`2f7908d` + `537eede` + `a8d639b` + `264dec9`, 2026-08-06) is built,
  signed, and staged — but NOT sent to anyone yet.** It fixes a serious bug
  Mike hit personally in Logic: a saved session, reopened the next day,
  produced intermittent loud noise blasts even while completely idle.
  **Mike's own validation is still pending** — the definitive test is
  reopening the *original* affected Logic session (not the copied-channel
  workaround) several times on 1.0.7 and confirming no blasts, before
  deciding whether to send to Paul/Phil.
- Both 1.0.6 installers verified: macOS pkg md5
  `7c52c6ddd7419c3bca43a6250c90c4ad`, Windows exe md5
  `699b920ff608b0dd9e71cb1f653cea56`, byte-identical across
  `dist/installers/` and `dist/shopify/SPASynth-{Standard,Pro}-1.0.6/`.
- Both 1.0.7 installers verified: macOS pkg signed + notarized + stapled,
  `spctl` accepted, `minos 11.0`, md5 `7c57e209cf97a926807309864ef97709`;
  Windows exe from CI run `31129966771`, md5
  `ea4c063223655774eb928a0e3a77f4d8`; byte-identical across
  `dist/installers/` and `dist/shopify/SPASynth-{Standard,Pro}-1.0.7/`.
- Suite is at **181 assertions, ALL PASS**. `auval` SUCCEEDED on 1.0.7.
- Repo is **PRIVATE** as of 2026-08-14.

## What shipped in 1.0.6 (with testers)

- `8266360` — FDN reverb wet-path gain normalization. Phil reported MIX was
  oversensitive (10% already too wet) and 100% mix clipped/distorted. Root
  cause: ~+12dB structural over-gain in the wet path (delay-line injection
  and output-tap summing both ran at unity instead of the correct 0.5
  scaling for N=4 lines). Fixed; factory `reverbMix` presets retuned upward
  (0.25→0.4, 0.45→0.6) to compensate for the old hot path they'd been
  ear-tuned against.
- `f00b6e9` — RANDOMIZE ALL headphone-safety guards, after Mike hit
  deafening spikes from stacked "reasonable" rolls. Oscillator levels get a
  uniform dB trim if combined linear gain exceeds a 1.25 budget; the limiter
  is left enabled at transparent defaults after any FX-unlocked roll as a
  safety net. 30-roll regression test added.

## What's in 1.0.7 (staged, not sent)

Fixes intermittent loud noise blasts on Logic session reload (blasts happen
even while idle — matches delay repeats recirculating on their own clock).

- **`2f7908d` — root cause, worth understanding for future sessions.**
  Verified against the JUCE AU wrapper source directly: `processBlock`
  takes `getCallbackLock()`, but AU's `setStateInformation` (called during
  Logic project load) takes **no lock at all**, and `apvts.replaceState()`
  updates parameters one at a time (JUCE's own docs say this is not
  realtime-safe). So `processBlock` could race in mid-restore and render
  against a half-old/half-new parameter set — an unstable coefficient
  combo that injected an energy burst into the FX chain's feedback
  structures (delay near-unity feedback → slow decaying re-emission,
  exactly matching the symptom). A fresh instance (channel-copy workaround)
  has no concurrent audio thread to race, so it comes up clean. Fix: wrap
  the state-mutating core of `restoreStateTree` in `getCallbackLock()`
  (blocking file I/O stays outside the lock). Deadlock-checked: no listener
  or other caller already holds the lock.
- `537eede` — FX modules with recursive state (EQ bands, phaser/flanger)
  were freezing hot/stale state on disable and resuming from it on
  re-enable. Added edge-triggered state clears on disable→enable for
  ParametricEQ, ModEffect, TremVib. New `fxToggleBlastTest`.
- `a8d639b` — defense-in-depth output safety net: NaN/Inf scan on the final
  output buffer every block (silences + flags `fxChain.reset()` via the
  existing 150ms timer if found), plus a hard ±4.0 (+12dBFS) output clamp
  at the very end of `processBlock` so no future bug can produce an
  arbitrarily loud output.

## Disk cleanup (2026-08-14, docs-only, no version bump)

`dist/shopify/` was eating ~70GB apparent (93% full disk). Cause:
`SPASynth-{Standard,Pro}-1.0.2/` and `-1.0.3/` each still had a full
`cp`-duplicated library copy left over from before `build_release.sh`
stopped copying the library into version folders (1.0.4+ have empty
`Library/` subdirs by design). Deleted those old copies (verified
byte-identical to the canonical `dist/library/` via md5 first). Freed 35GB
(61G → 96G free; less than 70GB because APFS had already clone-shared some
blocks). `dist/library/` is the one canonical archive going forward.
`docs/shopify-setup-guide.md` updated so future uploads copy the needed zip
in from `dist/library/` temporarily and delete it again after, instead of
leaving a permanent second copy in a version folder.

## Open items (Mike's manual steps)

1. **Validate 1.0.7 against the original affected Logic session** — the
   definitive test for the AU-wrapper-lock fix. Several reopens, confirm no
   blasts.
2. **Decide when to send 1.0.7 to Paul/Phil** (or go straight to launch).
3. **Windows real-DAW smoke test** — still the one untested surface.
4. **Shopify build-out** per `docs/shopify-setup-guide.md` — clone the
   needed library zip in from `dist/library/` temporarily per SKU.
5. Marketing-site update + announcement when ready.

## How to rebuild after a code fix (this worked all session)

Signing + notary are set up on Mike's machine (Developer ID certs in the login
keychain, `SPASYNTH_NOTARY` profile). Per fix:

```
export SPASYNTH_CODESIGN_IDENTITY="Developer ID Application: Kenzora Games (7K9WY5T49S)"
export SPASYNTH_INSTALLER_IDENTITY="Developer ID Installer: Kenzora Games (7K9WY5T49S)"
export SPASYNTH_NOTARIZE_PROFILE="SPASYNTH_NOTARY"
./scripts/build_release.sh -            # "-" skips the slow library repackage (unchanged)
```

Then Windows: push `main` to trigger the Windows-only-on-push CI, `gh run
download <id> -n spasynth-installer-Windows`, copy the pkg + exe into both
`dist/shopify` folders. Verify: one distinct md5 per installer across all
locations, `otool -l <standalone> | grep minos` -> `minos 11.0`, `spctl -a -t
install <pkg>` -> accepted. Ask Mike before rebuilding (he batches findings).

## Gotchas learned

- **A push can silently produce no CI run** if the repo visibility flip
  hasn't fully propagated yet (or Actions gets disabled briefly after a
  visibility change) — no error, no run appears. Hit this on 1.0.7: the
  first push right after going public produced nothing; a second push with
  an empty `ci: trigger Windows build` commit worked. Diagnostic: `gh run
  list --limit 1` shows nothing new for the pushed SHA after ~1 minute ->
  push an empty commit to retry. Wait-and-retry only, not a real fix (PAT
  lacks admin to inspect Actions settings).
- **`SPASYNTH_NOTARY` keychain profile has vanished more than once.**
  Recovery: Mike recreates it interactively, then `xcrun notarytool submit
  <pkg> --keychain-profile SPASYNTH_NOTARY --wait` + `xcrun stapler staple
  <pkg>` — the signed pkg does NOT need rebuilding. The build script dying
  at notarize also skips shopify-folder staging; stage manually (`mkdir
  folder/Library`, cp the pkg + the 3 packaging/docs txt files).
- **Test binary path**: `build/` was reconfigured without
  `CMAKE_BUILD_TYPE`, so the tests binary is
  `build/SPASynthTests_artefacts/SPASynthTests` (no `Debug/` subdir). A
  stale `Debug/` binary can silently run old tests — delete any `Debug/`
  copy you find.
- After any dev AU/VST3 build, clear `~/Library/Audio/Plug-Ins/
  {Components/SPASynth.component,VST3/SPASynth.vst3}` before Mike
  smoke-tests an installed release (dev copies shadow /Library in Logic).
- **Windows CI needs the repo PUBLIC.** Mike's PAT lacks admin, so he flips
  visibility himself around Windows CI runs (public for the push+build, back
  to private after). Repo is PRIVATE as of 2026-08-14.
- **`gh` token can expire mid-session** -> `gh auth login` (PAT needs `repo` +
  `workflow`; Actions:read is enough, no rerun via API).
- Upgrade-install note sent to testers: if a replaced plugin doesn't show up,
  rescan (Logic: Plug-in Manager -> Reset & Rescan Selection) + restart the
  DAW.
- Verification ritual for every change: build `SPASynthTests` and run it
  (expect ALL PASS, 181+ assertions), look at `--snapshot` renders for UI,
  then `auval` + `pluginval` strictness-8 for anything touching the audio
  thread.
- Load-bearing invariants (do not break): `CMAKE_OSX_DEPLOYMENT_TARGET=11.0`;
  append-only choice orders (FX module ids, EQ band types, voice/reverb/EQ
  character modes); RT-safety on the audio thread; per-preset `fxOrder` packed
  atomic; `restoreStateTree`'s state-mutating core now runs under
  `getCallbackLock()` — don't move it back out. See CLAUDE.md's invariants
  section.

## House style (customer-facing copy)

First-person company voice ("we"/"our"/Silverplatter Audio), never name
individuals, **no em dashes**, sound count 11,474.
