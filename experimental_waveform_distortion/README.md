# Fr330hfr33

> Experimental waveform/distortion branch. This folder builds independently
> from the release candidate in the repository root.

## Building the experiment

Prerequisites:

- CMake
- The Arm GNU toolchain (`arm-none-eabi-gcc`)
- A Raspberry Pi Pico SDK checkout

Set the standard Pico SDK environment variable, then run the helper from this
folder:

```sh
export PICO_SDK_PATH=/path/to/pico-sdk
./build_experimental.sh
```

If `pico-sdk` and `303bass` are sibling folders, setting `PICO_SDK_PATH` is
optional. The helper always starts with a clean
`experimental_waveform_distortion/build` directory, builds there, moves the
finished UF2 into `experimental_waveform_distortion/uf2`, and removes the build
directory afterward—even when a build fails.

The editor/firmware SysEx layout and compatibility notes are maintained in
[`protocol.md`](protocol.md).

Release-candidate firmware for the Fr330hfr33 Music Thing Modular Workshop
Computer program card.

Fr330hfr33 is a compact acid bass instrument with a sawtooth oscillator,
four-pole resonant ladder filter, accented AR envelope, pitch glide, USB MIDI,
and a scale-aware random sequencer.

## Release Candidate

Hardware-tested release-candidate UF2:

```text
uf2/Fr330hfr33-v0.9.0-rc1.uf2
```

SHA-256:

```text
50aaab12139e2a007f69be9eff87c80a5031bf7ab8d40b23b73c7c0e04f091a3
```

Version `0.9.0-rc1` was promoted after the full available hardware test pass:
boot and sustained stability, pitch, filter and resonance behavior, envelope,
accent, manual and MIDI legato glide, internal/external/MIDI clocks, Web MIDI
configuration, outputs, generative sequencing, and battery-pull behavior.


## Current Feature Set

- Web MIDI-selectable sawtooth or raw phase-derived square oscillator.
- Web MIDI-selectable filtered-voice distortion: off, RAT-style hard clipping,
  or Tube Screamer-style soft clipping, with 0–100% drive and tone controls.
- Four-pole fixed-point TPT ladder filter with algebraically solved feedback.
- Single AR envelope controlling amplitude and filter modulation.
- Velocity accent with a stronger, shorter envelope response.
- Pitch glide coupled musically to envelope decay.
- External 1V/oct CV and gate control.
- USB MIDI device mode for computers and browsers.
- USB MIDI host mode for class-compliant controllers.
- Last-note-priority monophonic MIDI handling with clean disconnect recovery.
- Random scale-quantized sequencer with internal or external clock.
- History-aware melodic generation that avoids immediate repeats, discourages
  recently played notes, favors nearby scale motion, and occasionally jumps an
  octave or makes a wider leap.
- Calibrated pitch CV and gate outputs for controlling other modules.
- Initial single-file Web MIDI editor.

## Controls

The three knobs keep the same purpose in every playing mode:

- **Main:** filter cutoff
- **X:** resonance
- **Y:** envelope decay and glide

### Filter Behavior

Main controls the resting cutoff with a curved response:

- Fully counter-clockwise produces a dark, rounded bass tone.
- The middle range opens the harmonics progressively and is intended to be the
  broadest useful performance area.
- Turning clockwise makes the rising saw brighter and increasingly exposes
  envelope and resonance interactions.
- Fully clockwise should sound substantially more open than the previous
  build, approaching the bright end of the available analogue comparison.

X controls resonance with a strongly curved response:

- The lower half adds body and a gentle emphasis around the cutoff.
- The upper half becomes increasingly nasal and acid-like.
- Strong resonance is concentrated near the top of the knob.
- Self-oscillation or pinging should appear only near the final part of travel,
  rather than replacing the useful resonance range.
- Resonance makeup gain reduces the large volume loss previously heard above
  noon.

Each articulated note now fires a separate filter contour that decays while
the gate remains high. This should produce an obvious downward “wah” sweep,
especially around Main noon and X between noon and 3 o’clock. X adds only a
small amount of extra sweep, so resonance and envelope motion remain usable
independently.

Y controls both envelope decay and glide. Counter-clockwise gives short notes
and nearly immediate pitch changes. The glide map is deliberately perceptual:
around noon selected slides should already be obvious, while clockwise reaches
a much longer release, filter sweep, and exaggerated pitch travel.
Glide only applies when selected by Pulse In 2, MIDI legato, or sequencer glide
probability.

### Switch Up — CV / MIDI

- `CV In 1`: 1V/oct pitch
- `Pulse In 1`: external gate
- `Pulse In 2` high: manually enables glide for CV and MIDI pitch changes
- USB MIDI notes take priority while a MIDI note is held
- MIDI velocity 112–127 produces an accent

Accent now adds a much stronger filter-contour push plus approximately 25%
post-filter output gain, so accented notes should sound distinctly brighter,
louder, and punchier rather than disappearing into the normal VCA ceiling.

Pitch changes are immediate when Pulse In 2 is low. When Pulse In 2 is high,
the pitch slides at the time set by Y. Pulse In 2 is level-sensitive: hold it
high across the pitch change that should glide.

MIDI also supports automatic 303-style legato slide. Pressing a new MIDI note
before releasing the previous note keeps the gate and envelope open, then
glides to the new pitch using the Y setting. Releasing the newest note while an
older note remains held glides back to that older note without retriggering the
envelope. A note played after all previous notes have been released starts a
new envelope and changes pitch immediately, unless Pulse In 2 is held high.

### Switch Middle — Random Sequencer

- Uses the internal clock when no recent external clock is present
- `Pulse In 2`: external sequencer clock
- Scale, accent probability, octave range, and tempo come from the Web MIDI
  editor
- The default lowest note is MIDI note 36 (C2); root transposition moves the
  scale upward by up to eleven semitones
- Base octave can be selected from C1, C2, C3, or C4
- Gate length and per-step glide probability are configurable
- Optional MIDI clock sync advances the sequence every 12 MIDI clock ticks

The generator is not a fixed looping pattern. It remembers the last few scale
positions, avoids immediate repeats, generally walks to nearby notes, and
occasionally makes a wider or octave jump. Changing scale, root, base octave,
or range resets that melodic history.

### Switch Down — Battery Pull

Holding the momentary switch down simulates pulling the batteries from a
running TB-303. The virtual supply sags, the gate falls, pitch droops, the
filter and resonance lose authority, and both audio outputs collapse through a
longer audible dying tail. Releasing the switch restores power smoothly.

Audio Out 1 holds the VCA level present at the instant the switch is pressed,
then lets the virtual supply collapse fade it. This prevents the filtered voice
from turning into a plain mute before the pitch and filter failure can be
heard. Audio Out 2 remains the continuous raw-oscillator view of the same
collapse. Its magnitude response is unfiltered, with cutoff-tracking all-pass
phase compensation so it can be blended with Audio Out 1 without pronounced
parallel-filter cancellation.

## Outputs

- `Audio Out 1`: enveloped post-filter voice
- `Audio Out 2`: phase-aligned raw sawtooth oscillator
- `CV Out 1`: calibrated 1V/oct pitch output
- `Pulse Out 1`: current gate
- `CV Out 2`: unused
- `Pulse Out 2`: unused

## LEDs

- LEDs 1, 3, and 5 indicate CV/MIDI, sequencer, and battery-pull modes
- LED 2 follows the gate
- LED 4 indicates accent
- LED 6 varies with the current note

## Web MIDI Editor

Open the hosted editor:

**[Launch the Fr330hfr33 Web MIDI Editor](https://soveda.github.io/303bass/web_config/Fr330hfr33.html)**

The hosted editor has been tested with the hardware and connects successfully
over Web MIDI.

It controls:

- Oscillator waveform: saw or square
- Filtered-voice distortion: off, RAT-style hard clipping, or Tube
  Screamer-style soft clipping
- Distortion/overdrive amount from 0% to 100%
- Distortion tone from 0% to 100%; RAT mode applies a post-clip high-frequency
  roll-off, while Tube Screamer mode uses a mid-forward active EQ with variable
  treble presence
- Scale: major pentatonic, minor pentatonic, Ionian (major), Lydian,
  ascending melodic minor, or chromatic
- Root note from C to B
- Base octave: C1/MIDI 24, C2/MIDI 36, C3/MIDI 48, or C4/MIDI 60
- Accent probability
- Sequencer range from one to four octaves
- Gate length from 10% to 95% of the measured step
- Glide probability from 0% to 100%
- MIDI input channel from 1 to 16
- Internal tempo from 30 to 240 BPM
- Optional MIDI clock synchronization

Alternatively, serve the editor locally:

```sh
python3 -m http.server 5173 --directory web_config
```

Then open:

```text
http://localhost:5173/Fr330hfr33.html
```

Use Chrome or Edge with Web MIDI and SysEx support. Use a USB-C data cable,
close Serial Monitor and other MIDI applications, select **Fr330hfr33** in the
browser MIDI dialog, and press **Apply**.

Editor settings currently affect the running session only and return to
defaults after reset.

When MIDI clock sync is enabled, MIDI Start resets and starts the clock,
Continue resumes it, Stop pauses it, and each 12 clock ticks advances one
sequencer step. Pulse In 2 remains available as the highest-priority external
clock.
