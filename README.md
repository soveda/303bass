# Fr330hfr33

Work-in-progress firmware for the Fr330hfr33 Music Thing Modular Workshop
Computer program card.

Fr330hfr33 is a compact acid bass instrument with a sawtooth oscillator,
four-pole resonant ladder filter, accented AR envelope, pitch glide, USB MIDI,
and a scale-aware random sequencer.

## Initial Build

Current UF2:

```text
build/Fr330hfr33.uf2
```

SHA-256:

```text
b272f0bcf90db98122baeae7e1072c887bb99ef4fdcdfb4ad99e4345d9753968
```

This initial build compiles successfully but is not yet hardware-qualified.
Filter tuning, self-oscillation behavior, USB host/device operation, CV pitch,
and worst-case interrupt timing still need testing on the card.

## Current Feature Set

- Sawtooth oscillator using a phase accumulator and interpolated lookup table.
- Four-pole fixed-point resonant ladder filter.
- Single AR envelope controlling amplitude and filter modulation.
- Velocity accent with a stronger, shorter envelope response.
- Pitch glide coupled musically to envelope decay.
- External 1V/oct CV and gate control.
- USB MIDI device mode for computers and browsers.
- USB MIDI host mode for class-compliant controllers.
- Random scale-quantized sequencer with internal or external clock.
- Calibrated pitch CV and gate outputs for controlling other modules.
- Initial single-file Web MIDI editor.

## Controls

The three knobs keep the same purpose in every playing mode:

- **Main:** filter cutoff
- **X:** resonance
- **Y:** envelope decay and glide

With Y counter-clockwise, notes are short and pitch changes are nearly
immediate. Turning Y clockwise lengthens the decay and increases slide time.

### Switch Up — CV / MIDI

- `CV In 1`: 1V/oct pitch
- `Pulse In 1`: external gate
- USB MIDI notes take priority while a MIDI note is held
- MIDI velocity 112–127 produces an accent

### Switch Middle — Random Sequencer

- Uses the internal clock when no recent external clock is present
- `Pulse In 2`: external sequencer clock
- Scale, accent probability, octave range, and tempo come from the Web MIDI
  editor

### Switch Down — Performance Mute

Holding the momentary switch down mutes both audio outputs and lowers the gate
output.

## Outputs

- `Audio Out 1`: enveloped post-filter voice
- `Audio Out 2`: raw pre-filter sawtooth oscillator
- `CV Out 1`: calibrated 1V/oct pitch output
- `Pulse Out 1`: current gate
- `CV Out 2`: unused
- `Pulse Out 2`: unused

## LEDs

- LEDs 1, 3, and 5 indicate CV/MIDI, sequencer, and mute modes
- LED 2 follows the gate
- LED 4 indicates accent
- LED 6 varies with the current note

## Web MIDI Editor

The initial editor is:

```text
web_config/Fr330hfr33.html
```

It controls:

- Scale: major pentatonic, minor pentatonic, major, or chromatic
- Accent probability
- Sequencer range from one to four octaves
- Internal tempo from 30 to 240 BPM

Serve it locally:

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

## Real-Time Architecture

The RP2040 runs at **192 MHz** and the complete firmware is copied to RAM.

Core 0 is reserved for the 48 kHz audio interrupt:

- oscillator
- envelope and glide
- ladder filter
- audio, CV, and gate outputs

Core 1 handles:

- USB host/device tasks
- MIDI parsing
- knob and CV scaling
- random sequencer and clock timing
- LED updates
- coherent parameter snapshots sent to core 0

The generated audio callback contains no division helpers, locks, waits,
function calls, or software 64-bit multiplication. Physical scope testing is
still required to confirm the worst-case callback remains below 20 µs.

## Build

Requirements:

- Raspberry Pi Pico SDK at `~/pico-sdk`
- Cizzle repository at `~/Documents/GitHub/Cizzle`
- ARM embedded GCC and CMake

The project uses:

- the local `computercard.h`, copied exactly from Cizzle ComputerCard v0.3.0
- Cizzle's USB MIDI host implementation
- `PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64`
- `pico_set_binary_type(Fr330hfr33 copy_to_ram)`
- a 192 MHz system clock

Build with:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
```

The current build reports:

```text
FLASH: 49960 B
RAM:   57068 B
```

## Hardware Test Checklist

1. Confirm clean boot after reset, not only immediately after flashing.
2. Scope the audio ISR and verify worst-case execution remains below 20 µs.
3. Tune the cutoff curve and resonance range by ear.
4. Check that high resonance self-oscillates without unstable runaway.
5. Test USB MIDI device and powered USB MIDI host modes separately.
6. Confirm external Pulse In 2 clock overrides and releases back to internal
   clock cleanly.
7. Check CV In 1 pitch tracking and calibrated CV Out 1 across several octaves.
8. Exercise Web MIDI settings at their minimum and maximum values.

## Repository Layout

- `Fr330hfr33.cpp`: firmware, DSP, multicore control, MIDI, and sequencer
- `Fr330hfr33_LUT.cpp` / `Fr330hfr33_LUT.h`: sawtooth lookup table
- `computercard.h`: local Cizzle ComputerCard v0.3.0 reference
- `tusb_config.h`: dual-role TinyUSB configuration
- `usb_descriptors.c`: Fr330hfr33 USB MIDI device identity
- `web_config/Fr330hfr33.html`: self-contained Web MIDI editor
- `info.yaml`: Workshop Computer card metadata
- `build/Fr330hfr33.uf2`: current local firmware build

## Status

Version `0.1.0` is an AI-assisted initial implementation created with Adrian
Vos. It is intentionally marked **WIP** until the hardware test checklist has
been completed.
