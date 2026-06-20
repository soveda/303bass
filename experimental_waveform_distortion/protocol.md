# Fr330hfr33 Experimental Web MIDI Protocol

This records the SysEx protocol shared by the experimental firmware and Web
MIDI editor.

## Message layout

All values are MIDI-safe 7-bit bytes.

```text
F0 7D 46 33 30 33 01
   scale
   accent_probability
   octave_span
   tempo_lsb
   tempo_msb
   root_note
   gate_length
   glide_probability
   midi_channel
   midi_clock_sync
   base_midi_note
   waveform
   distortion_mode
   distortion_amount
   distortion_tone
   filter_poles
F7
```

The complete message is 24 bytes including `F0` and `F7`. The firmware stores
the 22 bytes between them in `sysex[]`.

## Header

| Wire bytes | Value | Meaning |
| --- | --- | --- |
| 0 | `F0` | SysEx start |
| 1 | `7D` | Educational/non-commercial manufacturer ID |
| 2–5 | `46 33 30 33` | ASCII `F303` |
| 6 | `01` | Configuration command |

## Configuration fields

The SysEx offsets match the firmware's internal `sysex[]` array. Wire offsets
include the leading `F0`.

| SysEx offset | Wire offset | Field | Values |
| ---: | ---: | --- | --- |
| 6 | 7 | Scale | `0` major pentatonic, `1` minor pentatonic, `2` Ionian, `3` Lydian, `4` melodic minor, `5` chromatic |
| 7 | 8 | Accent probability | `0–100` percent |
| 8 | 9 | Sequencer octave span | `1–4` |
| 9 | 10 | Tempo low 7 bits | Combined with the next byte |
| 10 | 11 | Tempo high 7 bits | Result clamped to `30–240` BPM |
| 11 | 12 | Root note | `0–11`, C through B |
| 12 | 13 | Gate length | `10–95` percent |
| 13 | 14 | Sequencer legato probability | `0–100` percent; selected transitions keep the gate high and become slides or repeated-note ties |
| 14 | 15 | MIDI input channel | `0–15`, representing channels 1–16 |
| 15 | 16 | MIDI clock sync | Bit 0: `0` disabled, `1` enabled |
| 16 | 17 | Base MIDI note | `24`, `36`, `48`, or `60` |
| 17 | 18 | Waveform | `0` saw, `1` square |
| 18 | 19 | Distortion mode | `0` off, `1` RAT-style, `2` Tube Screamer-style |
| 19 | 20 | Distortion amount | `0–100`; zero bypasses distortion and tone |
| 20 | 21 | Distortion tone | `0–100`, dark to bright |
| 21 | 22 | Diode filter poles | `4` for 24 dB/octave, `3` for 18 dB/octave |

Wire byte 23 is `F7`, the SysEx terminator.

## Distortion behavior

Distortion applies only to Audio Out 1's filtered voice. Audio Out 2 remains
the phase-aligned raw selected oscillator.

- RAT-style mode uses variable pre-gain, symmetrical hard clipping, and a
  post-clip low-pass tone control.
- Tube Screamer-style mode uses variable pre-gain, a soft clipping knee, fixed
  mid emphasis, and variable treble presence.
- Amount zero bypasses clipping and distortion tone processing.

These are lightweight character approximations, not circuit-level pedal
models.

## Backward compatibility

The firmware accepts these internal SysEx lengths:

| Length | Last field present |
| ---: | --- |
| 17 | Base MIDI note; release-candidate editor |
| 19 | Distortion mode |
| 20 | Distortion amount |
| 21 | Distortion tone; previous experimental editor |
| 22 | Diode filter pole count; current experimental editor |

Missing experimental fields retain their current/default values:

- Waveform: saw
- Distortion: off
- Amount: 50%
- Tone: 50%
- Diode filter: four pole / 24 dB per octave

Invalid values are clamped or replaced with safe defaults.

## Maintenance notes

Command `01` retains the release-candidate configuration prefix. If a future
change alters existing field meanings or ordering, use a new command byte
rather than reinterpreting command `01`.

When adding fields:

1. Append them after the current payload.
2. Increase the firmware SysEx buffer if necessary.
3. Accept the previous payload length where practical.
4. Update the editor, firmware parser, and this document together.
