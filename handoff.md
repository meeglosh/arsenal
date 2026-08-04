# SPASynth handoff (2026-08-04)

Quick "start here" for the next session. Full detail lives in `CLAUDE.md`; this
is the short version.

## Where we are

- **v1.0.4 (`6087fef`, 2026-08-03) is with the partner testers.** Paul and
  Phil were sent an install link on 2026-08-03. Six tester-feedback fixes.
- **v1.0.5 (`02bba26`, 2026-08-04) is `HEAD`, pushed, signed + staged, but NOT
  sent to anyone yet.** It's a stability/hardening release from a full
  six-agent pre-release audit (RT-safety, concurrency/lifecycle,
  memory-safety, security/packaging, performance, release hygiene). Every
  crash/hang/UAF-class finding the audit turned up is fixed. Security came
  back clean: zero network code, no committed secrets, clean installers/CI.
- Versioning rule (Mike's call): bump as soon as a build has been SENT to
  anyone, testers included — that's why the audit work is 1.0.5 rather than
  more 1.0.4 commits.
- Both 1.0.5 installers are verified: macOS pkg signed + notarized + stapled,
  `spctl` accepted, `minos 11.0`, md5 `6d98568a7f9d18865c4b588c43a3c3ca`
  byte-identical across `dist/installers/` and
  `dist/shopify/SPASynth-{Standard,Pro}-1.0.5/`; Windows exe from CI run
  `30914554294`, md5 `4b1a1f5d4c55fd3d3e6f97789caa353a`, same three locations.
  The 1.0.4/1.0.5 shopify folders have **empty `Library/` subdirs by design**
  (installer-iteration folders); clone the library zips in from the
  1.0.3/1.0.2 folders when actually uploading to the store.

## What shipped in 1.0.4 (with testers)

Six tester-feedback fixes, one commit each: library rescan feedback when the
root vanishes (`361c474`), Convolve IR chooser greyed-WAVs fix + remember-
last-folder for both file choosers (`a7137ef`), Filter 1 on/off toggle w/ new
`filter1.enable` param defaulting on (`b14ff4c`), reverb Decay range 12s->8s +
Hall multiplier 1.4->1.2 (`2ba3713`), sample/wavetable loader retry on
drive-remount (`cdd730a`), Reset to Default settings-menu item via
`PresetManager::resetToDefault` (`15d9962`).

## What's in 1.0.5 (staged, not distributed)

Four commits from the audit: `b662bbf` (malformed-preset null-deref fix,
WAV loader clamps), `68dd764` (arp non-finite-ppq + zero-sample-block guards,
Convolve tail-length now includes IR + pre-delay, relaxed atomics), `0aefecb`
(`WeakReference`/`SafePointer` guards on every raw-`this` async site in the
processor and editor, `scaledMidi` de-allocated from `processBlock`),
`02bba26` (six new regression tests, suite now 176 assertions ALL PASS, dead
`PresetManager` apvts member removed, version bump, changelog).

**Deferred post-launch (found by the audit, not fixed now):**
`juce::dsp::Convolution` NonUniform{256} partitioning for long IRs; document
that the FX chain runs at the oversampled rate; FDN reverb's 4x
`std::sin()`-per-sample tail mod; `EqEditor` FFT running while its tab is
hidden; no `processBlockBypassed` override; `maxModDests=96` guard is
debug-only; CI hardening (`permissions:`, SHA-pinned actions); `$LIB$`
`../` traversal (defense-in-depth only); `PluckString`'s ~1MB/instance
preallocation. Full list in CLAUDE.md's 2026-08-04 section.

## Open items (Mike's manual steps)

1. ~~Re-private the GitHub repo~~ — **done**, repo is private as of 2026-08-04.
2. **Install + smoke-test the 1.0.5 macOS pkg** (`sudo installer -pkg
   dist/installers/SPASynth-1.0.5-macOS.pkg -target /`; the agent can't sudo).
3. **Windows real-DAW smoke test** — still the one untested surface.
4. **Decide when to send 1.0.5 to Paul/Phil** (or go straight to launch —
   1.0.5 is audit hardening, not new tester-facing surfaces).
5. **Shopify build-out** per `docs/shopify-setup-guide.md`.
6. Marketing-site update + announcement when ready.

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

- **`SPASYNTH_NOTARY` keychain profile vanished a second time** (2026-08-04).
  Same recovery as before: Mike recreates it interactively, then `xcrun
  notarytool submit <pkg> --keychain-profile SPASYNTH_NOTARY --wait` +
  `xcrun stapler staple <pkg>` — the signed pkg does NOT need rebuilding. The
  build script dying at notarize also skips shopify-folder staging; stage
  manually (mkdir `folder/Library`, cp the pkg + the 3 packaging/docs txt
  files).
- **Test binary moved.** `build/` was reconfigured without
  `CMAKE_BUILD_TYPE`, so the tests binary is now
  `build/SPASynthTests_artefacts/SPASynthTests` (no `Debug/` subdir). A stale
  `Debug/` binary silently ran old tests until caught and deleted
  (2026-08-04) — delete any `Debug/` copy you find.
- After any dev AU/VST3 build, clear `~/Library/Audio/Plug-Ins/
  {Components/SPASynth.component,VST3/SPASynth.vst3}` before Mike
  smoke-tests an installed release (dev copies shadow /Library in Logic — bit
  us on 1.0.4).
- **Windows CI needs the repo PUBLIC.** Mike's PAT lacks admin, so he flips
  visibility himself around Windows CI runs (public for the push+build, back
  to private after). Repo is PRIVATE as of 2026-08-04.
- **`gh` token expired mid-session once** -> `gh auth login` (PAT needs `repo` +
  `workflow`; Actions:read is enough, no rerun via API).
- Upgrade-install note sent to testers: if a replaced plugin doesn't show up,
  rescan (Logic: Plug-in Manager -> Reset & Rescan Selection) + restart the
  DAW.
- Verification ritual for every change: build `SPASynthTests` and run it
  (expect ALL PASS, 176+ assertions), look at `--snapshot` renders for UI,
  then `auval` + `pluginval` strictness-8 for anything touching the audio
  thread.
- Load-bearing invariants (do not break): `CMAKE_OSX_DEPLOYMENT_TARGET=11.0`;
  append-only choice orders (FX module ids, EQ band types, voice/reverb/EQ
  character modes); RT-safety on the audio thread; per-preset `fxOrder` packed
  atomic. See CLAUDE.md's invariants section.

## House style (customer-facing copy)

First-person company voice ("we"/"our"/Silverplatter Audio), never name
individuals, **no em dashes**, sound count 11,474.
