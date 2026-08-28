# SPASynth Shopify product listings

Paste-ready copy for the three Shopify products (Standard, Pro, Standard to
Pro Upgrade). Written from `docs/marketing-brief.md`, factually accurate to the
shipping v1.0.2 build. House style: no em dashes.

Staged deliverables to attach live in `dist/shopify/` (gitignored). Set each
product's **Price** to the intro number and **Compare-at price** to the regular
number to get the struck-through sale display.

## Pricing (USD; intro-period length TBD)

| Product | Price (intro/sale) | Compare-at (regular) |
| --- | --- | --- |
| Standard | $99 | $149 |
| Pro | $499 | $899 |
| Upgrade (Standard → Pro) | $400 | $750 |

Upgrade is the difference at each tier: `499 − 99 = 400` now, `899 − 149 = 750`
regular, so an intro Standard buyer pays exactly what buying Pro outright during
the sale would have cost.

## File attachments per product

The 37 GB Pro library is too big for Shopify's per-product cap, so it lives on
**Cloudflare R2** and is delivered as a small self-contained HTML download page
(`SPASynth Pro Library - Downloads.html`, in `dist/shopify/`; buyers open it and
click the 11 download buttons). The 11 parts
are at `https://downloads.spasynth.com/pro-library/SPASynth-Pro-Library-Part-NN.zip`
(NN = 01..11). Standard's 3 GB starter library still uploads to Shopify directly.

- **Standard:** `SPASynth-1.0.2-macOS.pkg`, `SPASynth-1.0.2-Windows.exe`,
  `SPASynth Starter Library.zip`, `README.txt`, `QUICKSTART.txt`, `EULA.txt`
- **Pro:** the pkg + exe + the 3 docs + `SPASynth Pro Library - Downloads.html`
  (the library itself is on R2, NOT uploaded to Shopify)
- **Upgrade:** `SPASynth Pro Library - Downloads.html` + `QUICKSTART.txt` +
  `EULA.txt` (library only; buyer already owns the synth)

macOS pkg is signed + notarized. Windows exe is unsigned by decision (SmartScreen
"More info → Run anyway"). R2 has no egress fees, so customer downloads are ~free.

## About block (append to each product description)

Paste this near the bottom of all three product descriptions:

> **About Silverplatter Audio**
> Silverplatter Audio is a boutique sound-effects library company. Drawing on
> years of recording and mastering game audio, sound design for film, and
> sound effects for video and music production, we create professional,
> high-quality sound-effects packs for sound designers around the world, with
> a focus on usability, convenience, and affordability so audio drops cleanly
> into games, film, TV, web, and more. SPASynth is the instrument we built to
> play those libraries, and we sell it the same way we sell our packs: no copy
> protection, because we would rather build for people who pay for the tools
> they love.

---

# 1 · SPASynth Standard

**Title:** SPASynth Standard

**Subtitle:** The hybrid synth where your sound library becomes the instrument.

Other synths give you waveforms. SPASynth gives you a Foley-grade sound-effects
library as the synthesis material. A typewriter, a rainstorm, or a billiard break
becomes a playable keyboard instrument, a granular cloud, or the modulation source
that pumps the filter.

Standard is the complete synthesizer. Every engine, every feature, all 264 factory
presets, and a 440-sound starter library drawn from all 88 Silverplatter Audio
packs (five representative sounds from each). Everything is here except the full
37 GB library, and you can add that any time with the Pro upgrade.

**What you get**
- The full SPASynth synthesizer (AU, VST3, and Standalone)
- 440-sound starter library spanning all 88 packs
- 264 factory presets, auto-generated from the library
- Free updates across the v1 line

**Inside the synth**
- 7 synthesis engines per oscillator slot, across 3 slots: wavetable, sample/SFX, granular, virtual analog, 2-operator FM, noise, and Karplus-Strong pluck
- In-pack quick-swap: audition every variant in a pack right from the oscillator, click the sample name for a dropdown of the whole pack, no file dialogs
- The SFX-as-modulator system: every loaded sound is analyzed for loudness and pitch, and those contours become live modulation sources. Route a thunderstorm's loudness to the filter and the storm plays the synth.
- Dual multimode filters, 8 types each, with series or parallel routing, drive, keytracking, envelope depth, and mix
- Organic Chaos: per-voice analog-style drift in pitch, phase, wavetable position, amplitude, saturation, and distortion, so digital patches breathe
- 16-slot modulation matrix, 20 sources into more than 90 destinations, with 3 host-synced LFOs and 3 envelopes
- RANDOMIZE ALL, a genuine patch generator with a WILD amount knob and 8 per-section locks. Every roll is guaranteed to make sound.
- 12-mode arpeggiator with swing, latch, up to 4 octaves, and CHANCE, STUTTER, JUMP, and HUMANIZE probability controls
- Global glide (Off, Always, Legato), 16-voice polyphony, pitch bend, mod wheel, aftertouch
- MIDI Learn on every control, right-click and move a knob
- FX chain with live scopes: distortion, chorus, tempo-synced ping-pong delay, reverb, and 3-band EQ
- Searchable slide-out preset browser with favorites, and automatic library discovery
- Resizable, telemetry-animated interface with user-tintable accent colors

**Formats and requirements**
- macOS 11 or later (universal, Apple silicon and Intel): AU, VST3, Standalone
- Windows 10 or later (64-bit): VST3, Standalone

**No DRM, ever.** No serial numbers, no activation, no dongles, no phone-home. Buy
it, download it, own it. Use the sounds in anything you make, royalty-free,
forever. Just do not redistribute the raw files.

Want the whole library later? The Standard to Pro upgrade is always available for
the difference in price. Download, extract to the same folder, done. Nothing to
reinstall.

### SEO
- **Page title:** SPASynth Standard | The Sound-Library Synth
- **URL handle:** `spasynth-standard`
- **Meta description:** The hybrid synth where your sound library is the instrument. Full synth, 7 engines, 264 presets, and a 440-sound starter library. No DRM. Mac and Windows.

### Card blurbs
- **Primary:** The hybrid synth where your sound library is the instrument. Full synth, 264 presets, and a 440-sound starter library to get you playing.
- **Short:** The full sound-library synth, with a 440-sound starter library.

### Tags
SPASynth, SPASynth Standard, Synthesizer, Virtual Instrument, Plugin, Hybrid Synth, Wavetable, Granular, Sample-Based, Sound Design, Cinematic, Film & TV, Game Audio, AU, VST3, Standalone, macOS, Windows, Royalty-Free, DRM-Free, Starter Library, Silverplatter Audio

---

# 2 · SPASynth Pro

**Title:** SPASynth Pro

**Subtitle:** The complete synth and the entire 11,474-sound Silverplatter library.

SPASynth Pro is the full instrument with nothing held back. You get the same
synthesizer as Standard plus the complete Silverplatter Audio library: 88 packs,
11,474 production-ready sounds, 37 GB at 24-bit / 48 kHz, cleared for film and TV
work.

This is the edition SPASynth was built for. The library is not a bonus pack, it is
the synthesis engine. Load any of the 11,474 sounds as an oscillator, granulate it
into an evolving texture, or let its own amplitude and pitch contours modulate the
patch. Then reach for RANDOMIZE ALL and let the whole library become a patch
generator.

**What you get**
- The full SPASynth synthesizer (AU, VST3, and Standalone)
- The complete 88-pack, 11,474-sound library, 37 GB at 24-bit / 48 kHz
- 264 factory presets, auto-generated from the library
- Free updates across the v1 line

**Inside the synth**
- 7 synthesis engines per oscillator slot, across 3 slots: wavetable, sample/SFX, granular, virtual analog, 2-operator FM, noise, and Karplus-Strong pluck
- In-pack quick-swap: audition every variant in a pack right from the oscillator, click the sample name for a dropdown of the whole pack, no file dialogs
- The SFX-as-modulator system: every loaded sound is analyzed for loudness and pitch, and those contours become live modulation sources
- Dual multimode filters, 8 types each, with series or parallel routing, drive, keytracking, envelope depth, and mix
- Organic Chaos: per-voice analog-style drift in pitch, phase, wavetable position, amplitude, saturation, and distortion
- 16-slot modulation matrix, 20 sources into more than 90 destinations, with 3 host-synced LFOs and 3 envelopes
- RANDOMIZE ALL patch generator with a WILD amount knob and 8 per-section locks
- 12-mode arpeggiator with swing, latch, up to 4 octaves, and CHANCE, STUTTER, JUMP, and HUMANIZE probability controls
- Global glide (Off, Always, Legato), 16-voice polyphony, pitch bend, mod wheel, aftertouch
- MIDI Learn on every control
- FX chain with live scopes: distortion, chorus, tempo-synced ping-pong delay, reverb, and 3-band EQ
- Searchable slide-out preset browser with favorites, and automatic library discovery
- Resizable, telemetry-animated interface with user-tintable accent colors

**The library**
- 88 Silverplatter Audio packs, 11,474 sounds, 24-bit / 48 kHz, production-ready for film and television
- Auto-discovery loading: extract to one folder and SPASynth finds it. No scanning dialogs, no locate-your-content prompts.
- Portable paths, so your presets and sessions survive the library living on any drive

**Formats and requirements**
- macOS 11 or later (universal, Apple silicon and Intel): AU, VST3, Standalone
- Windows 10 or later (64-bit): VST3, Standalone
- The full library is 37 GB. An external or secondary drive works fine, point SPASynth anywhere.

**No DRM, ever.** No serial numbers, no activation, no dongles, no phone-home. Buy
it, download it, own it. Use the sounds in anything you make, royalty-free,
forever. Just do not redistribute the raw files.

### SEO
- **Page title:** SPASynth Pro | Synth + 11,474-Sound Library
- **URL handle:** `spasynth-pro`
- **Meta description:** The complete SPASynth synth plus the full 11,474-sound, 37 GB Silverplatter library that becomes your oscillator, granular fuel, and modulator. No DRM.

### Card blurbs
- **Primary:** The complete synth and the entire 11,474-sound Silverplatter library. Every sound becomes an oscillator, a granular texture, or a modulator.
- **Short:** The full synth plus all 11,474 sounds, 37 GB of library.

### Tags
SPASynth, SPASynth Pro, Synthesizer, Virtual Instrument, Plugin, Hybrid Synth, Wavetable, Granular, Sample-Based, Sound Design, Cinematic, Film & TV, Game Audio, AU, VST3, Standalone, macOS, Windows, Royalty-Free, DRM-Free, Full Library, Sound Effects Library, Silverplatter Audio

---

# 3 · SPASynth Standard to Pro Upgrade

**Title:** SPASynth Standard to Pro Upgrade

**Subtitle:** Already own Standard? Add the complete library for the difference.

This upgrade takes you from the 440-sound starter library to the full Silverplatter
Audio collection: 88 packs, 11,474 sounds, 37 GB at 24-bit / 48 kHz. You keep the
synth you already have and simply add the content it was built for.

You pay only the difference between Standard and Pro. There is nothing to reinstall
and nothing to activate. Download the library, extract it to the same Silverplatter
Audio folder, and SPASynth picks it up automatically the next time it opens.

**What you get**
- The complete 88-pack, 11,474-sound library, 37 GB at 24-bit / 48 kHz
- The same production-ready, film-and-television-cleared sounds included in Pro

**How it works**
1. Purchase and download the library files.
2. Extract them into your existing SPASynth library folder.
3. Open SPASynth. The full library and all pack presets are simply there.

**For existing SPASynth Standard owners.** This upgrade contains the library only,
since you already own the synth. If you do not own Standard yet, buy SPASynth Pro
instead for the synth and the full library together.

**No DRM, ever.** No serial numbers, no activation, no phone-home. The same simple
license: use the sounds in anything you make, royalty-free, forever, just do not
redistribute the raw files.

### SEO
- **Page title:** SPASynth Standard to Pro Upgrade
- **URL handle:** `spasynth-standard-to-pro-upgrade`
- **Meta description:** Own SPASynth Standard? Add the full 11,474-sound, 37 GB library for the difference. Download, extract, done. Nothing to reinstall and nothing to activate.

### Card blurbs
- **Primary:** Already own Standard? Add the complete 11,474-sound library for the difference. Download, extract, done.
- **Short:** Add the full 11,474-sound library for the difference.

### Tags
SPASynth, SPASynth Upgrade, Library Upgrade, Full Library, Sound Effects Library, Sound Design, Cinematic, Film & TV, Game Audio, Foley, Textures, Royalty-Free, DRM-Free, Silverplatter Audio

---

# Shared FAQ block

Use on a dedicated FAQ page, or in the lower section of each product description.
Written to stand alone so it works in either spot.

**Which DAWs and formats are supported?**
SPASynth ships as AU, VST3, and Standalone on macOS, and as VST3 and Standalone on
Windows. That covers Logic Pro, GarageBand, Ableton Live, Cubase, Studio One,
Reaper, Bitwig, FL Studio, and any other AU or VST3 host. There is no AAX in
version 1, so it does not load in Pro Tools yet.

**What are the system requirements?**
macOS 11 or later, universal for both Apple silicon and Intel. Windows 10 or later,
64-bit. The synth itself is lightweight. The Pro library is 37 GB, so plan for the
disk space if you buy Pro or the upgrade.

**Do I have to keep the library on my system drive?**
No. Extract it anywhere, including an external or secondary drive, and point
SPASynth at that folder once. SPASynth auto-discovers the library on the next
launch, with no scanning dialogs. Presets and sessions use portable paths, so they
keep working no matter where the library lives.

**I already own Silverplatter sound-effects libraries. Can SPASynth use them?**
Yes, and you do not need to copy anything into a separate SPASynth folder.
Point SPASynth at the folder that holds your packs using "SET LIBRARY" in the
preset browser. Two layouts both work: a folder whose subfolders are packs
(each treated as its own category), or a plain folder of WAV files with no
subfolders at all. Either way SPASynth builds factory presets from the sounds
it finds. Sample rate does not matter, so your 24-bit / 96 kHz originals play
as-is with no conversion. If you own some of the packs, you see exactly those;
add more packs or Pro to fill in the rest.

**Do I need to re-download or reorganize my libraries?**
No re-download and no conversion. Keep your libraries where they are and point
SPASynth at them. The only thing to know is that SPASynth reads one library
folder at a time and treats its subfolders as packs, so if your packs are spread
across different drives or locations, put them under a single parent folder (or
point SPASynth at whichever one you want) so it can see them together.

**I own the Everything Bundle. Do I get SPASynth?**
Yes. If you own the Everything Bundle you already own every sound SPASynth plays,
so SPASynth is yours as part of your lifetime updates. You get the synth itself,
and you point it at your existing Everything Bundle folder with "SET LIBRARY".
There is nothing to re-download and nothing to reorganize, and because there is
no copy protection, installing it is all it takes.

**What is the difference between Standard and Pro?**
The synth is identical. Standard includes a 440-sound starter library, five
representative sounds from each of the 88 packs, so every factory preset plays. Pro
includes the complete library: 88 packs, 11,474 sounds, 37 GB at 24-bit / 48 kHz,
cleared for film and television work.

**How does the upgrade to Pro work?**
Buy the Standard to Pro upgrade for the difference in price. Download the library
files, extract them into your existing SPASynth library folder, and open SPASynth.
The full library and all pack presets are simply there. There is nothing to
reinstall and nothing to activate.

**Is there copy protection?**
None, ever. No serial numbers, no activation, no dongles, and it never phones home.
You buy it, you download it, you own it. We sell to people who pay for the tools
they love, and we price it accordingly.

**Can I use the sounds in my own work?**
Yes. Use the sounds in anything you make, royalty-free, forever, including
commercial film, television, games, and music. The only rule is that you do not
redistribute the raw sound files themselves.

**Will I get updates?**
Yes. Updates across the version 1 line are free. Because there is no activation,
updating is just downloading the latest installer and running it.

**How many sounds and presets are included?**
Pro includes 11,474 sounds across 88 packs. Both editions include 264 factory
presets, automatically generated from the library, with a playable Keys patch, a
granular Texture, and an SFX-driven Pulse for every pack.

**The Windows installer shows a security warning. Is that normal?**
Yes. The Windows installer is not certificate-signed, so Windows SmartScreen may
show a caution on first run. Click "More info" and then "Run anyway" to proceed.
The macOS installer is fully signed and notarized by Apple, so it installs without
warnings.
