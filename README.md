# Fr330hfr33

Release-candidate firmware for the Fr330hfr33 Music Thing Modular Workshop
Computer program card.

Fr330hfr33 is a compact acid bass instrument with selectable sawtooth or square
oscillator, diode-style resonant ladder filter, accented AR envelope, pitch
glide, USB MIDI, distortion, and a scale-aware random sequencer.

## Release Candidate

Hardware-tested release-candidate UF2:

```text
uf2/Fr330hfr33-v0.9.0-rc1.uf2
```

SHA-256:

```text
b84ae610403d7fd88a598090020a2009700b640e73d3a876e0b78462764903f1
```

Version `0.9.0-rc1` was promoted after the full available hardware test pass:
boot and sustained stability, pitch, filter and resonance behavior, envelope,
accent, manual and MIDI legato glide, internal/external/MIDI clocks, Web MIDI
configuration, outputs, generative sequencing, and battery-pull behavior.


## Current Feature Set

- Web MIDI-selectable sawtooth or raw phase-derived square oscillator.
- Web MIDI-selectable filtered-voice distortion: off, RAT-style hard clipping,
  or Tube Screamer-style soft clipping, with 0–100% drive and tone controls.
- Fixed-point diode-style TPT ladder filter with algebraically solved feedback.
- Web MIDI-selectable 24 dB/octave four-pole or 18 dB/octave three-pole
  response, each with its own feedback and resonance calibration.
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

- Fully counter-clockwise produces a dark, rounded bass tone without the
  fourth-pole roll-off feeling quite as abrupt.
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
- Self-oscillation or pinging appears decisively in the final part of travel,
  while the lower range remains useful for broad resonance.
- The ladder stages use a broad diode-pair saturation knee, so high resonance
  builds into a rounder sine instead of being held down by early stage clipping.
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
The VCA has an approximately 20 ms minimum release to suppress note-off
clicks. This floor affects only amplitude; the filter contour retains its
faster decay range.
Glide only applies when selected by Pulse In 2, MIDI legato, or sequencer glide
probability.

### Switch Up — CV / MIDI

- `CV In 1`: 1V/oct pitch
- `Pulse In 1`: external gate
- `Pulse In 2` high: manually enables glide for CV and MIDI pitch changes
- USB MIDI notes take priority while a MIDI note is held
- MIDI velocity 112–127 produces an accent

Accent adds a stronger filter-contour push and a 62.5% level lift relative to
normal notes. The firmware reserves output headroom and adds a short
contour-shaped bright component. That bright blend reaches 37.5% at the start
of the contour, keeping accented notes prominent near maximum cutoff or with
distortion active.

VCA retriggers attack from the current envelope level instead of resetting to
literal zero, reducing clicks when a new articulation arrives before the
previous note has completely released.

USB MIDI device disconnect watches TinyUSB's active/suspended bus state as well
as its callbacks. Host frame traffic stopping clears held MIDI/parser state and
latches the filtered voice gate low until USB traffic resumes or a fresh
physical gate edge takes control.

### Square-Wave Distortion Character

Hardware testing confirms that RAT-style hard clipping and Tube Screamer-style
soft clipping sound clearly different with the saw oscillator, but can sound
similar with the square oscillator. The raw square is already a two-level,
rail-shaped waveform, so symmetrical clipping changes its level more than its
shape. Both distortion tone controls remain active with either waveform.

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
- Gate length and per-transition legato probability are configurable
- Optional MIDI clock sync advances the sequence every 12 MIDI clock ticks

The generator is not a fixed looping pattern. It remembers the last few scale
positions, avoids immediate repeats, generally walks to nearby notes, and
occasionally makes a wider or octave jump. Changing scale, root, base octave,
or range resets that melodic history.

Glide probability prepares transitions one step ahead. A selected transition
keeps the gate continuously high: most move to a new pitch and glide without
retriggering either envelope, while one quarter repeat the current pitch as a
true tied note. Non-legato steps close the gate at the configured gate length.

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
parallel-filter cancellation. Four-pole mode uses two normal all-pass stages;
three-pole mode uses one normal stage plus a unity-magnitude fractional phase
stage.

The 18 dB mode emphasizes the difference between its third-pole output and the
continuously tracked fourth pole. It retains a third-order far-slope while
using a higher cutoff calibration to make the contrast with 24 dB mode easier
to hear. LED 6 stays fully lit whenever 18 dB mode is active.

## Outputs

- `Audio Out 1`: enveloped post-filter voice
- `Audio Out 2`: phase-aligned raw selected oscillator
- `CV Out 1`: calibrated 1V/oct pitch output
- `Pulse Out 1`: current gate
- `CV Out 2`: unused
- `Pulse Out 2`: unused

## LEDs

- LEDs 1, 3, and 5 indicate CV/MIDI, sequencer, and battery-pull modes
- LED 2 follows the gate
- LED 4 indicates accent
- LED 6 varies with the current note, or stays fully lit in 18 dB filter mode

## Web MIDI Editor

Open the hosted editor:

**[Launch the Fr330hfr33 Web MIDI Editor](https://soveda.github.io/303bass/web_config/Fr330hfr33.html)**

The hosted editor has been tested with the hardware and connects successfully
over Web MIDI.

It controls:

- Oscillator waveform: saw or square
- Diode filter slope: 24 dB/octave or 18 dB/octave
- Filtered-voice distortion: off, RAT-style hard clipping, or Tube
  Screamer-style soft clipping
- Distortion/overdrive amount from 0% to 100%
- Distortion tone from 0% to 100%
- Scale: major pentatonic, minor pentatonic, Ionian (major), Lydian,
  ascending melodic minor, or chromatic
- Root note from C to B
- Base octave: C1/MIDI 24, C2/MIDI 36, C3/MIDI 48, or C4/MIDI 60
- Accent probability
- Sequencer range from one to four octaves
- Gate length from 10% to 95% of the measured step
- Legato transition probability from 0% to 100%; selected transitions become
  either true repeated-note ties or slides to a new pitch
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

## Proposed Possible Future Changes

- Separate 16-step lanes for note, accent, gate length, octave, and tie state.
- Selective randomization of individual sequence lanes.
- Variable pattern length plus pattern rotation/offset.
- Forward, backward, and pendulum playback.
- Per-step short, medium, or full gate lengths.
- Stored pattern slots, sequencer swing, and optional timing jitter.
- An acidness control for root repetition, intervals, ties, and accents.
- Incoming chord analysis to derive the generator's permitted note pool.
- Velocity or aftertouch modulation of sequence density, cutoff, or resonance.
