# Fr330hfr33 experimental area

This folder contains the implementation that was hardware-tested and promoted
to main as version `0.9.1`. It remains independently buildable as a safe place
for evaluating later changes before they are promoted.

Only changes that pass hardware testing here should be added to main, and only
when promotion is explicitly requested.

## Implemented in 0.9.1

- Saw and square oscillators
- RAT-style and Tube Screamer-style distortion with amount and tone
- Switchable 24 dB four-pole and 18 dB three-pole diode-style filters
- Full-range cutoff response and host-mode CC1 cutoff with knob pickup
- Stronger accent with capacitor-memory sweep
- Click-reduced note articulation and reliable USB disconnect recovery
- Separate 16-step note, octave, accent, gate, and tie lanes
- Scale-aware lane randomization and Acidify Pattern
- Initial Step, forward, reverse, and pendulum playback
- Four persistent pattern slots and persistent editor settings
- Two-page Sound/Sequencer Web MIDI editor
- Active pattern slot shown by one to four pulses on LED 3

## Possible future changes

- Per-step short, medium, or full gate choices instead of only percentages
- Sequencer swing
- Incoming chord analysis to derive a root and permitted note pool

Aftertouch modulation and timing jitter are intentionally excluded.

## Building the experimental firmware

Prerequisites:

- CMake
- Arm GNU toolchain (`arm-none-eabi-gcc`)
- Raspberry Pi Pico SDK

```sh
export PICO_SDK_PATH=/path/to/pico-sdk
./build_experimental.sh
```

If `pico-sdk` and `303bass` are sibling folders, setting `PICO_SDK_PATH` is
optional. The helper uses a clean temporary build directory, places the UF2 in
`experimental_waveform_distortion/uf2`, and removes the build directory when
finished.

Protocol details are in [`protocol.md`](protocol.md).
