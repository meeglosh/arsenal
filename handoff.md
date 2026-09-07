# SPASynth handoff (2026-09-07)

Quick "start here" for the next session. Full detail lives in `CLAUDE.md`; this
is the short version.

## Where we are

- **2026-09-07: v1.0.14 built, staged, INSTALLED, and CONFIRMED by Mike.**
  1.0.13 got installed and confirmed (the VOICE close-window crash was gone),
  but before it went to testers Mike hit a NEW Logic crash recording a second
  MIDI track: arpeggiator + count-in, negative host ppq -> negative `%` ->
  out-of-bounds stack read. Fixed in `2585783`, version bumped to 1.0.14.
  Then an AddressSanitizer build of the test suite (now ritual step 1b, see
  below) turned up three more real bugs, fixed in `81236ad`: (a) FDNReverb
  read one float past a delay line on float-wrap rounding — almost certainly
  the cause of the reverb noise bursts that got `audit-hardening` abandoned
  on 2026-09-05; same guard added to delay/ModEffect/TremVib; (b) the VOICE
  call-out panel could outlive the processor on project close, now detached
  synchronously in `~ContentComponent`; (c) the intermittent
  `voicePanelEditorCloseTest` flake was a test bug (raw `CallOutBox*` across
  message pumps), now a SafePointer — this flake also aborts
  `build_release.sh` if it fires. 1.0.14 built + staged 2026-09-07: macOS pkg
  md5 `68edd892e562370c765c005627dfb376` (signed/notarized/stapled), Windows
  exe md5 `89003ae52f0c2905eba27f696de34df4` (draft release
  `ci-windows-81236ad`), byte-identical across `dist/installers/` and
  `dist/shopify/SPASynth-{Standard,Pro}-1.0.14/`. Docs commit `878bfc5`. Mike
  installed it (first attempt failed silently because he was given a
  relative pkg path from the wrong directory — **always give him an
  absolute pkg path**) and confirmed the recording crash is fixed. Repo was
  flipped PUBLIC by Mike for the Windows CI run and is still public.
  **Next: v1.0.15 is in progress** — tester-feedback improvements from
  Mike's own playtest sessions with Paul and Phil, starting with "enabled FX
  tabs show their label in bold." 1.0.14 has NOT been sent to Paul and Phil
  yet; the rest of the gauntlet + the 1.0.15 round come first. The
  `hardening-safe` branch (`d7f38c3`) still sits on 1.0.13's main — needs a
  rebase onto current main and Mike's Logic verification before any of it
  ships.

- **2026-09-06: 1.0.13 fixed the Logic crash on closing the window with the
  VOICE call-out open** (call-outs were parented to the AU wrapper's holder,
  which deletes its children; see CLAUDE.md history). Installed + confirmed
  by Mike, but superseded by 1.0.14 above before it reached testers.

- **2026-09-05: 1.0.12 is installed in /Library and confirmed working by
  Mike in Logic (his own session, playback clean).** The `audit-hardening`
  branch (old 1.0.13) was ABANDONED — its build produced reverb-triggered
  pulsing noise bursts that 1.0.12 does not. Cause was unknown at the time;
  **now believed found** — see the FDNReverb off-by-one fix above. Do not
  merge that branch as-is regardless; re-verify anything pulled from it.
  Full story + the Logic-loading lessons in CLAUDE.md's 2026-09-05 section.
  Everything below this bullet is older history.

- **v1.0.8, 1.0.9, and the RANDOMIZE-ALL focus fix are all long since
  confirmed and shipped** (QWERTY focus-steal fixes: the right flag is
  `setMouseClickGrabsKeyboardFocus`, not `setWantsKeyboardFocus` — see
  Gotchas below; the 1.0.9 hardening batch; the ~25-button focus fix that
  followed it). Full history in CLAUDE.md's 2026-08-21 through 2026-08-28
  sections if needed.
- GitHub Actions storage alerts are a known non-issue: the quota is
  account-wide across all of Mike's repos, not per-repo, and the alert
  reflects a cycle-peak measurement, not live usage.

## What's actually left before launch

1. **v1.0.15 feature round** — tester-feedback improvements from Mike's own
   playtests with Paul and Phil, starting with bolding enabled FX tab
   labels. Batch these, don't ship one at a time.
2. Finish the 1.0.14 gauntlet, then actually send a build to Paul and Phil
   (1.0.14 has never gone out).
3. Decide: one more tester round after that, or send the announcement
   directly once Mike's happy.
4. Shopify build-out per `docs/shopify-setup-guide.md`.
5. Send `docs/launch-email.md` / `docs/social-posts.md` (now current, reflect
   the full shipping feature set) — marketing site is already confirmed live
   and accurate by Mike.
6. Rebase `hardening-safe` (`d7f38c3`) onto current main and get Mike's Logic
   verification before shipping any of it.

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

**When handing Mike a pkg to install, always give an absolute path.** A
relative path from the wrong working directory failed silently on 1.0.14 —
the installer command just did nothing and looked like it worked.

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
  (expect ALL PASS), look at `--snapshot` renders for UI changes, then
  `auval` (+ `pluginval` strictness-8 if available) for anything touching
  the audio thread.
- **New step 1b (2026-09-07): also build and run the AddressSanitizer test
  suite in `build-asan/`.** This is what caught the FDNReverb off-path read
  (likely the real cause of the 1.0.13 audit-hardening noise bursts), the
  VOICE call-out lifetime bug, and a genuine test bug in
  `voicePanelEditorCloseTest` (fixed to use a SafePointer instead of a raw
  `CallOutBox*` across message pumps). That test's flake also aborts
  `build_release.sh` if it fires during a release build — if the release
  script dies there, it's the known flake, re-run.
- Load-bearing invariants (do not break): `CMAKE_OSX_DEPLOYMENT_TARGET=11.0`;
  append-only choice orders (FX module ids, EQ band types, voice/reverb/EQ
  character modes); RT-safety on the audio thread; per-preset `fxOrder`
  packed atomic. See CLAUDE.md's invariants section.

## House style (customer-facing copy)

First-person company voice ("we"/"our"/Silverplatter Audio), never name
individuals, **no em dashes**, sound count 11,474.
