# SPASynth handoff (2026-08-03)

Quick "start here" for the next session. Full detail lives in `CLAUDE.md`; this
is the short version.

## Where we are

- **v1.0.3 is merged to `main`** (release merge `2559c2f`, CMake version 1.0.3).
  `HEAD = ca5d6c4` (arp fix); `1087072` on top is just an empty CI-trigger
  commit.
- All 11 planned features + a round of smoke-test refinements are done, tested
  (`SPASynthTests` ALL PASS), and validated (`auval` PASS, `pluginval`
  strictness-8 SUCCESS).
- **1.0.3 is NOT distributed yet.** Stay on 1.0.3 and keep iterating (do not
  bump the version) until it actually ships.
- The signed + notarized macOS pkg and the CI-built Windows exe in
  `dist/installers/` and `dist/shopify/SPASynth-{Standard,Pro}-1.0.3/` were last
  rebuilt from the arp fix and are byte-identical across locations.

## What shipped in 1.0.3

11 base features: panic button, standalone tempo, FX drag-reorder, Mod
(Phaser/Flanger), Trem/Vib, Limiter/Maximizer, Convolve, FDN reverb
(Hall/Plate/Chamber/Room/Spring), 8-band parametric EQ (node editor + FFT
analyzer), voice modes (Poly/Mono/Duo/Paraphonic/Unison), whole-synth
oversampling. Plus smoke-test work: FX-tab grip fix, glide layout, EQ node
interactions (double-click add/remove, Cmd/Ctrl-drag Q), scrolling limiter
meter, Convolve library dropdown + IR shaping (pre-delay/decay/damping) +
waveform view, RANDOMIZE-ALL FX-order shuffle, limiter auto-gain, and the arp
stuck-notes fix. Customer changelog: `docs/CHANGELOG.md`.

## Open items (Mike's manual steps)

1. **Re-private the GitHub repo** — it is currently PUBLIC (flipped so Windows
   CI could build the arp fix). Flip back to Private when done rebuilding.
2. **Install + smoke-test the macOS pkg** (`sudo installer -pkg
   dist/installers/SPASynth-1.0.3-macOS.pkg -target /`; the agent can't sudo).
3. **Windows real-DAW smoke test** — the one surface never tested in a DAW.
4. **Shopify build-out** per `docs/shopify-setup-guide.md`; the 1.0.3 folders are
   ready to attach.
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

- **Windows CI needs the repo PUBLIC.** Private + drained Actions quota =
  Windows job fails in ~3s with no steps. Make it public, build, re-private.
- **`gh` token expired mid-session once** -> `gh auth login` (PAT needs `repo` +
  `workflow`; Actions:read is enough, no rerun via API).
- Verification ritual for every change: build `SPASynthTests` and run it
  (expect ALL PASS), look at `--snapshot` renders for UI, then `auval` +
  `pluginval` strictness-8 for anything touching the audio thread.
- Load-bearing invariants (do not break): `CMAKE_OSX_DEPLOYMENT_TARGET=11.0`;
  append-only choice orders (FX module ids, EQ band types, voice/reverb/EQ
  character modes); RT-safety on the audio thread; per-preset `fxOrder` packed
  atomic. See CLAUDE.md's invariants section.

## House style (customer-facing copy)

First-person company voice ("we"/"our"/Silverplatter Audio), never name
individuals, **no em dashes**, sound count 11,474.
