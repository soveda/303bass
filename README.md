# Fr330hfr33

Fr330hfr33 is a compact acid bass voice for the Music Thing Modular Workshop
Computer. It combines a saw oscillator, resonant four-pole ladder filter,
accented AR envelope, glide, USB MIDI, and a scale-aware random sequencer.

The firmware runs the RP2040 at **192 MHz**. Core 0 is reserved for the 48 kHz
audio interrupt. Core 1 handles USB, MIDI parsing, sequencer timing, control
scaling, and LEDs. The audio callback contains no division, no locks, no waits,
and no software 64-bit multiplication. The complete binary is copied to RAM.

## Controls

- **Main:** filter cutoff
- **X:** resonance
- **Y:** envelope decay and glide time
- **Switch up:** CV/gate or USB MIDI
- **Switch middle:** random sequencer
- **Switch down:** performance mute

At the left of Y, notes are short and glide is nearly immediate. Turning Y
clockwise lengthens the decay and increases glide.

## Connections

- **CV In 1:** 1 V/oct pitch in switch-up mode
- **Pulse In 1:** gate in switch-up mode
- **Pulse In 2:** sequencer clock; recent external clocks override the internal clock
- **Audio Out 1:** enveloped post-filter voice
- **Audio Out 2:** raw saw oscillator
- **CV Out 1:** calibrated pitch output
- **Pulse Out 1:** gate output

MIDI velocity 112–127 produces an accent. USB host/device mode follows the
Workshop Computer's USB power-role detection, using the MIDI implementation
from the local Cizzle project.

## Web editor

Open [web_config/Fr330hfr33.html](web_config/Fr330hfr33.html) in Chrome or
Edge. It configures the sequencer scale, accent probability, octave span, and
internal tempo. Settings currently apply to the running session and reset to
defaults after reboot.

## Build

The build intentionally uses:

- the repository's local `computercard.h`, copied exactly from Cizzle version 0.3.0
- Cizzle's `usb_midi_host.c` and `usb_midi_host_app_driver.c`
- Pico SDK from `~/pico-sdk`

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
```

The flashable result is `build/Fr330hfr33.uf2`.

## Hardware test checklist

1. Confirm clean boot after reset, not only immediately after flashing.
2. Scope the audio ISR and verify worst-case execution remains below 20 µs.
3. Sweep resonance at several cutoff positions and listen for stable,
   controllable self-oscillation.
4. Test USB device MIDI and powered USB-host MIDI separately.
5. Confirm external Pulse In 2 clock suppresses the internal sequencer clock.
6. Check calibrated CV Out 1 against the oscillator pitch across several octaves.
