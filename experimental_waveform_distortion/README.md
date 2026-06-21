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
diode-style resonant ladder filter, accented AR envelope, pitch glide, USB
MIDI, and a scale-aware random sequencer.

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
- Fixed-point diode-style TPT ladder filter with algebraically solved feedback.
- Web MIDI-selectable 24 dB/octave four-pole or 18 dB/octave three-pole
  response, each with its own feedback and resonance calibration.
- Single AR envelope controlling amplitude and filter modulation.
- Velocity accent with a stronger, shorter envelope response.
- Pitch glide coupled musically to envelope decay.
- External 1V/oct CV and gate control.
- USB MIDI device mode for computers and browsers.
- USB MIDI host mode for class-compliant controllers.
- Host-mode MIDI CC1 cutoff control with physical-knob pickup.
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
The experimental VCA has an approximately 20 ms minimum release to suppress
note-off clicks. This floor affects only amplitude; the filter contour retains
its faster decay range.
Glide only applies when selected by Pulse In 2, MIDI legato, or sequencer glide
probability.

### Switch Up — CV / MIDI

- `CV In 1`: 1V/oct pitch
- `Pulse In 1`: external gate
- `Pulse In 2` high: manually enables glide for CV and MIDI pitch changes
- USB MIDI notes take priority while a MIDI note is held
- MIDI velocity 112–127 produces an accent

Accent adds a stronger filter-contour push and a 62.5% level lift relative to
normal notes. The experimental build reserves output headroom and adds a short
contour-shaped bright component. That bright blend has also been increased
from 25% to 37.5% at the start of the contour, so accented notes should be more
prominent when cutoff is already near maximum or distortion is active.

This experimental revision also models the original dual-gang resonance/accent
sweep interaction. Each accented articulation adds charge to a virtual
capacitor rather than resetting a fixed accent envelope. The charge drains
over a few hundred milliseconds, so closely spaced accented notes retain some
of the previous voltage and drive each following filter sweep higher.

Resonance acts like the second section of the analogue pot: anti-clockwise
favours the immediate filter-envelope hit, while clockwise increasingly
favours the smoother capacitor response. With high resonance, one isolated
accent should make a rounded “wow”; a fast group of accents should climb over
the first few notes, then fall back after a pause. The capacitor memory can
briefly brighten a following non-accented note, as the real circuit does.

VCA retriggers attack from the current envelope level instead of resetting to
literal zero, reducing clicks when a new articulation arrives before the
previous note has completely released.

USB MIDI device disconnect watches TinyUSB's active/suspended bus state as well
as its callbacks. The RP2040 USB driver deliberately forces VBUS present, so
the ordinary mounted and physical-connection flags can remain true after the
DAW cable is removed. Host frame traffic stopping puts the device bus into
suspend and now clears held MIDI/parser state, latching the filtered voice gate
low until USB traffic resumes or a fresh physical gate edge takes control.

### Host-mode CC1 cutoff and pickup

When the card boots as a USB host, MIDI CC1 controls filter cutoff across the
full Main-knob range. CC1 is ignored in USB device mode.

The cutoff response is calibrated so both Main and CC1 continue opening the
filter audibly throughout the clockwise half of their travel, while preserving
the fully open endpoint.

After CC1 takes control, moving Main does nothing until the knob reaches and
crosses the current CC1 position. Main then picks up the cutoff without a jump
and resumes normal control. A subsequent CC1 message takes over again.
Disconnecting the hosted MIDI controller releases the CC1 override.

### Square-Wave Distortion Character

Hardware testing confirms that the RAT-style hard clipping and Tube
Screamer-style soft clipping are clearly different with the saw oscillator,
but can sound very similar with the square oscillator. This is expected rather
than a mode-selection fault.

The raw square has only two amplitude levels and is already effectively
rail-shaped before it reaches the filter and distortion. Additional
symmetrical clipping therefore changes its level much more than its waveform
shape. The saw contains a continuous range of intermediate amplitudes, so the
hard and soft clipping curves reshape it differently and produce a much more
obvious contrast. The distortion tone controls still operate in square-wave
mode.

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

The experimental editor can replace the generator with an editable pattern of
1–16 steps. Every step has independent lanes for chromatic note, octave,
accent, gate percentage, and tie-to-next. Root transposes the note lane and
Base Octave anchors the octave lane. A tie into a changed note creates a slide
without retriggering; a tie into the same note creates a true repeated-note
tie. Disabling the pattern returns immediately to the generative sequencer.

The editor also provides a separate randomize button for each lane, allowing
notes to be changed while accents, gates, octaves, or ties remain untouched.
Random Notes chooses pseudorandom pitch classes only from the currently
selected scale and avoids immediate repeats where possible. Root transposition
is applied later by the card.
Edits play from RAM immediately. Save Slot writes them to card flash; unsaved
edits are replaced by the active saved slot after reset.

Initial Step selects which stored lane column plays first without moving or
rewriting any lane data. It wraps within the selected pattern length: for
example, length 5 with Initial Step 3 plays stored steps 3, 4, 5, 1, 2.

Reverse Playback runs from Initial Step backward through the active pattern.
For example, length 5 with Initial Step 3 plays 3, 2, 1, 5, 4. Step ties follow
the playback direction, so a tie on step 3 carries into step 2 when reversed.

Pendulum Playback travels to the far end of the active pattern and then
reverses, without repeating the turnaround step. The Reverse checkbox chooses
its initial direction. Ties follow the actual travel direction at both
turnarounds.

Four pattern slots are stored in card flash. Save Slot explicitly writes the
current lanes, length, Initial Step, and playback mode to the selected slot;
Load Slot recalls it immediately. The active slot and all general editor
settings also restore after power cycling. Flash is checksummed, versioned,
and only rewritten when data has changed, following Cizzle’s persistence
approach. The editor keeps a browser mirror so recalled slots can repopulate
the visible controls.

Glide probability now prepares transitions one step ahead. A selected
transition keeps the gate continuously high: most move to a new pitch and
glide without retriggering either envelope, while one quarter repeat the
current pitch as a true tied note. Non-legato steps still close the gate at the
configured gate length and retrigger normally on the next step.

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
stage, avoiding the high-frequency cancellation caused by averaging phase
paths.

The 18 dB mode emphasizes the difference between its third-pole output and the
continuously tracked fourth pole. It retains a third-order far-slope while
using a higher cutoff calibration to make the contrast with 24 dB mode easier
to hear on bass waveforms. LED 6 stays fully lit whenever 18 dB mode is
actually active, providing a hardware confirmation that the setting arrived.

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

The public hosted editor currently targets the main release and does not send
the experimental waveform, distortion, or filter-mode fields. Use the
experimental folder's local editor for this build.

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
- Legato transition probability from 0% to 100%; selected transitions become
  either true repeated-note ties or slides to a new pitch
- MIDI input channel from 1 to 16
- Internal tempo from 30 to 240 BPM
- Optional MIDI clock synchronization

Serve the experimental editor locally:

```sh
python3 -m http.server 5173 --directory web_config
```

Then open:

```text
http://localhost:5173/Fr330hfr33.html
```

The editor is divided into Sound and Sequencer pages. Connect MIDI, Apply, and
the connection status stay at the top of both pages. The editor remembers the
last page used, and Apply always sends the complete sound and sequencer state.

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

Ideas worth considering after the current experimental firmware has passed
hardware testing:

- Per-step short, medium, or full gate lengths instead of one global gate
  percentage.
- Sequencer swing.
- Incoming chord analysis that derives a root and permitted note pool for the
  generator.

### Acidness Control

Acidness is a coordinated musical-density control rather than another audio
effect. In generator mode, low values favour roots and nearby scale movement,
longer gates, fewer accents, and little legato. Raising it progressively
introduces wider scale-degree movement, octave jumps, shorter punchier gates,
more accents, and more tied slides.

For editable patterns, **Acidify Pattern** uses the current Acidness value and
selected scale to rewrite all five lanes into a fixed, editable pattern. Low
values produce restrained root-heavy lines; high values produce restless
intervals, octave movement, clustered accents, short gates, and frequent ties.
The result does not keep changing during playback: it remains stable until
Acidify Pattern or another lane-randomisation button is pressed again.

Acidness is stored with the persistent global settings.
