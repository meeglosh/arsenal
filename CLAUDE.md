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

## Current state (2026-09-07): v1.0.14 (main `81236ad`) built + staged; arp count-in crash, reverb OOB read (the noise bursts), VOICE-panel lifetime

**1.0.13 was confirmed by Mike (close-window crash gone, session clean)
but never sent.** Then he hit a NEW crash: recording a second MIDI track
in Logic segfaulted every time. Three identical crash reports
(`~/Library/Logs/DiagnosticReports/AUHostingServiceXPC_*.ips`, JSON after
the first line): render thread, `processBlock` +7956, a `ldrb` of
`held[garbage].note` inside the inlined `Arpeggiator::triggerStep`.
Symbolicated by `lipo -thin arm64` on the `build-release` AU binary +
`objdump -d --start-address` (release has no line tables).

- `2585783` **arp crash on negative host ppq.** Logic reports a NEGATIVE
  ppqPosition before bar 1 (record count-in / pre-roll). On transport sync
  the arp sets `stepCounter` from it, and C++ `%` keeps the sign, so
  `stepCounter % length` indexed `sorted[]`/`byArrival[]` with a negative
  subscript: a stack read past the array → garbage held-note index →
  SIGSEGV. Latent since the arp landed (`573ef6d`); needs an arp preset
  playing during a count-in. Fix: `wrapStep()` (non-negative modulo) at
  every stepCounter `%` site. `arpNegativePpqTest` runs all 12 modes from
  ppq -8 → +2, octaves 2, accent velocity, asserts only held pitches play.
- `81236ad` **three more, found with AddressSanitizer** while chasing an
  intermittent test crash (see below). (a) **`FDNReverb::process` read one
  float PAST a delay line**: the modulated read position is a float; a
  value a hair below zero is wrapped by `+= size` and float precision at
  ~5768 rounds it to exactly `(float) size`, so `i0 == size`, `fr == 0`,
  and whatever the allocator placed after the vector was injected raw into
  the feedback network. Sporadic and purely heap-layout (= build)
  dependent, stops when the reverb is off — this is almost certainly the
  loud pulsing bursts that got the `audit-hardening` 1.0.13 build
  abandoned on 2026-09-05 (1.0.12 was clean by luck). Same guard
  (`while (i0 >= sz) i0 -= sz` after truncation) applied to the delay
  (`FXChain.cpp`), `ModEffect` and `TremVib`, which used the same pattern.
  (b) **VOICE call-out panel could outlive the processor**: the call-out
  is owned by JUCE's `CallOutBoxCallback`, deleted by the
  ModalComponentManager on a LATER message-loop turn; a host closing a
  project deletes editor then processor with no pump between, so the
  orphaned panel's Knob/Choice attachments unregistered from a freed APVTS
  (heap-use-after-free). `ContentComponent::openVoicePanel` (SafePointer)
  + synchronous `VoicePanel::detach()` (new `Knob/Choice::detach()`) in
  `~ContentComponent`, which also exits the box's modal state; deletion
  stays with the modal manager. `voicePanelEditorCloseTest` variant 3
  (window closed, processor destroyed immediately, then pump).
  (c) **`voicePanelEditorCloseTest` itself was flaky (~1 run in 3, SIGSEGV
  through a null vtable) — a TEST bug**: it held a raw `CallOutBox*` across
  message pumps, but `CallOutBoxCallback`'s 200ms timer dismisses any
  call-out while the process is not in the foreground (a CLI test run
  never is) and the modal manager frees it. Now a SafePointer. **This
  flake aborts `build_release.sh`** (it runs the suite under `set -e`) —
  it killed the first 1.0.14 build attempt.

Suite **437 assertions ALL PASS, 3/3 normal runs + 5/5 under ASan**.
**macOS 1.0.14 pkg from `81236ad`**: signed + notarized + stapled, `spctl`
accepted, minos 11.0, universal, md5 `68edd892e562370c765c005627dfb376`,
byte-identical across `dist/installers/` and
`dist/shopify/SPASynth-{Standard,Pro}-1.0.14/`. Windows exe from draft
release `ci-windows-81236ad` (CI run `34157223276`; repo flipped public by
Mike for it), md5 `89003ae52f0c2905eba27f696de34df4`, same three
locations. Changelog `## 1.0.14` covers all three fixes in customer voice.
The earlier 1.0.14 pkg (`b52afaf4…`, arp fix only) was overwritten.

**ASan is now part of the ritual** (step 1b below). The `hardening-safe`
branch (`d7f38c3`) still sits on 1.0.13's main; rebase it onto 1.0.14 and
have Mike verify in Logic before shipping any of it.

**Pending: Mike installs 1.0.14 and tests (1) recording a second track
with an arp preset + count-in, (2) his 1.0.11 session with reverb on for a
while, (3) VOICE open → close window, and close project; then the rest of
the gauntlet; then send to Paul and Phil.**

## Current state (2026-09-06): v1.0.13 (main) built + staged; fixes the Logic close-window crash

**1.0.12 was never sent.** Mike found a crash on it in Logic: open the VOICE
call-out, switch modes, close the plugin window → SIGABRT in JuceAU
deleteEditor ("pointer being freed was not allocated"). Root cause
(`e5fc7fa`): the 1.0.12 VOICE fix parented the call-out (and the accent
picker) to `getTopLevelComponent()`, which under the AU wrapper is JUCE's
`EditorCompHolder`, whose destructor `deleteAllChildren()`s — so a still-
open CallOutBox (owned BY VALUE by JUCE's CallOutBoxCallback) got
`delete`d. Standalone was immune (its top level is a window that doesn't
delete children). Fix: new `ContentComponent::callOutParent()` returns our
own editor shell (`findParentComponentOfClass<juce::AudioProcessorEditor>`),
and both dismissal lambdas are SafePointer-guarded (they fire from the
modal manager's deferred delete, possibly after the editor is gone).
`voicePanelEditorCloseTest` hosts the editor in a holder that mimics
EditorCompHolder; it aborted (exit 134) before the fix. Suite ALL PASS.
**Rule: never parent pop-overs to getTopLevelComponent(); use
callOutParent().**

**1.0.13 = main at `dee6146`** (fix + bump). Built 2026-09-05 evening:
macOS pkg signed + notarized + stapled, md5
`51feed96455d8b4b7bc2943ae3093e7f`; Windows exe from draft release
`ci-windows-dee6146`, md5 `b934ab4fdc993a27760977a8731a4c45`; both
byte-identical across `dist/installers/` and
`dist/shopify/SPASynth-{Standard,Pro}-1.0.13/`. (The old 1.0.13 files from
the abandoned audit branch were deleted first.) Changelog `## 1.0.13`
covers both fixes. **Pending: Mike installs 1.0.13, confirms the crash is
gone (close the window with VOICE open, repeatedly) and his session still
plays clean, then finishes the gauntlet and sends to Paul and Phil.**

## Current state (2026-09-05): v1.0.12 INSTALLED and CONFIRMED WORKING by Mike; audit-hardening branch ABANDONED

**Decision of record (Mike, 2026-09-05): the `audit-hardening` branch
(1.0.13, commits e1c131e/eb59215/4d4fd95/6af7243/37fa0e6) is abandoned.
Do not merge it, do not cherry-pick from it without re-testing in Logic.**
Reason: the signed 1.0.13 pkg built from it produced loud, sustained,
pulsing noise bursts during playback in Mike's session (a session created
on 1.0.11; patch = granular + sample + wavetable oscillators, delay +
reverb, no MIDI Learn), in both Logic and the standalone, on a fresh
instance too; switching the reverb off stopped them. Installing the staged
1.0.12 pkg (`b3db6f9304ac701294e0e7318eb34ab8`) over it made the bursts
vanish completely with the same session. The cause was NOT found: no
reverb/delay/voice/FX code differs between 1.0.11 and 1.0.13, and an
offline harness (soak probes with the real preset, all reverb modes,
44.1/96k, block sizes 64-1024, harsh MIDI) was clean on both builds. The
probes are in `git stash` ("soak/preset probes ...") on this machine. The
branch stays on the remote for history. Its useful non-audio pieces (atomic
preset writes, hermetic tests, CI gates, decode caps) could be re-landed
individually later, each verified in Logic by Mike before shipping.

**Logic loading saga (2026-09-04) — resolved, lessons kept:** Logic caches a
per-version validation verdict; a rescan that happens while a bundle is
mid-rebuild, or while two same-identity copies with DIFFERENT versions are
registered (user-domain dev copy vs /Library release), poisons it and
Logic then never re-validates. Fix that worked: install the signed pkg to
/Library with no dev copies present, then Plug-in Manager → Reset & Rescan
Selection → relaunch Logic. Rules: dev plugin copies get rebuilt only as a
deliberate final step right before Mike tests, never in agent verification
passes; never let a dev copy carry a different version than the installed
release; when testing a new version, install the pkg. Diagnostics:
`~/Library/Caches/AudioUnitCache/Logs/AUScan*.plist`, the per-user
`com.apple.audio.AudioComponentCache.plist`, and `/usr/bin/log show`
(zsh's `log` builtin shadows it) filtered on AMFI "adhoc signed" lines,
which prove whether a binary was actually loaded. `auval -a` is just slow
on this Mac (it dlopens hundreds of UAD plugins), not a wedged daemon.
The notary profile vanished a fourth time on 2026-09-04; Mike recreated it.

**Where that leaves the release:** 1.0.12 is installed in /Library and is
the first build since 1.0.8 Mike has actually run installed; his session
plays as expected. Remaining before launch: the rest of the 1.0.12
gauntlet (QWERTY everywhere incl. the VOICE call-out, loose-WAV folder,
dimming, silent preset clicks, banks, loop markers, redesign, soft bypass),
then send to Paul and Phil, then the Shopify build-out and announce. The
1.0.13 artifacts in `dist/installers/` and `dist/shopify/*-1.0.13/` are
obsolete; delete them before any upload. Repo is currently PRIVATE (check
`gh repo view --json visibility`; Mike flips it himself).

## Current state (2026-09-02): v1.0.12 built + staged (pending Mike's sign-off); this is the redesign build

**1.0.11 was never sent.** Mike found one more bug in it — the VOICE
call-out's MODE/PRIORITY dropdowns barely stayed open and selections never
registered (`8cce7f1`: the call-out launched with a null parent and, after
the QWERTY focus fixes, contained nothing focusable, so CallOutBox's modal
grab no-opped and PopupMenu's doesAnyJuceCompHaveFocus dismiss check fell
into a racy native key-window fallback; fixed by parenting to the top-level
component, leaving VoicePanel focusable as the ONE documented exception,
sweeping its on-demand children, and handing focus back to the keyboard on
dismissal; `voicePanelCallOutFocusTest`, suite 385). Mike confirmed the fix
in Logic, then called the next build **1.0.12** (`3714477`).

**1.0.12 = the 2026-09-01 redesign section below plus that fix.** Built +
signed + notarized + staged 2026-09-02: macOS md5
`b3db6f9304ac701294e0e7318eb34ab8`, Windows md5
`92aff81aada650ed56039f265bc12c99` (draft release `ci-windows-3714477`),
byte-identical across `dist/installers/` and
`dist/shopify/SPASynth-{Standard,Pro}-1.0.12/`. Dev copies cleared. All
1.0.10/1.0.11 artifacts in dist/ are obsolete and were never sent.
**Release-build sequencing rule: never push ANY commit between a release
sha's push and fetching its CI exe — the workflow cancels in-progress runs
(bit us on 1.0.11; docs commits go after the fetch).**

**Remaining:** Mike installs 1.0.12 and signs off in Logic (the full
gauntlet: QWERTY everywhere incl. the VOICE call-out, library folder of
loose WAVs, dimming, silent preset clicks, banks, loop-point markers, the
redesign, soft bypass) → send to Paul and Phil → launch checklist
(Shopify build-out, announce) unchanged below. Bump to 1.0.13 for any
change after it goes out.

## Current state (2026-09-01): v1.0.11 — faceplate redesign merged to main, release build in progress

**1.0.10 was never sent; Mike chose a fresh number for the redesign.**
Branch `faceplate-restyle` (2026-08-30..09-01, ~14 commits, merged as
`3fb5391`) delivered a visual-only restyle per Mike's spec + a UVI Thorus
reference, iterated through ~12 of his feedback rounds with rendered
snapshots each time:
- One continuous charcoal faceplate `0xff181d20` (sampled from his mock);
  `draw::panel` is a no-op (no cards); LED display wells removed (scopes
  draw on the surface; `displayWell` keeps an optional faint centre line);
  seeded noise tile at 0.03 alpha; dark gutter seams (`seam` =
  background.darker(1.3), full row-band height); layered row-overhang
  shadows + recessed selector/title channels sharing ONE recipe:
  `draw::easedShadowGradient` + `draw::shadowStartAlpha` (0.30, edge 0.44).
  Headers live in 32px bands (`metrics::sectionHeaderHeight`, tab depth
  matched so rules align per row; `OscStrip::headerNameRect` shares the
  constants). FILTER/FX inner headers are title-only (no rule, no recess —
  the selector above provides the line). Nav row overhangs row 1.
  `meterLane` token split from `seam`. Tab rules unified on `t.outline`.
- **Real bugs found by the restyle work, all fixed + regression-tested:**
  tab bars re-laid-out narrower on first click (ContentComponent's only
  setSize ran BEFORE parenting, so widths came from the default LnF;
  `tabLayoutInvarianceTest`); FX caption labels could clip on dense panels
  (labels reserve height first, display shrinks; `fxPanelLabelClippingTest`);
  matrix last visible row clipped (viewport clamps to whole rows).
- **Loop-point visualization** (Paul's request): sample mode overlays the
  waveform with an accentMod loop band + 1px edge markers (LOOP on) and a
  sampleStart tick; params verified normalized against
  `SamplePlayer::getNextSample`; repaints via the existing display listener
  + 24Hz timer. Snapshot seed sets OSC A loop points to demonstrate it.
- Suite 185 → **377 assertions ALL PASS**; auval SUCCEEDED at every step;
  committed `docs/spasynth-{dark,accent,loading,keyboard,marketing}.png`
  refreshed (`c53935e`). Changelog has a customer-facing `## 1.0.11`.
- Dev-copy workflow for design review: debug builds in `~/Library` shadow
  the installed release (deliberately, for Mike's Logic look); the release
  script clears them. **Never sign off a release while dev copies exist.**

**Remaining:** finish the 1.0.11 release build (macOS + Windows draft
release), stage `dist/shopify/SPASynth-{Standard,Pro}-1.0.11/`, Mike
installs + signs off in Logic (functional gauntlet from the 1.0.10 list +
the redesign + loop points), then send to Paul and Phil; the rest of the
launch checklist (Shopify build-out, announce) is unchanged from below.

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

- `94213fa` **QWERTY died on anything in the preset browser** (Mike found it
  on the first 1.0.10 build). JUCE trace: a row click lands on ListBox's
  private RowComponent, which doesn't want focus, so the grab walks up to the
  ListBox, which does. Closed the whole class this time: a whole-tree
  `setMouseClickGrabsKeyboardFocus(false)` sweep in the SPASynthEditor
  constructor (after `setResizable`, so the corner grip is included), plus
  hooks for the two things created later — ListBox rows (ComponentListener
  on the row container) and ComboBox text labels
  (`SPASynthLookAndFeel::createComboBoxTextBox` override). Exceptions:
  TextEditor subtrees, the MidiKeyboardComponent itself, the browser's own
  Esc-to-close self-focus. `presetBrowserFocusGrabTest` walks the ENTIRE
  editor with the drawer open and fails on any offender (it found 304 before
  the sweep — every FX section toggle/combo, tab bars, scrollbars…). Suite
  now **248 assertions**. Shared helper `disableMouseClickFocusGrab` in
  Controls.h.
- **`SPASYNTH_NOTARY` vanished a THIRD time** (2026-08-28, mid-rebuild;
  `build_release.sh` exited 69 after signing). Recreating it via the `!`
  prefix in Claude Code FAILS with an instant 401 — the hidden password
  prompt doesn't get real input there. It must be run in Terminal.app:
  `xcrun notarytool store-credentials SPASYNTH_NOTARY --apple-id <id>
  --team-id 7K9WY5T49S`. Then submit + staple the already-signed pkg and
  stage by hand (the script skips staging after a notarize failure).

- `e1e2d18` **opening the preset browser took focus from the keyboard**
  (Mike, on the `94213fa` build). `togglePresetBrowser` grabbed focus for the
  browser so Esc could close it. Now it only does that when the keyboard
  strip is hidden; otherwise focus stays on the keyboard and Esc bubbles up
  (`MidiKeyboardComponent::keyPressed` only claims mapped notes →
  `ComponentPeer::handleKeyPress` parent walk → new
  `ContentComponent::keyPressed` closes the drawer). Closing hands focus back
  to the keyboard. `presetBrowserKeyboardFocusTest` puts the editor on the
  desktop and checks REAL focus + Esc through the peer. Suite **256**.

**1.0.10 FINAL, built + staged 2026-08-29 from `e1e2d18`, NOT yet tested by
Mike, NOT sent:** macOS pkg signed + notarized + stapled, `spctl` accepted,
md5 `c21abd598c55f96c11a6d8b4c1153580`; Windows exe from draft release
`ci-windows-e1e2d18`, md5 `1b8eb5e3e0258dfdd9e905a411fcaaa2`; both
byte-identical across `dist/installers/` and
`dist/shopify/SPASynth-{Standard,Pro}-1.0.10/`. Everything 1.0.9 in `dist/`
is obsolete.

**Remaining for launch:**
1. Mike installs 1.0.10 (`sudo installer -pkg … -target /`, agent can't
   sudo) and tests in Logic: RANDOMIZE ALL + QWERTY, SET LIBRARY on a plain
   folder of WAVs, greyed-out LFO rate under SYNC, silent preset clicks after
   playing a note, saving into a New Folder bank, and QWERTY surviving
   clicks anywhere in the preset browser and on FX section toggles. Also the 1.0.9 hardening
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
   1b. **AddressSanitizer run, for any DSP or lifetime change and before
   every release build.** One-time configure (gitignored, no plugin copies
   so it can never shadow the installed release):
   `cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug
   -DSPASYNTH_COPY_PLUGIN=OFF -DCMAKE_OSX_ARCHITECTURES=arm64
   -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer"
   -DCMAKE_C_FLAGS="-fsanitize=address -fno-omit-frame-pointer"
   -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address`, then
   `cmake --build build-asan --target SPASynthTests` and
   `ASAN_OPTIONS=detect_leaks=0 build-asan/SPASynthTests_artefacts/Debug/SPASynthTests`
   a few times (some findings are timing-dependent). It found the reverb
   one-past-the-end read and the VOICE-panel use-after-free on its first
   run (2026-09-07) — bugs the plain suite, auval and pluginval all passed.
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

- **`SPASYNTH_NOTARY` keychain profile is unreliable** — it has vanished six
  times now (`security find-generic-password -s com.apple.gke.notary.tool`
  finds nothing after each loss), cause never identified. Notarization is now
  a standalone step, `scripts/notarize.sh <pkg>`, which prefers a credentials
  FILE (`~/.config/spasynth/notary.env`, mode 600, hand-created by Mike,
  **never inside the repo** — `SPASYNTH_NOTARY_APPLE_ID` /
  `_TEAM_ID` / `_PASSWORD`, an app-specific password) and only falls back to
  `SPASYNTH_NOTARIZE_PROFILE` if that file is absent. `build_release.sh` now
  only signs the pkg via `installers/macos/build_installer.sh`, then calls
  `scripts/notarize.sh` itself; on failure it exits 69 and skips Shopify
  staging. To recover without a rebuild: fix credentials, run
  `scripts/notarize.sh dist/installers/SPASynth-<v>-macOS.pkg`, then
  `scripts/build_release.sh --stage-only <v>`.
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
