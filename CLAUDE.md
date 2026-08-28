# SPASynth — project state & working guide

Commercial hybrid soft synth from **Silverplatter Audio**, primarily a
boutique sound-effects library company (SPASynth is our synth product).
Customer-facing materials speak in the company's own first-person voice
("we"/"our"/"Silverplatter Audio") and never name individuals. Sold via
Shopify. JUCE 8.0.14 (submodule `libs/JUCE`), C++20, CMake + Ninja.
Formats: AU/VST3/Standalone (macOS universal), VST3/Standalone (Windows x64).
AAX deliberately out for v1. Original spec: `spasynth-claude-code-brief.md`
(the project was renamed Arsenal → SPASynth; the repo folder is still
`arsenal`, plugin code `SpSy`, manufacturer `SpAu`).

## Current state (2026-08-28): v1.0.10 built + staged (pending Mike's test); 1.0.9 superseded, never sent

**1.0.9 was never distributed** and Mike chose to call the next build 1.0.10
anyway ("there already was a 1.0.9"), so the changelog now splits: 1.0.9 =
the 2026-08-21 hardening batch only; 1.0.10 = everything below.

**Session flow (Mike as orchestrator, Sonnet subagents implementing, Fable
reviewing every diff before commit):**
- `8833a57` — the RANDOMIZE-ALL-and-friends focus fix from the 08-25 session,
  **confirmed by Mike** and committed.
- **Paul's round-2 feedback** (macOS, on 1.0.8) drove four fixes:
  - `9f7c6c8` **library folder bug (real, confirmed in source).**
    `chooseLibraryFolder` saved the pick, then `refreshLibrary` →
    `findLibraryRoot` re-validated it with `looksLikeLibrary` (which required
    `root/<pack>/*.wav`) and, on failure, silently reverted the setting to the
    auto-discovered install location or blanked it — no message. A plain
    folder of WAVs never worked. Now: loose WAVs in the root form a pack named
    after the folder; `looksLikeLibrary` agrees exactly with `scanLibrary`; a
    configured root that still exists is never second-guessed (discovery only
    when the path is gone); the picker scans BEFORE saving and shows a
    plain-English dialog if nothing is found; Rescan warns on an empty root.
    This also explains Paul's "previews not playing" (there is NO preview
    feature; presets went silent because samples didn't resolve) and why
    wiping settings with Pearcleaner "fixed" it (settings + `Presets/User/`
    both live in `~/Library/Application Support/Silverplatter Audio/SPASynth/`
    — he lost one user preset; Pearcleaner trashes rather than deletes).
  - `9f7c6c8` **dependent-control dimming** — `DependentEnable` helper in
    `Controls.h` (APVTS listener + AsyncUpdater → `setEnabled`; the rotary
    LnF already painted a disabled state, ComboBox/ToggleButton dimming
    added). Wired: LFO rate↔division on sync, sample loop start/end on LOOP,
    delay time↔division on delay sync, glide time when glide mode is Off.
  - `1997217` **noise burst on preset click** (Paul mistook it for a "C1
    preview note"). Reproduced deterministically: cold loads are silent; a
    load while a released note's tail or the reverb/delay/mod feedback state
    is still non-zero peaks up to ~2.0 (FDN reverb worst) because
    `apvts.replaceState` swaps every coefficient under live state. Fix:
    `restoreStateTree` now does panic()'s hard reset (`synth.allNotesOff(0,
    false)`, `arp.reset()`, `fxChain.reset()`) synchronously inside the
    callback lock it already holds. Reached only from preset load,
    reset-to-default and host session restore; RANDOMIZE ALL doesn't go
    through it. **Design consequence: loading a preset while holding a note
    hard-cuts it** — Mike was told, hasn't objected.
  - `88e3145` **user preset banks** — each immediate subfolder of `User/` is a
    bank (category = folder name, scanned recursively); new
    `PresetInfo::isUser` flag replaces the `category == "User"` inference in
    the USER quick-filter; Save honors the folder chosen in the native dialog
    (New Folder creates a bank) only if inside `User/`, else falls back to the
    root. Favorites keys already include the category. README documents it.
  - Paul's "installer didn't ask to move to trash" = Apple's Installer.app
    (only prompts for a quarantined download in Downloads). Not ours.
- `c8f846c` **CI: Windows exe → draft GitHub Release asset.** The Windows job
  hit the account-wide Actions storage quota ("Artifact storage quota has
  been hit") at the raw-binaries `upload-artifact` step even though the build
  passed; live storage was ~11MB, the meter is cumulative GB-month. Release
  assets don't count. Both `upload-artifact` steps removed from the Windows
  job; it now creates a draft release `ci-windows-<sha7>` (job-scoped
  `contents: write`; workflow default stays read; drafts are invisible to
  the public and create no tag) and prunes older `ci-windows-*` drafts to 5.
  Fetch with `scripts/fetch_windows_build.sh <sha7>` (drafts can't be fetched
  by tag; the script goes through the releases list API). **Verified live on
  run `33213356459`.** The first agent pass removed the wrong upload step —
  the failing one runs BEFORE the installer upload — caught in review.
- Suite 185 → **231 assertions, ALL PASS** (`dependentEnableTest`,
  `looseWavLibraryTest`, `libraryRootPersistsWhenEmptyTest`,
  `factoryPresetRootPackTest`, `presetLoadNoiseBurstTest`,
  `presetBankTest`). auval SUCCEEDED on every step.

**1.0.10 built + staged 2026-08-28, NOT yet tested by Mike, NOT sent:**
macOS pkg signed + notarized + stapled, md5
`176075022554bb1846f9c4d521cbc960`; Windows exe from draft release
`ci-windows-88e3145`, md5 `c190be6903f209920003117bbc24c93e`; both
byte-identical across `dist/installers/` and
`dist/shopify/SPASynth-{Standard,Pro}-1.0.10/`. The 1.0.9 artifacts in
`dist/` are obsolete (the 1.0.9 Windows exe even predates the focus fix).

**Remaining for launch:**
1. Mike installs 1.0.10 (`sudo installer -pkg … -target /`, agent can't
   sudo) and tests in Logic: RANDOMIZE ALL + QWERTY, SET LIBRARY on a plain
   folder of WAVs, greyed-out LFO rate under SYNC, silent preset clicks after
   playing a note, saving into a New Folder bank. Also the 1.0.9 hardening
   (soft bypass) which he never test-drove.
2. Tell Paul: folder-of-WAVs fixed; the "preview" was a bug, fixed; check the
   Trash for his lost preset; banks exist now.
3. Tester round vs. announce; Shopify build-out; send launch email/posts.

## Current state (2026-08-25): v1.0.9 staged; QWERTY focus follow-up (historical — superseded by the 2026-08-28 section above)

**v1.0.8 — two attempts, the first was wrong and Mike caught it.** Fixes
QWERTY (computer-keyboard) note input via the on-screen keyboard silently
dying the instant any knob/dropdown was touched, only resuming after
clicking a virtual key.
- First attempt (`bc7f4d2`) used `setWantsKeyboardFocus(false)` on every
  param control. **Did not work** — Mike tested and reported it back broken.
  That flag only controls whether a component accepts focus if GIVEN it;
  JUCE grabs focus on every mouse click **unconditionally**, via a completely
  separate flag, walking up to a parent if the clicked component doesn't
  want focus — so the first fix just relocated where focus went, not whether
  it moved.
- Corrected (`768309d`), traced through JUCE's actual
  `Component::grabKeyboardFocusInternal` source before landing the real fix:
  `setMouseClickGrabsKeyboardFocus(false)` is the flag that actually stops
  the grab (checked FIRST in `grabKeyboardFocusInternal`, short-circuits
  before any parent-walk). Applied to Controls.h's Knob/Choice/Toggle, mod
  matrix rows, preset browser's category box, top-bar WILD/GLIDE/MASTER, the
  EqEditor's node-drag handling, DraggableTabButton (FX reorder), and
  OscStrip's sample-swap click.
  **Lesson for any future focus-stealing bug in this codebase: it's
  `setMouseClickGrabsKeyboardFocus`, not `setWantsKeyboardFocus`.**
- Since 1.0.8 was never distributed, the corrected build replaced it in
  place (same version, no bump — Mike's "never sent = overwrite" rule).
  Confirmed working by Mike in Logic. **Sent to Paul and Phil**, who also
  tested the Windows build — no issues reported by either.

**v1.0.9 (`c856a4c`..`5ef9d8f`, 2026-08-21) — pre-launch performance/hardening
batch, built+staged, NOT yet tested by Mike.** Done at Mike's request ("wrap
those up pre launch as long as we have the luxury of time"), not in response
to a bug — the deferred list from the 1.0.5 audit. Seven agents ran in
parallel on disjoint files:
- Soft bypass: `processBlockBypassed` no longer hard-zeros output on
  host-triggered bypass (JUCE's instrument default does exactly that) — now
  filters new note-ons and runs the normal pipeline so reverb/delay/convolve
  tails ring out naturally.
- Convolution `NonUniform{256}` partitioning (JUCE's own recommendation for
  the up-to-10s IRs Convolve allows).
- FDN reverb's 4x-`std::sin()`-per-sample tail mod replaced with a seeded
  rotation recurrence — numerically verified identical via before/after peak
  comparison on the existing reverb tests.
- EQ analyzer FFT gated on tab visibility (`isShowing()`).
- `maxModDests=96` capacity guard made always-on (was a debug-only
  `jassert`) — every use site now clamps, in every build configuration.
- `$LIB$` preset path traversal clamp (defense-in-depth; presets can still
  reference arbitrary absolute paths by design).
- PluckString buffers now allocate lazily (~1MB saved when Pluck mode is
  unused), triggered off the osc-mode parameter listener (message-thread
  only, with a timer fallback) — NOT from the audio thread.
- CI `permissions: contents: read` (least-privilege, matters more than usual
  since this repo gets flipped public for Windows CI runs).

Suite grew 181→185 (`bypassTailTest`, `pluckLazyAllocTest`). **Caught two
accidental file-ownership overlaps between parallel agents mid-session**
(`SPASynthProcessor.cpp` and `SPASynthVoice.h` each touched by two agents) —
verified via `grep`/`git status` that no work was lost in either case before
proceeding; both agents' changes coexisted correctly. Marketing copy
refreshed same session: `docs/launch-email.md` and `docs/social-posts.md`
were stuck describing the v1.0.0/v1.0.2 feature set (missing the entire FX
chain, voice modes, oversampling, on-screen keyboard) — rewritten to match
reality. Marketing site (spasynth.com) independently fact-checked via
WebFetch and confirmed accurate by Mike directly (correct sound count,
correct USD pricing, full current feature list — though it does use em
dashes and its footer says v1.0.7, both Mike's call, outside the repo).

Built+signed+notarized+staged 2026-08-21: macOS md5
`2993e1265925293724aca05528ecc343`, Windows md5
`f36f6a7b79cf375dd166d0c3083af7cd`, byte-identical across
`dist/installers/` and both shopify 1.0.9 folders.

**In progress, UNCOMMITTED as of 2026-08-25: a second focus-steal bug, found
after 1.0.9 was staged.** Mike reported QWERTY stops the instant RANDOMIZE
ALL is clicked. Root cause: the 1.0.8 fix only covered *parameter* controls
(Controls.h's Knob/Choice/Toggle etc.) — it never touched the ~25 plain
action buttons across the UI (RANDOMIZE ALL, SAVE, preset nav, settings,
panic, keyboard toggle, accent picker's LINK/RESET, the standalone tempo
bar's TAP/SYNC, Convolve's library/browse buttons, OscStrip's LOAD/INIT,
PresetBrowser's close/favorites/library/rescan), all of which have the exact
same `setMouseClickGrabsKeyboardFocus` defect. Applied the fix to every one
of them in `source/ui/SPASynthEditor.cpp`, `ModulePanels.cpp`, and
`PresetBrowser.cpp`. Build clean, suite still `ALL PASS`, dev build installed
to Mike's plugin folder — **but not yet committed, and not yet confirmed
working by Mike** (he was asked to test RANDOMIZE ALL plus several other
buttons before this gets packaged into a build). If picking this up in a
future session: check `git status` first, this may still be sitting
uncommitted in the working tree.

**GitHub Actions storage alert (2026-08-25, resolved as a non-issue, no code
change).** Mike got a "100% of 0.5GB Actions storage used" email. This quota
is **account-wide across all of Mike's ~21 repos**, not per-repo. Checked
live artifact storage (`gh api repos/{owner}/{repo}/actions/artifacts`) and
Actions cache usage (`.../actions/cache/usage`) across every repo on the
account: totals ~11MB, all in spasynth, already correctly capped at 3-day
retention from the earlier fix (`3d7a109`). The ~50x mismatch vs. the
reported 100%/0.5GB means the billing meter reflects peak/cumulative usage
earlier in the cycle, not current live storage — nothing is actively
accumulating. Resets 2026-09-01. Recommended (not done, Mike declined for
now): a $0 Actions spending limit in GitHub billing settings, since that's a
web-UI action outside CLI/API reach.

**Repo state: currently PUBLIC** (Mike's explicit ongoing choice as of
2026-08-21 — "I'll leave the repo public for now so I won't risk blocking
your progress." Don't prompt him to re-private it unless he asks.)

**Remaining for launch:**
1. **Get the uncommitted RANDOMIZE-ALL-and-friends focus fix confirmed by
   Mike**, then commit + package into a build (1.0.9 if nothing else has
   shipped yet, otherwise bump per the versioning rule).
2. Mike test-drives 1.0.9's actual hardening changes (separate from the
   focus-fix above) — nothing user-facing changed except bypass behavior,
   low risk, but unverified by him.
3. **Windows real-DAW smoke test** — done as of 1.0.8 (Paul and Phil both
   tested Windows with no issues); no longer open.
4. Decide whether to do one more tester round or send the announcement
   directly once Mike is happy with his own testing.
5. Shopify build-out per `docs/shopify-setup-guide.md` (clone the needed
   library zip in from `dist/library/` temporarily, don't leave a permanent
   second copy in a version folder).
6. Send `docs/launch-email.md` / `docs/social-posts.md` (now current) when
   ready — marketing site is already confirmed live and accurate.

## Current state (2026-08-14): v1.0.7 built + staged (pending Mike's real-world validation); v1.0.6 shipped to testers

**v1.0.6 (`97d86e6`, 2026-08-05) WAS SENT to Paul and Phil** (Mike confirmed).
Two fixes responding to tester feedback on 1.0.4/1.0.5:
- `8266360` — FDN reverb wet-path gain normalization. Phil reported MIX was
  oversensitive (10% already too wet) and 100% mix clipped/distorted. Root
  cause: ~+12dB structural over-gain in the wet path — the diffused input was
  injected into all 4 delay lines at unity (proper 1/sqrt(N) injection for
  N=4 is 0.5), and the output tap summed 2 lines per channel at unity (a
  deterministic +6dB on early reflections). Fix: scale both by 0.5. Measured
  wet peaks for a 0.5-amplitude test burst dropped from ~6.4 to ~1.5-1.8
  across all 5 modes; decay/RT60 math and mode character untouched. Factory
  preset `reverbMix` values retuned upward (0.25→0.4, 0.45→0.6) to
  compensate, since they'd been ear-tuned against the old hot path. Test
  bounds tightened.
- `f00b6e9` — RANDOMIZE ALL headphone-safety guards. Mike reported RANDOMIZE
  ALL occasionally produced deafening headphone spikes from combinations of
  individually-reasonable rolls stacking. Two guards, both gated on the
  existing lock groups (oscillators/FX): (a) enabled oscillator levels get a
  uniform dB trim if their combined linear gain sum exceeds a 1.25 budget
  (~one full-scale slot + headroom), preserving the rolled balance; (b) the
  limiter is left enabled at transparent registry defaults after any
  FX-unlocked roll, as a safety net (user can switch it off). New regression
  test iterates 30 rolls asserting both invariants hold.

Both 1.0.6 installers verified: macOS pkg md5
`7c52c6ddd7419c3bca43a6250c90c4ad`, Windows exe md5
`699b920ff608b0dd9e71cb1f653cea56`, byte-identical across
`dist/installers/` and `dist/shopify/SPASynth-{Standard,Pro}-1.0.6/`.

**v1.0.7 (`2f7908d`, `537eede`, `a8d639b`, `264dec9`, empty CI-trigger
`fb6746d`, 2026-08-06) — built, signed, staged; NOT yet sent to anyone.**
Fixes a serious bug Mike hit personally in Logic (not from Paul/Phil): a
saved session, reopened the next day, played intermittent loud noise blasts
covering the patch, recurring even while Logic sat completely idle (no
playback). Copying the channel strip (which restores a fresh plugin instance
from the same saved state) fixed it completely, proving the serialized state
itself was fine — it was the *original* instance's runtime state that was
corrupted at restore time. Mike confirmed: same version both days (1.0.6),
blasts happened during both playback and silence/idle, session had reverb +
delay + limiter all active.
- `2f7908d` — **root cause, an important architectural finding.** Verified
  directly against the JUCE AU wrapper source
  (`libs/JUCE/modules/juce_audio_plugin_client/juce_audio_plugin_client_AU_1.mm`):
  `processBlock`/`Render()` takes `getCallbackLock()`, but `setStateInformation`
  (called during Logic project load, `RestoreState`) takes **no lock at all**.
  `apvts.replaceState()` updates parameters ONE PARAMETER AT A TIME (JUCE's
  own docs: "not realtime-safe, do not call from audio processing code"). So
  during a session reload, `processBlock` could race in and render a block
  against a half-old/half-new parameter set — an unstable coefficient
  combination that injected a burst of energy into the FX chain's feedback
  structures. With delay feedback near-unity, that burst then re-emitted at
  every delay repeat for minutes, decaying only slowly — exactly matching
  "intermittent, decaying, happens even when idle" (delay repeats keep firing
  on their own clock regardless of playback). A fresh instance restoring the
  same state has no concurrent audio thread to race against, so it comes up
  clean — matching Mike's channel-copy fix exactly. Fix: the state-mutating
  core of `restoreStateTree` (`apvts.replaceState`,
  fxOrderPacked/tempo/convIrPath stores) now runs under `getCallbackLock()`,
  mirroring the existing pattern already used for `rebuildOversampling` in
  `timerCallback`. Blocking filesystem I/O (`library::findLibraryRoot()`)
  stays OUTSIDE the lock so audio is never blocked on disk. Deadlock analysis
  done: no APVTS parameter listener in the codebase acquires a lock or calls
  back into the audio thread; the other `restoreStateTree` callers
  (resetToDefault, loadPresetFile) are message-thread-only user actions that
  never already hold the callback lock.
- `537eede` — secondary/amplifying fix: FX modules with recursive internal
  state (ParametricEQ bands, the ModEffect phaser/flanger) were freezing that
  state when disabled (the processing gate just stopped touching it, e.g.
  `if (!active[i]) continue;`) and resuming from the stale/hot frozen state on
  re-enable, which could also ring out a burst. Fixed with edge-triggered
  state clears on the disable→enable transition for ParametricEQ, ModEffect
  (which now tracks its own enable state so it can detect the edge, including
  clearing its delay-line buffer), and TremVib (added defensively, lower risk
  since it has no feedback). New `fxToggleBlastTest` traps hot state in both
  EQ and the flanger, toggles off then on, and asserts silence (measured peak
  = 0 post-fix).
- `a8d639b` — defense-in-depth output safety net, added specifically because
  of how alarming a headphone blast is: (1) `processBlock` now scans the
  final host-domain output buffer for non-finite (NaN/Inf) samples every
  block; if found, silences that block and sets an atomic flag that the
  existing 150ms timer services (under `getCallbackLock`) by calling
  `fxChain.reset()` — a non-finite value in a feedback structure never decays
  on its own, so this flushes rather than lets it recirculate forever. Voices
  deliberately NOT reset by this path (`FXChain::reset()` already covers
  every persistent feedback structure matching the delay-ring-recirculation
  evidence; voices are per-note and re-primed on the next `startNote`, so
  they age out naturally). (2) The very end of `processBlock` hard-clamps
  output to ±4.0 (+12dBFS) via `juce::FloatVectorOperations::clip` — normal
  program material never approaches this, so it's inaudible insurance, but it
  means no future bug of any kind can produce an arbitrarily loud/deafening
  output.
- Suite grew to **181 assertions, ALL PASS**. `auval` SUCCEEDED.

Both 1.0.7 installers verified: macOS pkg signed + notarized + stapled,
`spctl` accepted, `minos 11.0`, md5 `7c57e209cf97a926807309864ef97709`;
Windows exe from CI run `31129966771`, md5 `ea4c063223655774eb928a0e3a77f4d8`;
byte-identical across `dist/installers/` and
`dist/shopify/SPASynth-{Standard,Pro}-1.0.7/`. **1.0.7 status: staged, NOT
sent.** Mike's own validation is still pending — the definitive test is
reopening the *original* affected Logic session (not the copied-channel
workaround) several times on 1.0.7 and confirming no blasts, before deciding
whether to send to Paul/Phil.

**Disk cleanup (2026-08-14, no version bump, docs-only).** Mike found
`dist/shopify/` was consuming ~70GB apparent (93% full disk, 61GB free of
926GB). Root cause: `dist/shopify/SPASynth-{Standard,Pro}-1.0.2/` and
`-1.0.3/` each still had a full `cp`-duplicated copy of the packaged library
(verified byte-identical via md5 to the canonical `dist/library/` copy) left
over from before `build_release.sh` stopped copying the library into version
folders (that stopped starting with 1.0.4 — version folders 1.0.4+ have empty
`Library/` subdirs by design). Deleted the `Library/` contents of the 1.0.2
and 1.0.3 shopify folders (recreated as empty dirs for structure). Actual
disk freed: 35GB (61G → 96G free) — less than the ~70GB apparent-size sum
because APFS had already clone-shared some blocks between the "duplicate"
copies. `dist/library/` remains the one canonical archive (built once by
`package_library.sh`, idempotent/skip-if-exists). Updated
`docs/shopify-setup-guide.md` so future uploads copy the needed zip in from
`dist/library/` temporarily and delete it again afterward, instead of leaving
a permanent second copy in a version folder.

**Repo state: currently PRIVATE** (confirmed 2026-08-14).

**New CI gotcha:** pushing to GitHub while the repo visibility flip hasn't
fully propagated (or possibly Actions being disabled after a visibility
change) can cause a push to NOT spawn a CI run at all, silently — no error,
no run appears. Happened on 1.0.7: the first push (right after the repo went
public) produced no run; a second, later push with an empty
`ci: trigger Windows build` commit triggered it successfully. Diagnostic:
`gh run list --limit 1` shows no new run for the pushed SHA after ~1 minute
→ push an empty commit to retry. This is a wait-and-retry workaround, not a
real fix (PAT lacks admin to inspect/fix Actions settings directly).

**Remaining for launch (Mike's manual steps) — see the 2026-08-25 section
above for the current list; this one is historical.**

## Current state (2026-08-04): v1.0.5 — audit-hardening build, signed + staged, NOT distributed; 1.0.4 is with testers

**v1.0.4 (`6087fef`, 2026-08-03) went out to the partner testers** — Paul and
Phil were sent an install link on 2026-08-03. Six tester-feedback fixes, one
commit each:

1. Library rescan feedback when the root vanishes (`361c474`).
2. Convolve IR chooser greyed-out-WAVs fix + remember-last-folder for both
   file choosers (`a7137ef`).
3. Filter 1 on/off toggle, new `filter1.enable` param defaulting on
   (`b14ff4c`).
4. Reverb Decay range 12s → 8s + Hall multiplier 1.4 → 1.2 (`2ba3713`).
5. Sample/wavetable loader retry on drive-remount (`cdd730a`).
6. Reset to Default settings-menu item via `PresetManager::resetToDefault`
   (`15d9962`).

**Then a full six-agent pre-release audit** (RT-safety, concurrency/lifecycle,
memory-safety, security/packaging, performance, release hygiene) reviewed the
codebase for commercial readiness. Every crash/hang/UAF-class finding was
fixed — that hardening work is **v1.0.5**. Security came back clean: zero
network code (verified "no phone-home"), no committed secrets, clean
installers/CI.

**v1.0.5 (`02bba26`, 2026-08-04, `HEAD`, pushed).** Four commits:
- `b662bbf` — malformed-preset null-deref fix in
  `PresetManager::loadPresetFile`; WAV loader clamps (channel count in
  `WavetableLoader`, int64 length in `SampleLoader`).
- `68dd764` — arp non-finite-ppq guard + zero-sample-block guard in
  `Arpeggiator.cpp`; `convIrLoaded`/`irLengthSeconds` made relaxed atomics;
  `FXChain::tailSeconds` now includes the Convolve IR length + pre-delay.
- `0aefecb` — `WeakReference` guards on all five raw-`this` async sites in
  `SPASynthProcessor` (`loadSampleFromFile`, `loadWavetableFromFile`,
  `restoreStateTree` per-slot loads, conv-IR load); `SafePointer` guards on
  popup menus + the library-missing alert in `SPASynthEditor`; `SlotTable`
  requestSerial latest-wins for wavetables; `scaledMidi` now a persistent
  pre-sized member so `processBlock` never allocates.
- `02bba26` — six regression tests (`malformedPresetTest`,
  `convolveTailLengthTest`, `arpZeroSampleBlockTest`, `arpNonFinitePpqTest`,
  `filter1EnableTest`, `presetResetToDefaultTest`); suite now **176
  assertions ALL PASS**; dead `PresetManager` apvts member removed
  (constructor is now 3-arg); version bump; changelog.

Both 1.0.5 installers verified: macOS pkg signed + notarized + stapled,
`spctl` accepted, `minos 11.0`, md5 `6d98568a7f9d18865c4b588c43a3c3ca`
byte-identical across `dist/installers/` and
`dist/shopify/SPASynth-{Standard,Pro}-1.0.5/`; Windows exe from CI run
`30914554294`, md5 `4b1a1f5d4c55fd3d3e6f97789caa353a`, same three locations.
The 1.0.4/1.0.5 shopify folders have **empty `Library/` subdirs by design**
(installer-iteration folders; library zips get cloned in from the
1.0.3/1.0.2 folders at actual store-upload time). **NOT yet distributed** —
1.0.4 is with testers, 1.0.5 has not gone to anyone yet.

**Versioning rule (Mike's call this session):** bump the version as soon as a
build has been SENT to anyone (testers count) — 1.0.4 went out, so the audit
work became 1.0.5 rather than more 1.0.4 commits.

**Deferred post-launch (found by the audit, deliberately not fixed now —
pick these up in a future session):**
- `juce::dsp::Convolution` should use `NonUniform{256}` partitioning for long
  IRs (currently default uniform — CPU-inefficient for the up-to-10s IRs,
  worse under oversampling).
- Decide/document that the whole FX chain runs at the oversampled rate
  (compounding CPU at 8x).
- FDN reverb does 4 `std::sin()` per sample for tail mod (a recurrence
  oscillator would fix it).
- `EqEditor` computes its FFT every 30Hz tick even when its tab is hidden
  (gate on `isShowing()`).
- No `processBlockBypassed` override (hosts that soft-bypass hard-cut tails).
- `maxModDests=96` capacity check is a debug-only `jassert` (needs an
  always-on guard).
- CI workflow could add `permissions: contents: read` and SHA-pinned actions.
- `$LIB$` `fromPortable` permits `../` traversal (no new trust boundary,
  defense-in-depth only).
- `PluckString` preallocates ~1MB/instance whether used or not.
- Business (not code): CLAUDE.md/handoff.md themselves expose the "Kenzora
  Games" legal-entity name + Team ID during the public-CI flips — Mike's call
  whether to move ops runbook content out of the repo.

**New gotchas from this session:**
- The `SPASYNTH_NOTARY` keychain profile vanished a **second** time
  (2026-08-04). Same recovery as before: Mike recreates it interactively,
  then `xcrun notarytool submit <pkg> --keychain-profile SPASYNTH_NOTARY
  --wait` + `xcrun stapler staple <pkg>` — the signed pkg needs no rebuild.
  The build script dying at notarize also skips the shopify-folder staging
  step; stage manually per the script's section 4 (`mkdir folder/Library`,
  `cp` the pkg + the 3 packaging/docs txt files).
- `build/` was reconfigured without `CMAKE_BUILD_TYPE`, so the tests binary
  now lives at `build/SPASynthTests_artefacts/SPASynthTests` (**no `Debug/`
  subdir**). A stale `Debug/` binary silently ran old tests until caught and
  deleted on 2026-08-04 — see the updated verification-ritual path below.
- Dev AU/VST3 builds copy plugins into `~/Library/Audio/Plug-Ins/`
  (`SPASYNTH_COPY_PLUGIN=ON` by default), and macOS's AudioComponent lookup
  prefers the user domain over the system domain (`/Library`, where the
  signed release installs) — a leftover dev copy silently shadows every
  subsequent signed release in any DAW, however recent or correctly signed.
  Bit us on 1.0.4 and again on 1.0.8 (a QWERTY-fix dev copy from two days
  earlier silently shadowed the whole 1.0.8 release; Mike couldn't get the
  update to show up in Logic at all until it was cleared). **Fixed for good
  going forward**: `scripts/build_release.sh` now unconditionally clears
  `~/Library/Audio/Plug-Ins/{Components/SPASynth.component,VST3/SPASynth.vst3}`
  as its first step, every run — no longer a manual habit to remember.
- Mike's PAT lacks admin: he flips repo visibility himself around Windows CI
  runs (public for the push+build, back to private after). **Repo is PRIVATE
  as of 2026-08-04.**
- Upgrade-install note sent to testers: if a replaced plugin doesn't show up,
  rescan (Logic: Plug-in Manager -> Reset & Rescan Selection) + restart the
  DAW.

**Remaining for launch (Mike's manual steps) — see the 2026-08-14 section above
for the current list; this one is historical.**

## Current state (2026-08-03): v1.0.3 — merged to `main`, built + signed, in smoke testing

**Superseded by the 2026-08-04 (v1.0.5) section above** — kept for history.

v1.0.3 is **merged to `main`** (CMake version 1.0.3; the release merge is
`2559c2f`). All 11 planned features plus the post-merge smoke-test refinements
below are implemented, unit-tested (`SPASynthTests` ALL PASS), and the full
plugin validates (`auval` PASS, `pluginval` strictness-8 SUCCESS incl. param
fuzz). **`HEAD` = `ca5d6c4`** (the arp fix; `1087072` on top is an empty
CI-trigger commit). The signed + notarized macOS pkg and the CI-built Windows
exe in `dist/installers/` and both `dist/shopify/SPASynth-{Standard,Pro}-1.0.3/`
folders were **last rebuilt from the arp fix** (byte-identical across locations;
library zips APFS-cloned from 1.0.2, which is unchanged). Each smoke-test fix so
far has triggered a full signed rebuild (see the loop below). **NOT yet
distributed** — still 1.0.3, keep iterating on it (don't bump) until it goes
out. Repo visibility flips public only for Windows CI builds, then back to
private (see the CI note). The 11 base features, one commit each:

1. Panic button (`b31f2ab`) — stop all sound + clear stuck/latched notes.
2. Standalone tempo (`91274c9`) — internal BPM + tap + external MIDI clock.
3. FX reorder foundation (`f1de48d`) — drag tabs -> chain order (packed atomic).
4. Mod tab (`4d6a9b3`) — Phaser/Flanger, reorderable.
5. Trem/Vib tab (`21b2548`) — independent tremolo + vibrato, reorderable.
6. Limiter/Maximizer (`aadd140`) — reorderable, defaults last, optional lookahead.
7. Convolve (`f4e12a5`) — library SFX / user WAV as impulse (juce::dsp::Convolution).
8. FDN reverb (`748d2c3`) — 4-line FDN, Hall/Plate/Chamber/Room/Spring; ALSO
   fixed a real reorder desync bug (bar moveTab moved only buttons, not the
   TabbedComponent content array -> selecting a tab showed the wrong panel).
9. Parametric EQ — DSP core (`b724b0b`, 8-band hand-rolled RBJ biquads, RT-safe,
   character modes) + interactive Pro-Q editor (`8918ebd`, draggable nodes,
   wheel=Q, double-click add/remove, live FFT analyzer via a Telemetry scope ring).
10. Voice modes (`06070f0`) — Poly/Mono/Duo/Paraphonic/Unison in a rewritten
    `GlideSynthesiser` (note-stack + priority; unison via direct startVoice with
    per-voice detune/pan; paraphonic = shared amp env rendered by the processor,
    per-voice `paraSawGate` latch). Header VOICE call-out. Also fixed a -Wswitch
    gap in the randomizer lock-group map (the four new FX sections).
11. Oversampling (`3e0fa5f`) — whole-synth Off/2x/4x/8x. `processBlock`'s engine
    section factored into `renderEngine()`; runs on the host buffer or an
    oversampled block (juce::dsp::Oversampling IIR polyphase), MIDI scaled to the
    engine domain, tempo/CC layer stays host-domain. Factor swap on the message
    thread under `getCallbackLock()` (purge timer now 150 ms). Settings-menu item.

New invariants worth remembering: **FXChain::Module + numModules(9) + the
default-order array + the fxOrder packed atomic are load-bearing** (order
serialized per preset). **ParametricEQ band choice orders (types) and the
VoiceMode/NotePriority/reverb-mode/EQ-character choice orders are append-only.**
The **Telemetry scope ring** (post-master, 2048 pow2) feeds the EQ analyzer.
Paraphonic gate lags one block by design. Voice-mode + oversampling params live
in `Section::global`; the EQ bands are generated via `id::eqBand(band, key)`.

**Post-merge smoke-test refinements (all on `main`, each rebuilt + re-signed):**
- FX tab grip fix (`465afcd`) — grips were drawn as a fixed left overlay while
  the text was centred; after a drag reorder the bar re-laid-out tight and the
  text slid onto the grips. Reserve grip width in `getTabButtonBestWidth` + the
  text draw for DraggableTabButton (SPASynthLookAndFeel).
- Glide layout (`8f0da9d`) — GLIDE knob now sits left of the mode dropdown.
- EQ interactions (`50bbce1`,`36897de`) — double-click empty to add a band /
  double-click a node to remove (single click just selects); Cmd/Ctrl-drag a
  node vertically for Q (anchored, Pro-Q style) in addition to the wheel; a
  selected-node ring + a freq/gain/Q readout + an on-panel hint and tooltip.
- Limiter scrolling meter (`4f1956e`) — replaced the static curve with a Pro-L-
  style scrolling output waveform + amber gain-reduction from the top + live GR
  readout, fed by a new lock-free `Telemetry` limiter ring (limOut/limGrDb, one
  frame per block; master level w/ 0 GR when off). Bigger display, compact strip.
- Convolve library dropdown (`7c27640`) — a "From library..." button browses
  packs then samples (one folder scanned at a time, like the osc quick-swap).
- Convolve waveform + shaping (`db318f6`) — pre-delay (wet gap), decay (exp IR
  fade), damping (IR HF roll-off) added; the raw IR is kept and RESHAPED (not
  re-read) on the 150 ms timer, reloaded via `loadImpulseResponse(buffer,...)`,
  and re-applied after `prepare` so it survives sr/oversampling changes. New
  `ConvolveDisplay` draws the shaped-IR envelope with the pre-delay gap.
- FX-order randomize + limiter auto-gain (`9356c43`) — RANDOMIZE ALL shuffles
  the chain order (gated by the FX lock group), but the limiter keeps its slot;
  the editor re-applies the tab order on the change broadcast (`refreshAll` ->
  `fxTabs.applyOrder`). Limiter auto-gain toggle = output makeup of `1/drive`
  (transparent peak control; off by default).
- **Arp stuck-notes fix (`ca5d6c4`) — important regression.** The standalone-
  tempo feature (#2) set `ap.hostPlaying = blockPlaying`, and the internal free-
  running clock forces `blockPlaying = true` while `blockPpq` stays frozen at 0,
  so the arp thought it was following a host timeline stuck at beat 0 and re-
  fired the first step every block -> every note stuck. Hit the standalone and
  any host reporting tempo but no ppq. Fix: `ap.hostPlaying = gotHostPpq &&
  blockPlaying` (new `gotHostPpq` flag, true only when the host gives an
  advancing ppq); otherwise the arp free-runs on its own beat clock, as in
  1.0.2. `arpStuckNoteTest` covers it (full-chain, no host, held key -> release
  -> all voices free; plus audible on the internal clock).
- Changelog kept current for all of the above (`docs/CHANGELOG.md`, house style).

**Signing/CI are ready on this machine:** the Developer ID Application +
Installer certs are in the login keychain and the `SPASYNTH_NOTARY` notary
profile works, so the signed+notarized macOS build runs unattended. The rebuild
loop each time a fix lands: `export SPASYNTH_CODESIGN_IDENTITY="Developer ID
Application: Kenzora Games (7K9WY5T49S)"`, `SPASYNTH_INSTALLER_IDENTITY=
"Developer ID Installer: Kenzora Games (7K9WY5T49S)"`, `SPASYNTH_NOTARIZE_PROFILE
="SPASYNTH_NOTARY"`, then `./scripts/build_release.sh -` (skip library repackage
— unchanged); push `main` to trigger Windows CI (Windows-only-on-push, no 10x
macOS); `scripts/fetch_windows_build.sh <sha7>` (superseded `gh run download
<id> -n spasynth-installer-Windows` once the Windows exe moved to a draft
release — see Conventions & gotchas); copy the pkg+exe into the two shopify
folders; verify one-hash byte-identity + `minos 11.0` + `spctl` accepted.
(Mike's PAT expired mid-session once — `gh auth login` fixes it; the PAT
needs `repo` + `workflow` scopes, Actions:read is enough.)

**Remaining for launch (Mike's manual steps) — see the 2026-08-04 section above
for the current list; this one is historical.**

## Current state (2026-07-20): v1.0.2 — first build to the testing team

v1.0.2 is the build Mike is sending to the partner testers (Paul, Phil) — the
first time it leaves his machine. Signed + notarized (macOS), freshly CI-built
(Windows). One clean set in `dist/installers/` and the
`dist/shopify/SPASynth-{Standard,Pro,Upgrade}-1.0.2/` folders (byte-identical
across locations). Team changelog: `docs/CHANGELOG.md`. Version stays **1.0.2**
(never distributed before); bump to 1.0.3+ for any change after this goes out.

**1.0.2 changes (from the partner testing round):**
- **CRITICAL — macOS deployment target (load-bearing).** Builds had no
  `CMAKE_OSX_DEPLOYMENT_TARGET`, so binaries inherited the build machine's OS
  (macOS 26, `minos 26`); dyld refused to launch the standalone on anything
  older (plugins still loaded — hosts dlopen them without an
  LSMinimumSystemVersion check). Pinned to **11.0** before `project()` with
  FORCE (stale caches held an empty value). Never remove this. Verify:
  `otool -l <bin> | grep -A2 LC_BUILD_VERSION` → `minos 11.0`.
- **Arp swing fix.** `firstStep` was computed from the un-swung beat, so a swung
  (odd) step whose delay crossed an audio-buffer boundary was dropped (every odd
  step of a 1/16 arp). Start the scan one step early, guarded against
  double-fire (`Arpeggiator.cpp`) + `reverbMixTest`-style regression in the arp
  test.
- **Settings menu.** Top-left logo (`SettingsButton` overlay over the painted
  logo) opens a PopupMenu: Set Library Folder, Rescan, Accent Colors, Show
  Keyboard, Clear All MIDI Learn. Works in plugin AND standalone. Answers Phil's
  "no settings menu in Live" — the standalone's JUCE "Options" button is
  audio-DEVICE settings, which cannot exist in a plugin (the host owns the audio
  device + MIDI routing), so this is the host-correct equivalent.
- **On-screen keyboard.** `juce::MidiKeyboardComponent` bottom strip, toggled
  from the settings menu or the bottom-right `KeyboardButton` (piano icon, lit
  in the accent colour when shown, kept clear of the window resize grip). Mouse
  + computer-QWERTY playing (JUCE maps the keys by default). Processor owns a
  `juce::MidiKeyboardState`; `processBlock` merges its notes via
  `keyboardState.processNextMidiBuffer(...)` (brief lock — the standard JUCE
  on-screen-keyboard idiom, accepted here). Persists in the APVTS property
  `uiKeyboardVisible`. The strip adds `metrics::keyboardStripHeight` to the
  content base height so the module grid is unchanged; the shell re-fixes the
  window aspect ratio on toggle (`getContentBaseHeight`, `keyboardToggled`,
  `configureConstrainer` in `SPASynthEditor.cpp`).
- **Reverb MIX fix.** Was `wetLevel=mix, dryLevel=1-0.4*mix` — the dry never
  dropped below ~60% (never full wet), and juce::Reverb scales dryLevel by 2x
  internally so mix=0 was ~+6 dB, not transparent. Now an equal-power crossfade:
  `wetLevel=sin(theta), dryLevel=0.5*cos(theta)`, theta = `mix*halfPi` → unity
  dry at 0, pure reverb at 1. This changes how reverb-heavy factory presets
  sound (quieter, more balanced). `FXChain::processReverb` + `reverbMixTest`.
- UI polish: master meter padded off the right edge; keyboard toggle button.

**CI change (`.github/workflows/build.yml`).** Repo went private mid-session;
private repos meter Actions minutes and **macOS runners bill at 10x**, so one
~2h universal run drained the monthly quota and every push then failed instantly
(4s, no steps). CI's macOS artifact was unsigned/unused (we build+sign+notarize
macOS locally), so: **Windows builds on every push** (only platform we cannot
build locally); **macOS is `workflow_dispatch` only**. Until the quota resets or
a spending limit is set, a private repo cannot build Windows — the session
workaround was to make the repo **temporarily public**, push (Windows-only on
push = no 10x macOS), `gh run download ... -n spasynth-installer-Windows`
(superseded — see Conventions & gotchas for the current
`scripts/fetch_windows_build.sh` draft-release flow), then re-private. PAT
has Actions:read (Mike enabled it) but not Actions:write (cannot
`gh run rerun`; trigger with an empty commit push instead).

**Signing/notary gotcha.** The `SPASYNTH_NOTARY` keychain profile vanished
mid-session (notarize failed "No Keychain password item found for profile") with
the keychain unlocked. Mike recreated it: `xcrun notarytool store-credentials
SPASYNTH_NOTARY --apple-id <id> --team-id 7K9WY5T49S` (prompts for the
app-specific password, kept local). If notarize fails this way the signed pkg
does NOT need rebuilding — just `xcrun notarytool submit <pkg>
--keychain-profile SPASYNTH_NOTARY --wait` then `xcrun stapler staple <pkg>`.

**Open (not launch blockers):**
- **External-monitor drag (Paul).** Standalone window will not drag from a
  Retina laptop screen onto an external monitor (mixed-DPI). Root cause:
  fixed-aspect window + the points-per-pixel change at the display seam; JUCE
  re-evaluates size against the aspect ratio and snaps it back. Not reproducible
  on Mike's matched-DPI setup; risky to fix blind (could break resizing for
  everyone); works fine as a plugin in a DAW. Documented as a known limitation
  in `docs/CHANGELOG.md`; workarounds: make the external the main display, or use
  the plugin. Revisit post-launch on a real mixed-DPI rig if customers hit it.
- **RX 9 "Failed to load" (Phil).** Not a bug: RX 9 is an effects host and
  cannot host an instrument (SPASynth is a synth; Zebra2 shows the same in RX).
  The VST3 passes pluginval strictness 8 and loads in Logic/Live. Test in an
  instrument host.

## Where the project stands (2026-07-11)

**v1.0.0 — feature-complete, packaged, in macOS smoke testing.** All 10 brief
checkpoints are done, plus post-brief features: dual filters
(series/parallel), UAD-style preset browser drawer, glide (Off/Always/Legato),
arp probability controls (chance/stutter/jump/humanize), user-tintable accent
colors (light mode removed), license.txt ownership stamp in the footer,
2-decimal value readouts, and the full packaging pipeline.

**Smoke testing found + fixed four shipping bugs (all committed/pushed):**
1. `33abff0` — installer only laid down the Standalone; AU/VST3 silently
   didn't install. Cause: all three component pkgs shared one identifier
   (`com.silverplatteraudio.spasynth`, derived from the common
   CFBundleIdentifier). Fix: explicit unique `--identifier` + `--version` per
   `pkgbuild --component` (`installers/macos/build_installer.sh`).
2. `33abff0` — standalone had no app icon. Fix: `assets/branding/app_icon.svg`
   (+ `make_app_icon.sh` → `app_icon.png`) wired as `ICON_BIG` in
   `juce_add_plugin`; JUCE emits the `.icns`/`.ico`.
3. `9b15cd2` — standalone hard-crashed (SIGABRT via macOS TCC) on the
   Bluetooth MIDI menu. Cause: no `NSBluetoothAlwaysUsageDescription` in
   Info.plist. Fix: `BLUETOOTH_PERMISSION_ENABLED/_TEXT` +
   `MICROPHONE_PERMISSION_ENABLED/_TEXT` in `juce_add_plugin` (mic added
   pre-emptively — audio input would TCC-crash the same way).
4. The AU *installed then vanished* on upgrade installs (receipt written,
   bundle gone from `/Library/...`; Logic never saw it). Root cause: all
   three bundles shared one CFBundleIdentifier, and PackageKit keys its
   payload "atomic shove" bookkeeping on the bundle id — three same-id
   payloads in one install collide and the AU gets trashed right after
   landing (`/var/log/install.log` showed "Parent bundle … will be
   atomically shoved" ×3 with one id; the fixed pkg logs three distinct
   ids). Fix: per-format CFBundleIdentifier patched into the JUCE-generated
   plists at configure time (CMakeLists.txt — AU = `…spasynth.au`, VST3 =
   `…spasynth.vst3`, standalone keeps the base id since it owns the TCC
   grants), plus `BundleIsRelocatable=false` component plists in
   `build_installer.sh`. Same scheme Arturia/Soundtoys/Softube ship. Host
   compat unaffected (AU identity = aumu/SpSy/SpAu; VST3 = class UUID).

**macOS smoke test now PASSES end-to-end**: the reinstalled pkg laid down
all three formats and they stayed put; icon + Bluetooth prompt OK; auval +
pluginval pass on the installed copies; SPASynth loads and plays in Logic.
The pkg is still UNSIGNED, so first launch needs right-click → Open.

Follow-up polish from the same smoke-test day (all committed/pushed; Logic
behaviour confirmed by Mike where noted):
- `2251488` — library discovery falls back to any WAV-holding folder inside
  a "Silverplatter Audio" dir when no folder named "SPASynth Library"
  exists. (Mike's "samples not loading" was the dev symlink still pointing
  at ~/arsenal after the repo folder rename — repointed.)
- `91bd70d` — macOS Tahoe + Logic's AUHostingService opens the editor with
  a stale hit-test region (top strip dead until a knob moves; Apple bug,
  hits non-JUCE plugins too, REAPER/standalone immune). Workaround: 1px
  resize nudge + repaint after the editor first shows (AU/macOS only,
  `parentHierarchyChanged`). Confirmed fixed in Logic.
- `f1d18c6` — loading state while slot content loads: sweep bar + dimmed
  stale waveform + "loading..." header label; per-slot atomic pendingLoads
  counters on the processor; third snapshot `spasynth-loading.png`.
- `f7aee02` — both accents default to Silverplatter teal #51D0BF (the old
  orange/cyan pair read too close to MiniFreak); LINK defaults on and the
  picker's RESET re-links.
- `ff537aa` — brand wordmark centred on true glyph ink via path bounds
  (GlyphArrangement's box is advance-based; tracked text sat 5.5px left).
- `826cf82` — coarse tune excluded from RANDOMIZE ALL (semitone jumps break
  the song key; fine detune still rolls; same pattern as rootNote).

Dev-machine notes from the bug-4 session:
- Both build trees had stale CMake caches from the `~/arsenal` →
  `~/spasynth` folder rename; both were reconfigured from scratch.
- installd (as root) had earlier "relocated" an app payload INTO the build
  tree — a root-owned `build-release/SPASynth_artefacts/Release/Standalone/
  SPASynth.app.root-junk` remains; Mike can `sudo rm -rf` it whenever.
- Logic caches per-version validation verdicts: after replacing a
  same-version AU, use Plug-in Manager → Reset & Rescan Selection, then
  RESTART Logic (the plugin menu is built at launch).
- Dev builds copy Debug plugins into ~/Library, which shadow the installed
  /Library release copies in Logic — clear them (`rm -rf ~/Library/Audio/
  Plug-Ins/{Components/SPASynth.component,VST3/SPASynth.vst3}`) whenever
  Mike is smoke-testing the installed release.
- GitHub repo renamed to `meeglosh/spasynth` (remote updated).

**macOS signing/notarization: DONE (2026-07-17).** Kenzora Games Developer
ID (team `7K9WY5T49S`); certs imported to login keychain (had to use
`security import` via CLI — double-click threw -25294 on this macOS), keys
authorized for the signing tools with `security set-key-partition-list`
(else codesign stalls on a GUI prompt), notary creds stored as keychain
profile `SPASYNTH_NOTARY` (app-specific password). The shipping pkg
`dist/installers/SPASynth-1.0.0-macOS.pkg` is signed + notarized + stapled
(spctl: "Notarized Developer ID / accepted") and copied into both
`dist/shopify/` folders. To re-sign a future build:
`export SPASYNTH_CODESIGN_IDENTITY="Developer ID Application: Kenzora Games (7K9WY5T49S)"`,
`SPASYNTH_INSTALLER_IDENTITY="Developer ID Installer: Kenzora Games (7K9WY5T49S)"`,
`SPASYNTH_NOTARIZE_PROFILE="SPASYNTH_NOTARY"`, then `./scripts/build_release.sh`.

**Windows installer: BUILT + STAGED (2026-07-17).** CI had a latent bug —
the Inno `/O` output path used `..\..\dist\installers` (correct base for
Source paths, which are .iss-relative, but `/O` is CWD-relative), so the
`.exe` landed two levels above the workspace and the artifact upload found
nothing on every prior run. Fixed in `e719088` (absolute
`%GITHUB_WORKSPACE%` path). `SPASynth-1.0.0-Windows.exe` (unsigned by
decision) is now in `dist/installers/` and both `dist/shopify/` folders.
Both SKU folders are complete: signed pkg + exe + library zips + 3 docs
(Standard 3.0 GB, Pro 32 GB; every Pro part < 5 GB Shopify cap).

**Post-prep UI polish (all committed, verified in Logic, in the shipping
build `9dcae8e`):** flat modern knobs (thin ring + accent arc + position
dot, replacing the skeuomorphic disc; `664e2be`); padlock glyph on locked
section buttons; animated granular playback — the waveform shows the live
grain cloud (per-slot `Telemetry::GrainViz`, published each mod chunk;
`7524a4c`); in-pack sample quick-swap — click the osc sample name for a
dropdown of the whole pack (`getPackSiblings` + latest-wins load serial;
`bd149b0`); solid triangle preset-nav carets (`5b7f223`); and the WILD knob
ring heats accent → red with amount (HSV lerp; `9dcae8e`). The signed +
notarized macOS pkg and the fresh Windows exe in both `dist/shopify/`
folders include all of it (rebuilt 2026-07-17).

**Product listings written** to `docs/shopify-listings.md` (descriptions,
SEO fields, card blurbs, FAQ, pricing, per-SKU attachment lists; no em
dashes). Sound count corrected to 11,474 everywhere (verified WAV count;
spasynth.com still says 11,401 and needs updating outside the repo).

**Official pricing (USD):** Standard $99 intro / $149 reg; Pro $499 sale /
$899 reg (the $899 = the Everything Bundle price); Upgrade = the difference,
$400 intro ($499−$99) / $750 reg ($899−$149). Set Shopify Price = intro,
Compare-at = reg. Intro-period length TBD. (Earlier $702/$1264 figures were
CAD by mistake; corrected to USD 2026-07-18 across all docs.)

**Everything that can be built/staged is DONE.** Signed+notarized macOS pkg
and fresh Windows exe (both from `9dcae8e`) are in `dist/installers/` and,
with the library zips + docs, in both `dist/shopify/SPASynth-{Standard,Pro}
-1.0.0/` folders — verified byte-identical across locations. The full launch
copy kit is written and committed: `docs/shopify-listings.md` (descriptions,
SEO, blurbs, FAQ, pricing, per-SKU attachment lists), `docs/launch-email.md`
(HTML + plain-text), `docs/social-posts.md` (X + Instagram),
`docs/marketing-brief.md` (landing-page source), `docs/spasynth-marketing.png`
(retina hero). All customer copy: "we"/Silverplatter Audio voice, no
individual names, no em dashes, 11,474 sound count.

**Pro library delivery = Cloudflare R2 (done 2026-07-18).** The 37 GB Pro
library exceeds Shopify's per-product cap, so the 11 parts live on R2 (bucket
`spasynth`, account `25de31a7…`) with clean names
`SPASynth-Pro-Library-Part-01..11.zip` under `pro-library/`, public via the
custom domain **downloads.spasynth.com** (spasynth.com DNS moved GoDaddy →
Cloudflare; site still on GitHub Pages, grey-cloud A records; no email on the
domain). Verified: `curl` HEAD 200 + correct size + valid ZIP. Delivered to
buyers via a small links file `dist/shopify/SPASynth Pro Library - Download
Links.txt`. Uploads done with `rclone` (remote `r2`, `no_check_bucket=true`;
the R2 API token is bucket-scoped so ListBuckets/CreateBucket 403 is normal —
use bucket-direct ops). R2 has no egress fees, so downloads are ~free.

**Remaining for launch (Mike's manual steps, nothing to code):**
1. **Windows real-DAW smoke test** — the one untested surface. Load the VST3
   in Reaper/Live/Cubase, confirm the library auto-discovers and a preset
   plays. Windows unsigned → "More info → Run anyway" past SmartScreen.
2. **Shopify build-out** — full plain-English click-by-click walkthrough is
   saved to `docs/shopify-setup-guide.md` (Mike is non-technical on the ops
   side; hand-hold). Standard uploads its 3 GB starter library directly; Pro
   and Upgrade deliver the big library via the R2 links file (do NOT upload the
   32 GB to Shopify). Attach lists per SKU are in `docs/shopify-listings.md`
   and the guide. Set Price=intro, Compare-at=regular; uncheck "physical
   product"; test-purchase; activate.
3. **Free "Everything Bundle" product (Part 6 of the guide)** — decision of
   record: bundle owners get SPASynth free. Make an installer-only product
   (pkg+exe+docs, no library), price 0, kept off the public storefront, shared
   via direct link or a 100%-off code. TODO: pick the mechanism + notify list.
4. **Marketing site update** — spasynth.com still says 11,401 and predates
   the "we"/company voice + the quick-swap feature. Use the prompt drafted
   in-session (Claude Code in the site repo): fix count to 11,474, first-
   person company voice (no names), boutique-SFX-company positioning + the
   mission statement, add in-pack quick-swap to feature lists, verify pricing.
5. **Announce** — send `docs/launch-email.md`, post `docs/social-posts.md`.
6. **Add-on pack products (later)** — one per pack from `dist/library/packs/`.

**Open verification (before publishing the existing-library FAQ):** confirm
the shipping SFX-library downloads are laid out as pack-folder-per-library
containing WAVs (so "just point SPASynth at your existing folders via SET
LIBRARY" holds). SPASynth reads ONE library root and treats its immediate
subfolders as packs; factory presets regenerate from whatever files are found
(portable `$LIB$/pack/file`), and the loader resamples, so customers' 24/96
originals work as-is with no re-download or conversion. Covered by the three
new FAQ entries in `docs/shopify-listings.md`.

Business decisions of record: two SKUs differentiated by **content only**
(one binary, no gating). Standard = full synth + 440-sound starter library;
Pro = all 88 packs / 11,474 WAVs / 37 GB at 24/48 (24/96 originals archived
by Mike). **No DRM ever** — no serials, no activation (Mike re-confirmed
after considering a serial system; the license.txt footer stamp is the
agreed alternative). Upgrade path is handled entirely in Shopify.
**Everything Bundle owners get SPASynth free** (part of their lifetime
updates; Mike's call) — deliver the installer only, they point at their
existing bundle via SET LIBRARY. **Existing SFX-library customers reuse
their own folders** (no separate SPASynth folder, no re-download). Company
identity: **Silverplatter Audio is primarily a boutique sound-effects
library company**; SPASynth is our synth product. All customer-facing copy
is first-person company voice ("we"/"our"), never names individuals, and
uses no em dashes; sound count is 11,474.

## The verification ritual (do this for every change)

1. `cmake --build build --target SPASynthTests` then run
   `build/SPASynthTests_artefacts/SPASynthTests` → expect `ALL PASS` (176+
   assertions). Note: this dir has no `Debug/` subdir since `build/` was
   reconfigured without `CMAKE_BUILD_TYPE` (2026-08-04) — if you see a
   `Debug/` copy, it's stale, delete it. Fix every new compiler warning — one
   caught a real Filter-2 lock bug.
2. UI changes: render snapshots and **actually look at them**:
   `SPASynthTests --snapshot <dir>` writes `spasynth-dark.png` (preset
   browser open, FILTER 2 + DELAY fronted) and `spasynth-accent.png`
   (violet/lime re-tint). Note: renders pick up the *user's saved accent
   colors* from machine settings; the committed `docs/*.png` are the visual
   regression record — refresh them from a defaults run.
3. `cmake --build build --target SPASynth_AU SPASynth_VST3` then
   `auval -v aumu SpSy SpAu` and
   `pluginval --strictness-level 8 --validate ~/Library/Audio/Plug-Ins/VST3/SPASynth.vst3`.
4. Commit + push at every completed feature (Mike expects this cadence).

Dev build dir is `build/` (Ninja, Debug, plugins copied to user plugin
folders). Release: `./scripts/build_release.sh` (uses `build-release/`,
universal, runs tests, builds pkg, packages library, assembles
`dist/shopify/`; pass `-` to skip the slow library packaging). Ignore stale
clangd/IDE diagnostics ("juce not found" etc.) — the build is the arbiter.

## Load-bearing invariants (break these = break users' sessions)

- **`source/params/ParameterRegistry.*` is the single source of truth** for
  every parameter (range/default/section/mod-dest flag/RandomSpec/choices).
  Add params there, never ad hoc. New params are keyed by ID so placement is
  free, EXCEPT:
  - **ModSource enum is append-only** (serialized in matrix route choices).
  - **Mod-destination order is append-only**: dense dest indices are
    serialized; new destination params must be appended AFTER all existing
    dests in registry order (capacity `maxModDests = 96`).
  - Choice-parameter orders (filter types, osc modes, arp modes, LFO shapes,
    divisions, glide modes) are load-bearing and append-only.
- **Real-time safety**: no allocation/locks/IO on the audio thread.
  Content loads on background threads → atomic live pointers → timer-deferred
  retirement (processor). Fixed-capacity everything in voices/arp
  (`Arpeggiator` pending-ratchet queue, preallocated scratch MidiBuffer).
- **Per-voice modulation at 64-sample chunks** in normalized space; chaos and
  granular followers use one-chunk-latency feedback. `SharedState` is written
  once per block by the processor; voices only read (glide origin/key-count
  is the exception — written by `GlideSynthesiser` note hooks in event order).
- **UI reads all colors/fonts/metrics from `source/ui/Theme.h` tokens at
  paint time.** No light theme; accents are user preferences (see picker).
  Value formatting is set at parameter construction (adaptive ≤2 decimals).
- **Machine settings** (library root, favorites, accent colors + link,
  MIDI-learn map is session-state not settings) go through the single
  `PropertiesFile` singleton in `source/library/Library.cpp` — never create
  another PropertiesFile (that pattern caused the theme-reset bug).
- **Portable paths**: sample/wavetable refs inside presets/sessions are
  `$LIB$/...` relative to the library root.
- Presets: `.spasynth` XML; factory presets are generated per pack
  (Keys/Texture/Pulse from smallest/middle/largest WAV — the starter library
  intentionally includes exactly those files so Standard presets all load).
- MIDI-Learn maps ride host sessions but are **excluded from presets**.

## Map

- `source/SPASynthProcessor.*` — APVTS, raw-pointer caches (`Raw` struct),
  `updateSharedState`, `buildStateTree/restoreStateTree`, randomizeAll,
  library refresh, content storage.
- `source/dsp/` — `SPASynthVoice` (7 engines/slot, dual filters, glide,
  chaos), `Arpeggiator` (modes + probability), `FXChain`, `Telemetry`
  (lock-free audio→UI), loaders.
- `source/params/` — registry + `Randomizer` (lock groups, wildness).
- `source/library/` — settings singleton, auto-discovery
  (`/Users/Shared/Silverplatter Audio/SPASynth Library` etc.), PresetManager,
  license stamp (`getLicenseLine`).
- `source/ui/` — `Theme.h` tokens, LookAndFeel, `SPASynthEditor`
  (ContentComponent + fixed-aspect scaling shell + accent picker),
  `PresetBrowser` drawer, `ModulePanels`, `Displays` (telemetry scopes),
  `Controls.h` (Knob/Choice/Toggle with paramID props for MIDI Learn).
- `tests/SPASynthTests.cpp` — the whole suite + `--snapshot` renderer.
- `scripts/` — `build_library.sh` (zips→24/48 packs), `package_library.sh`
  (pack zips + starter + Pro volumes; APFS-clone staging, idempotent),
  `build_release.sh` (one-shot release + Shopify folders).
- `installers/` — macOS pkg builder (unique per-component ids + signing
  hooks), Windows Inno `.iss`.
- `packaging/` — customer docs (README/QUICKSTART/EULA),
  `license-template.txt` (per-order ownership stamp).
- `assets/branding/` — logo SVGs + `app_icon.svg`/`app_icon.png` (standalone
  icon, wired via `ICON_BIG`); regen the PNG with `make_app_icon.sh`.
- Local-only (gitignored): `library/` (88 built packs, symlinked to
  `/Users/Shared/...`), `Silverplatter Audio packs/` (raw zips),
  `dist/`, `build*/`. A starter-library copy for Standard-experience testing
  lives at `/Users/Shared/Silverplatter Audio/SPASynth Starter Library`.

## Conventions & gotchas

- Comment style: explain constraints/why, sparingly; match existing density.
- CI (`.github/workflows/build.yml`): macOS universal + Windows x64. macOS
  job is slow (~1 h JUCE build) and still uploads via `actions/upload-artifact`.
  Windows's installer `.exe` no longer does — `actions/upload-artifact` was
  hitting the account-wide Actions storage quota (0.5 GB, cumulative
  GB-month; release assets don't count against it), so the Windows job now
  publishes the exe as an asset on a DRAFT release tagged
  `ci-windows-<short sha>` (job-level `permissions: contents: write`,
  workflow default stays `contents: read`; drafts are invisible to the
  public even on this public repo and don't create a real tag until
  published), pruning older `ci-windows-*` drafts down to the newest 5 each
  run. Fetch a build locally with `scripts/fetch_windows_build.sh
  [<sha-or-latest>] [<outdir>]` (draft releases aren't resolvable via
  `gh release download <tag>`, only via the releases list API, which the
  script handles).
- GitHub remote: `https://github.com/meeglosh/SPASynth.git`. Mike's PAT has
  repo+workflow scopes but not admin.
- Snapshot tests front tabs/drawer via `dynamic_cast` walks; keep component
  types discoverable if refactoring.
- `juce_add_binary_data` asset list: the white square logo filename really is
  `SPAudio_logo_sqaure_white.svg` (upstream typo, kept).
- Old "Arsenal"-named DAW sessions predate the rename and won't reconnect.
- Release rebuilds: never hand-`rm` subdirs inside
  `build-release/SPASynth_artefacts/` — Ninja relinks the binary but skips the
  bundle-assembly steps (Info.plist, VST3 manifest), leaving half-built
  bundles that `pkgbuild` rejects. To force a clean rebuild, delete the whole
  `SPASynth_artefacts/` dir (the compiled objects live in
  `build-release/CMakeFiles/`, so this re-archives + relinks + reassembles in
  ~40s with no source recompile).
- Installing the pkg needs admin (`sudo installer -pkg … -target /`); the
  agent shell can't sudo, so the actual install is always Mike's step.
