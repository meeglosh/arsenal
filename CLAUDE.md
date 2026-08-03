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

## Current state (2026-08-03): v1.0.3 — merged to `main`, built + signed, in smoke testing

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
macOS); `gh run download <id> -n spasynth-installer-Windows`; copy the pkg+exe
into the two shopify folders; verify one-hash byte-identity + `minos 11.0` +
`spctl` accepted. (Mike's PAT expired mid-session once — `gh auth login` fixes
it; the PAT needs `repo` + `workflow` scopes, Actions:read is enough.)

**Remaining for launch (Mike's manual steps):**
1. **Re-private the GitHub repo** — it was made public for the Windows CI builds.
2. **Install + smoke-test the macOS pkg** (`sudo installer -pkg … -target /`; the
   agent can't sudo). Exercise the new surfaces: EQ node editor, FX reorder +
   RANDOMIZE reshuffle, voice modes, oversampling, the limiter meter + auto-gain,
   the Convolve waveform/shaping + library browser.
3. **Windows real-DAW smoke test** — the one untested surface.
4. **Shopify build-out** per `docs/shopify-setup-guide.md`; the 1.0.3 folders are
   ready to attach.
5. Marketing site / announcement when ready.

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
push = no 10x macOS), `gh run download ... -n spasynth-installer-Windows`, then
re-private. PAT has Actions:read (Mike enabled it) but not Actions:write (cannot
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
   `build/SPASynthTests_artefacts/Debug/SPASynthTests` → expect `ALL PASS`
   (140+ assertions). Fix every new compiler warning — one caught a real
   Filter-2 lock bug.
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
- CI (`.github/workflows/build.yml`): macOS universal + Windows x64 + both
  unsigned installers as artifacts. macOS job is slow (~1 h JUCE build).
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
