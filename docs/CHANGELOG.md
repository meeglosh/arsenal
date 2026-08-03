# SPASynth changelog

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
