# SPASynth changelog

## 1.0.9

A performance and stability pass, wrapping up ahead of launch.

- Bypassing SPASynth in your host now lets reverb, delay, and other effect
  tails ring out naturally instead of cutting them off instantly.
- Lower CPU use for long convolution impulses, and a lighter-weight reverb
  tail engine, with no change to how either sounds.
- The EQ's spectrum analyzer no longer uses CPU while its tab isn't in view.
- General hardening: tightened internal safety margins and a smaller memory
  footprint in a couple of engine components. Nothing here changes how any
  patch sounds.

**Notes for this build**

- The macOS installer is signed and notarized by Apple, so it installs cleanly.
- The Windows installer is unsigned by design. On first launch, click More info
  and then Run anyway to get past the SmartScreen prompt.
- Runs on macOS 11 or later (Apple silicon and Intel) as AU, VST3, and
  Standalone, and on Windows 10 or later as VST3 and Standalone.
- The Limiter's optional lookahead mode adds a small amount of latency and
  reports it to your host automatically so playback stays in sync. It's off
  by default, so live play stays at zero added latency until you turn it on.
- Known limitation: on a Mac with a Retina laptop screen plus an external
  monitor, the standalone window may not drag across onto the external display
  (a fixed-aspect window plus mixed-resolution quirk in the window system). It
  works normally as a plugin in your DAW. Workaround: set the external display
  as your main display in System Settings, or use SPASynth as a plugin.

## 1.0.8

A small fix for anyone using the on-screen keyboard's QWERTY (computer
keyboard) note input.

- Fixed QWERTY note input stopping after touching a knob or dropdown.
  Previously, playing notes from your computer keyboard would stop working
  the moment you turned any knob or changed any setting, and would only
  resume after clicking directly on a key in the on-screen keyboard. Knobs,
  dropdowns, and toggles no longer take over keyboard focus, so QWERTY play
  keeps working while you tweak the sound.

## 1.0.7

A targeted fix for a serious tester-reported bug, plus extra safety hardening.

- Fixed loud noise bursts in reopened DAW sessions. Reopening a saved session
  could occasionally produce intermittent loud blasts of noise over the
  patch, even while the session sat idle. The cause was a race while the
  session's settings were being restored: the audio engine could briefly run
  on a half-applied mix of old and new settings, and the resulting burst of
  energy then kept re-emerging from the delay's feedback loop. Restoring is
  now properly synchronized with the audio engine, so this cannot happen.
- Effects no longer carry stale energy across an off/on toggle. Switching an
  EQ band or the phaser/flanger off and back on could release a burst of
  sound trapped from before the toggle; those effects now start clean every
  time they are re-enabled.
- New output safety net. As an extra layer of protection, the synth now
  detects and clears any invalid audio state instead of letting it circulate,
  and the final output is capped at a hard ceiling so no malfunction,
  whatever the cause, can produce ear-damaging levels in headphones.

## 1.0.6

A response to tester feedback, focused on levels and safety.

- Reverb levels rebalanced. The reverb's wet signal was running much hotter
  than it should have, which made the MIX knob feel touchy (a little went a
  long way) and let 100 percent mix get loud enough to distort. The wet path
  now sits at a natural level: MIX sweeps smoothly from subtle to full wash,
  and full wet no longer overloads. Factory presets were retuned to match.
  Note: patches you saved with heavy reverb will sound a bit drier than
  before; nudge MIX up to taste.
- RANDOMIZE ALL volume safety. Random patches could occasionally land
  painfully loud, especially in headphones, when several loud settings
  stacked together. Randomize now keeps the combined oscillator level within
  a sensible ceiling (preserving the balance between oscillators, so patches
  stay just as varied in character) and leaves the limiter switched on at
  transparent settings as a safety net. You can switch the limiter off
  afterward if you prefer.

## 1.0.5

A stability and hardening release. We ran a full audit of the code ahead of
launch and fixed every crash, freeze, and reliability issue it turned up;
there are no new features here, just a more solid foundation.

- Fixed a crash when loading a damaged or hand-edited preset file.
- Fixed a possible crash when loading a corrupted WAV file.
- Fixed a rare freeze when a host reports invalid timeline data to the
  arpeggiator.
- Fixed possible crashes when closing the plugin while samples were still
  loading, or while a menu or dialog was open.
- The reported effect tail now includes the Convolve impulse, so bounces and
  freezes no longer cut it short.
- Smoother performance when oversampling is active.

## 1.0.4

Fixes and small improvements from real-world testing feedback.

- Filter 1 now has its own on/off switch, matching Filter 2.
- Reset to Default. A new "Reset to Default" option in the settings menu
  brings every parameter back to its starting point, handy after RANDOMIZE
  ALL or a long tweaking session.
- File browsers now remember the last folder you opened, for both
  sample/wavetable loading and the Convolve impulse browser.
- Fixed the Convolve impulse browser showing valid WAV files as greyed out.
- Tuned the reverb's Decay range so Hall mode no longer produces an overly
  long, uncontrolled-sounding tail at high settings.
- If your library lives on an external drive, reconnecting after unplugging
  it is now much more reliable. Rescan Library offers to point you at a new
  folder instead of silently doing nothing, and sample/wavetable loading
  retries automatically instead of occasionally showing a spurious
  "unrecognized format" error right after a reconnect.

## 1.0.3

A big update to the effects, the voice engine, and sound quality.

**New effects**

- Panic button. If a note ever gets stuck, one click stops all sound and clears
  any held or latched notes. It also responds to your host's panic control.
- Reorderable effects. Drag the effect tabs (there is a grip handle on each one)
  to change the order the effects run in, and the chain order is saved with your
  preset. RANDOMIZE ALL can now shuffle the order too, and the limiter keeps its
  place at the end of the chain.
- Mod tab. A new modulation effect that switches between Phaser and Flanger,
  with rate (free or tempo-synced), depth, feedback, stages, and stereo spread.
- Tremolo / Vibrato tab. Independent tremolo (amplitude) and vibrato (pitch),
  usable together, with shape, rate, depth, and stereo controls.
- Limiter / Maximizer. A loudness stage with drive, ceiling, character modes,
  optional lookahead, and auto-gain, shown on a scrolling gain-reduction meter
  so you can see it work in real time. It sits last in the chain by default.
- Convolve. Use any sound from your library, or your own WAV, as a convolution
  impulse for reverbs, spaces, and creative textures. Browse your library packs
  right from the tab, shape the impulse with pre-delay, decay, and damping, and
  watch it on a live waveform view.
- Reverb algorithms. The reverb now offers Hall, Plate, Chamber, Room, and
  Spring, with pre-delay, size, decay, damping, tail modulation, and tone
  controls.
- Parametric EQ. A new Pro-style EQ with up to 8 bands, draggable nodes (drag
  for frequency and gain, mouse wheel or Cmd/Ctrl-drag for Q, double-click to
  add or remove a band), a live spectrum analyzer, and Clean / Modern / Vintage
  / Tube character modes.

**New voice engine**

- Voice modes. Choose Poly, Mono, Duo, Paraphonic, or Unison from the new VOICE
  button in the top bar, with note priority (Last / High / Low) and unison
  voices, detune, and width.
- Standalone tempo. The standalone app now has its own tempo: set the BPM, tap
  it in, or follow an external MIDI clock, so tempo-synced effects and the
  arpeggiator lock to the right speed without a host.

**Sound quality**

- Oversampling. Run the whole synth at 2x, 4x, or 8x for cleaner, lower-alias
  sound on bright and hard-driven patches. Off by default; choose it from the
  settings menu.

## 1.0.2

**New**

- On-screen keyboard. Click the keyboard icon at the bottom-right of the window
  (or open the settings menu) to show a playable keyboard strip. Play it with
  the mouse or with your computer keyboard (QWERTY), so you can audition sounds
  without a MIDI controller connected. Click the icon again to hide it.
- Settings menu. Click the SPASynth logo in the top-left to open a settings menu
  with Set Library Folder, Rescan Library, Accent Colors, Show Keyboard, and
  Clear All MIDI Learn. It works the same in the plugin and the standalone.

**Fixed**

- The standalone app now launches on macOS 11 (Big Sur) and later. A missing
  build setting made it require a much newer macOS, so it would refuse to open
  on older systems even though the plugins loaded fine.
- Reverb Mix now works as a true dry/wet dial: fully dry at 0 percent, fully wet
  (pure reverb) at 100 percent, with a smooth, even sweep in between. Before, the
  dry signal never fully left and enabling reverb nudged the level up.
- Arpeggiator swing no longer drops notes. With swing turned up, every second
  step could be skipped depending on the audio buffer size.
- The master output meter no longer sits flush against the right edge of the
  window.

**Notes for this build**

- The macOS installer is signed and notarized by Apple, so it installs cleanly.
- The Windows installer is unsigned by design. On first launch, click More info
  and then Run anyway to get past the SmartScreen prompt.
- Runs on macOS 11 or later (Apple silicon and Intel) as AU, VST3, and
  Standalone, and on Windows 10 or later as VST3 and Standalone.
- Known limitation: on a Mac with a Retina laptop screen plus an external
  monitor, the standalone window may not drag across onto the external display
  (a fixed-aspect window plus mixed-resolution quirk in the window system). It
  works normally as a plugin in your DAW. Workaround: set the external display
  as your main display in System Settings, or use SPASynth as a plugin.
