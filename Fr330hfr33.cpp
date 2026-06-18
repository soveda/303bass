#include "computercard.h"
#include "Fr330hfr33_LUT.h"

#include "hardware/clocks.h"
#include "hardware/sync.h"
#include "pico/multicore.h"
#include "pico/time.h"
#include "tusb.h"
#include "usb_midi_host.h"

#include <stdint.h>

namespace {

constexpr uint32_t SampleRate = 48000;
constexpr int32_t PitchCountsPerVolt = 341;
constexpr uint8_t BaseMidiNote = 36;
constexpr uint32_t ControlPublishDivider = 48;

enum class PlayMode : uint8_t {
    Mute = 0,
    Sequencer = 1,
    CvMidi = 2
};

struct HardwareSnapshot {
    int32_t mainKnob;
    int32_t xKnob;
    int32_t yKnob;
    int32_t cv1;
    int32_t cv2;
    uint32_t pulse1Edges;
    uint32_t pulse2Edges;
    uint8_t switchPosition;
    uint8_t pulse1High;
    uint8_t pulse2High;
    uint8_t reserved;
};

struct AudioParameters {
    uint32_t phaseIncrement;
    int32_t pitchMillivolts;
    int32_t cutoffQ15;
    int32_t resonanceQ12;
    int32_t envelopeDecayQ15;
    int32_t glideQ15;
    int32_t filterEnvelopeQ15;
    uint8_t midiNote;
    uint8_t gate;
    uint8_t accent;
    uint8_t mute;
};

template <typename T>
struct SharedSnapshot {
    T slots[2] = {};
    volatile uint32_t active = 0;
};

SharedSnapshot<HardwareSnapshot> hardwareShared;
SharedSnapshot<AudioParameters> audioShared;

inline int32_t clamp32(int32_t value, int32_t low, int32_t high)
{
    if (value < low)
        return low;
    if (value > high)
        return high;
    return value;
}

template <typename T>
void publishSnapshot(SharedSnapshot<T> &shared, const T &value)
{
    uint32_t next = shared.active ^ 1u;
    shared.slots[next] = value;
    __dmb();
    shared.active = next;
}

template <typename T>
bool readSnapshot(const SharedSnapshot<T> &shared, T &value)
{
    uint32_t before;
    uint32_t after;
    do {
        before = shared.active;
        __dmb();
        value = shared.slots[before];
        __dmb();
        after = shared.active;
    } while (before != after);
    return true;
}

uint32_t pitchUnitsToPhaseIncrement(int32_t units)
{
    static constexpr uint32_t SemitoneRatioQ15[13] = {
        32768, 34716, 36781, 38968, 41285, 43740, 46341,
        49097, 52016, 55109, 58386, 61858, 65536
    };
    static constexpr int32_t UnitsPerOctave = 4096;
    static constexpr uint32_t C2PhaseIncrement = 5852465u;

    units = clamp32(units, -2 * UnitsPerOctave, 7 * UnitsPerOctave);
    int32_t octaves = units / UnitsPerOctave;
    int32_t remainder = units % UnitsPerOctave;
    if (remainder < 0) {
        remainder += UnitsPerOctave;
        --octaves;
    }

    int32_t semitone = (remainder * 12) / UnitsPerOctave;
    int32_t start = (semitone * UnitsPerOctave) / 12;
    int32_t end = ((semitone + 1) * UnitsPerOctave) / 12;
    int32_t fraction = ((remainder - start) << 15) / (end - start);
    uint32_t ratio = SemitoneRatioQ15[semitone] +
        (uint32_t)((((int32_t)SemitoneRatioQ15[semitone + 1] -
        (int32_t)SemitoneRatioQ15[semitone]) * fraction) >> 15);
    uint32_t increment =
        (uint32_t)(((uint64_t)C2PhaseIncrement * ratio) >> 15);

    if (octaves >= 0)
        increment <<= octaves;
    else
        increment >>= -octaves;
    return increment;
}

uint32_t midiNoteToPhaseIncrement(uint8_t note)
{
    int32_t units = ((int32_t)note - BaseMidiNote) * 4096 / 12;
    return pitchUnitsToPhaseIncrement(units);
}

class MidiParser {
public:
    void process(uint8_t byte)
    {
        if (byte == 0xF8u) {
            if (midiClockSync && midiClockRunning) {
                if (++midiClockTicks >= 12u) {
                    midiClockTicks = 0;
                    ++pendingMidiSteps;
                }
            }
            return;
        }
        if (byte == 0xFAu) {
            midiClockTicks = 0;
            pendingMidiSteps = 0;
            midiClockRunning = true;
            return;
        }
        if (byte == 0xFBu) {
            midiClockRunning = true;
            return;
        }
        if (byte == 0xFCu) {
            midiClockRunning = false;
            midiClockTicks = 0;
            pendingMidiSteps = 0;
            return;
        }
        if (byte >= 0xF8u)
            return;

        if (byte == 0xF0u) {
            inSysex = true;
            sysexLength = 0;
            return;
        }
        if (inSysex) {
            if (byte == 0xF7u) {
                inSysex = false;
                applySysex();
            } else if (sysexLength < sizeof(sysex)) {
                sysex[sysexLength++] = byte;
            }
            return;
        }

        if (byte & 0x80u) {
            runningStatus = byte;
            dataCount = 0;
            return;
        }
        if (runningStatus < 0x80u || runningStatus >= 0xF0u)
            return;

        data[dataCount++] = byte;
        uint8_t type = runningStatus & 0xF0u;
        uint8_t needed = (type == 0xC0u || type == 0xD0u) ? 1u : 2u;
        if (dataCount < needed)
            return;
        dataCount = 0;

        if ((runningStatus & 0x0Fu) != midiChannel)
            return;
        if (type == 0x90u && data[1] != 0) {
            note = data[0];
            velocity = data[1];
            gate = true;
            noteTriggered = true;
        } else if ((type == 0x80u || type == 0x90u) && data[0] == note) {
            gate = false;
        }
    }

    bool takeTrigger()
    {
        bool result = noteTriggered;
        noteTriggered = false;
        return result;
    }

    bool takeClockStep()
    {
        if (pendingMidiSteps == 0)
            return false;
        --pendingMidiSteps;
        return true;
    }

    bool clockControlsSequencer() const
    {
        return midiClockSync;
    }

    uint8_t note = BaseMidiNote;
    uint8_t velocity = 100;
    uint8_t midiChannel = 0;
    bool gate = false;

    // Web editor settings. The SysEx payload keeps these in a stable order:
    // scale, accent, span, tempo LSB/MSB, root, gate, glide, channel, MIDI sync.
    uint8_t scale = 0;
    uint8_t accentProbability = 32;
    uint8_t octaveSpan = 2;
    uint16_t tempo = 120;
    uint8_t rootNote = 0;
    uint8_t gateLength = 60;
    uint8_t glideProbability = 35;
    bool midiClockSync = false;

private:
    void applySysex()
    {
        // Educational/non-commercial manufacturer ID, "F303", command 1.
        if (sysexLength != 16 ||
            sysex[0] != 0x7Du ||
            sysex[1] != 'F' ||
            sysex[2] != '3' ||
            sysex[3] != '0' ||
            sysex[4] != '3' ||
            sysex[5] != 0x01u)
            return;

        scale = sysex[6] > 3 ? 0 : sysex[6];
        accentProbability = sysex[7] > 100 ? 100 : sysex[7];
        octaveSpan = clamp32(sysex[8], 1, 4);
        tempo = clamp32(
            (uint16_t)sysex[9] | ((uint16_t)sysex[10] << 7), 30, 240);
        rootNote = sysex[11] > 11 ? 0 : sysex[11];
        gateLength = clamp32(sysex[12], 10, 95);
        glideProbability = sysex[13] > 100 ? 100 : sysex[13];
        midiChannel = sysex[14] & 0x0Fu;
        midiClockSync = (sysex[15] & 0x01u) != 0;
        if (!midiClockSync) {
            midiClockRunning = false;
            midiClockTicks = 0;
            pendingMidiSteps = 0;
        }
    }

    uint8_t runningStatus = 0;
    uint8_t data[2] = {};
    uint8_t dataCount = 0;
    bool noteTriggered = false;
    bool inSysex = false;
    uint8_t sysex[16] = {};
    uint8_t sysexLength = 0;
    bool midiClockRunning = false;
    uint8_t midiClockTicks = 0;
    uint8_t pendingMidiSteps = 0;
};

MidiParser midi;
volatile uint8_t hostMidiDeviceAddress = 0;

} // namespace

class Fr330hfr33 : public ComputerCard {
public:
    bool shouldBootUsbHost()
    {
        return USBPowerState() == USBPowerState_t::DFP;
    }

    void setLed(uint32_t index, uint16_t brightness)
    {
        LedBrightness(index, brightness);
    }

    void ProcessSample() override
    {
        captureHardware();
        receiveAudioParameters();

        if (parameters.mute) {
            envelope = 0;
            AudioOut1(0);
            AudioOut2(0);
            CVOut1Millivolts(parameters.pitchMillivolts);
            PulseOut1(false);
            return;
        }

        if (parameters.gate && !lastGate)
            envelope = 0;
        lastGate = parameters.gate;

        const int32_t envelopeTarget = parameters.gate ? 32767 : 0;
        int32_t envelopeCoefficient = parameters.gate
            ? 6144
            : parameters.envelopeDecayQ15;
        if (!parameters.gate && parameters.accent) {
            envelopeCoefficient += envelopeCoefficient >> 1;
            if (envelopeCoefficient > 32767)
                envelopeCoefficient = 32767;
        }
        envelope += ((envelopeTarget - envelope) * envelopeCoefficient) >> 15;

        if (smoothedIncrement == 0)
            smoothedIncrement = parameters.phaseIncrement;
        int32_t pitchDifference =
            (int32_t)parameters.phaseIncrement - (int32_t)smoothedIncrement;
        // Split the shift around the multiply so this remains a fast 32-bit
        // operation on Cortex-M0+ even at the top of the pitch range.
        int32_t pitchStep =
            ((pitchDifference >> 14) * parameters.glideQ15) >> 1;
        if (pitchStep == 0 && pitchDifference != 0)
            pitchStep = pitchDifference > 0 ? 1 : -1;
        smoothedIncrement = (uint32_t)((int32_t)smoothedIncrement + pitchStep);
        phase += smoothedIncrement;

        uint32_t tableIndex = phase >> 26;
        uint32_t fraction = (phase >> 14) & 0x0FFFu;
        int32_t a = Fr330hfr33SawLut[tableIndex];
        int32_t b = Fr330hfr33SawLut[tableIndex + 1u];
        int32_t saw = a + (((b - a) * (int32_t)fraction) >> 12);

        int32_t dynamicCutoff = parameters.cutoffQ15 +
            ((envelope * parameters.filterEnvelopeQ15) >> 15);
        dynamicCutoff = clamp32(dynamicCutoff, 96, 32100);

        int32_t filtered = processLadder(saw, dynamicCutoff,
            parameters.resonanceQ12);
        int32_t amplitude = envelope;
        if (parameters.accent) {
            amplitude += amplitude >> 2;
            if (amplitude > 32767)
                amplitude = 32767;
        }

        AudioOut1((int16_t)clamp32(
            (filtered * amplitude) >> 15, -2048, 2047));
        AudioOut2((int16_t)saw);
        CVOut1Millivolts(parameters.pitchMillivolts);
        PulseOut1(parameters.gate);
    }

private:
    void captureHardware()
    {
        if (PulseIn1RisingEdge())
            ++pulse1Edges;
        if (PulseIn2RisingEdge())
            ++pulse2Edges;

        if (++publishCounter < ControlPublishDivider)
            return;
        publishCounter = 0;

        HardwareSnapshot snapshot = {};
        snapshot.mainKnob = KnobVal(Knob::Main);
        snapshot.xKnob = KnobVal(Knob::X);
        snapshot.yKnob = KnobVal(Knob::Y);
        snapshot.cv1 = CVIn1();
        snapshot.cv2 = CVIn2();
        snapshot.pulse1Edges = pulse1Edges;
        snapshot.pulse2Edges = pulse2Edges;
        snapshot.switchPosition = (uint8_t)SwitchVal();
        snapshot.pulse1High = PulseIn1();
        snapshot.pulse2High = PulseIn2();
        publishSnapshot(hardwareShared, snapshot);
    }

    void receiveAudioParameters()
    {
        uint32_t active = audioShared.active;
        if (active == audioSlot)
            return;

        __dmb();
        AudioParameters next = audioShared.slots[active];
        __dmb();

        // If core 1 published again during the copy, retain the old coherent
        // parameters and try on the next sample. The ISR never waits.
        if (active == audioShared.active) {
            parameters = next;
            audioSlot = active;
        }
    }

    int32_t processLadder(int32_t input, int32_t cutoffQ15,
        int32_t resonanceQ12)
    {
        // Four zero-delay-style one-pole stages with feedback around the chain.
        // State stays in audio sample units. Saturation at each stage keeps high
        // resonance bounded while retaining the sharp 303-like squelch.
        int32_t driven = input - ((ladder[3] * resonanceQ12) >> 12);
        driven = clamp32(driven, -4095, 4095);

        for (uint32_t i = 0; i < 4; ++i) {
            int32_t delta = driven - ladder[i];
            ladder[i] += (delta * cutoffQ15) >> 15;
            ladder[i] = clamp32(ladder[i], -3072, 3072);
            driven = ladder[i];
        }
        return ladder[3];
    }

    AudioParameters parameters = {
        midiNoteToPhaseIncrement(BaseMidiNote), -2000,
        4096, 0, 128, 32767, 12000, BaseMidiNote, 0, 0, 1
    };
    uint32_t audioSlot = 0;
    uint32_t phase = 0;
    uint32_t smoothedIncrement = 0;
    uint32_t publishCounter = 0;
    uint32_t pulse1Edges = 0;
    uint32_t pulse2Edges = 0;
    int32_t envelope = 0;
    int32_t ladder[4] = {};
    bool lastGate = false;
};

Fr330hfr33 card;

namespace {

uint32_t randomState = 0xF330F333u;

uint32_t nextRandom()
{
    randomState = randomState * 1664525u + 1013904223u;
    return randomState;
}

uint8_t quantizedRandomNote(uint8_t scale, uint8_t octaves, uint8_t root)
{
    static constexpr uint8_t MajorPentatonic[] = {0, 2, 4, 7, 9};
    static constexpr uint8_t MinorPentatonic[] = {0, 3, 5, 7, 10};
    static constexpr uint8_t Major[] = {0, 2, 4, 5, 7, 9, 11};
    uint32_t value = nextRandom();
    uint8_t octave = (uint8_t)((value >> 8) % octaves);
    uint8_t degree;

    if (scale == 1)
        degree = MinorPentatonic[(value >> 16) % 5u];
    else if (scale == 2)
        degree = Major[(value >> 16) % 7u];
    else if (scale == 3)
        degree = (uint8_t)((value >> 16) % 12u);
    else
        degree = MajorPentatonic[(value >> 16) % 5u];
    uint32_t note = BaseMidiNote + root + octave * 12u + degree;
    return (uint8_t)(note > 127u ? 127u : note);
}

void updateLeds(PlayMode mode, bool gate, bool accent, uint8_t note)
{
    card.setLed(0, mode == PlayMode::CvMidi ? 4095 : 0);
    card.setLed(2, mode == PlayMode::Sequencer ? 4095 : 0);
    card.setLed(4, mode == PlayMode::Mute ? 4095 : 0);
    card.setLed(1, gate ? 4095 : 96);
    card.setLed(3, accent ? 4095 : 0);
    card.setLed(5, (uint16_t)(((uint32_t)(note & 0x0Fu) * 4095u) / 15u));
}

void controlWorker()
{
    sleep_ms(100);
    bool hostMode = card.shouldBootUsbHost();
    if (hostMode)
        tuh_init(0);
    else
        tud_init(0);

    HardwareSnapshot hardware = {};
    AudioParameters parameters = {};
    parameters.midiNote = BaseMidiNote;
    parameters.phaseIncrement = midiNoteToPhaseIncrement(parameters.midiNote);
    parameters.pitchMillivolts = -2000;
    parameters.mute = 1;

    uint32_t lastPulse2Edges = 0;
    uint64_t nextInternalStep = time_us_64();
    uint64_t lastExternalClockTime = 0;
    uint64_t lastSequencerStepTime = 0;
    uint64_t sequencerStepPeriod = 250000;
    uint64_t gateOffTime = 0;
    bool sequenceGate = false;
    bool sequenceAccent = false;
    bool sequenceGlide = false;
    uint8_t sequenceNote = BaseMidiNote;
    uint32_t ledDivider = 0;

    while (true) {
        if (hostMode) {
            tuh_task();
        } else {
            tud_task();
            uint8_t bytes[64];
            uint32_t count = tud_midi_stream_read(bytes, sizeof(bytes));
            for (uint32_t i = 0; i < count; ++i)
                midi.process(bytes[i]);
        }

        readSnapshot(hardwareShared, hardware);
        PlayMode mode = hardware.switchPosition == (uint8_t)ComputerCard::Switch::Up
            ? PlayMode::CvMidi
            : hardware.switchPosition == (uint8_t)ComputerCard::Switch::Middle
                ? PlayMode::Sequencer
                : PlayMode::Mute;

        int32_t mainKnob = clamp32(hardware.mainKnob, 0, 4095);
        int32_t xKnob = clamp32(hardware.xKnob, 0, 4095);
        int32_t yKnob = clamp32(hardware.yKnob, 0, 4095);

        // Squared cutoff mapping gives the useful low-frequency area more room.
        parameters.cutoffQ15 = 120 +
            (int32_t)(((int64_t)mainKnob * mainKnob * 31000) >> 24);
        parameters.resonanceQ12 = (xKnob * 17400) / 4095;

        // One musically coupled control: clockwise gives a longer envelope and
        // more glide. Coefficients are calculated here, never in the ISR.
        parameters.envelopeDecayQ15 =
            24 + ((4095 - yKnob) * 900) / 4095;
        int32_t physicalGlideQ15 =
            32767 - (yKnob * 32500) / 4095;
        if (physicalGlideQ15 < 48)
            physicalGlideQ15 = 48;
        parameters.filterEnvelopeQ15 = 9000 + (xKnob * 15000) / 4095;

        uint64_t now = time_us_64();
        if (mode == PlayMode::Sequencer) {
            bool externalClock = hardware.pulse2Edges != lastPulse2Edges;
            if (externalClock) {
                lastPulse2Edges = hardware.pulse2Edges;
                lastExternalClockTime = now;
            }

            uint32_t bpm = clamp32(midi.tempo, 30, 240);
            uint64_t stepPeriod = 60000000ull / ((uint64_t)bpm * 2ull);
            bool externalClockActive =
                lastExternalClockTime != 0 &&
                now - lastExternalClockTime < 2000000ull;
            bool receivedMidiClockStep = midi.takeClockStep();
            bool midiClockStep =
                !externalClockActive && receivedMidiClockStep;
            bool midiClockControls = midi.clockControlsSequencer();
            bool internalClock = !externalClockActive && !midiClockControls &&
                now >= nextInternalStep;
            if (internalClock)
                nextInternalStep = now + stepPeriod;

            if (externalClock || midiClockStep || internalClock) {
                if (lastSequencerStepTime != 0) {
                    uint64_t measured = now - lastSequencerStepTime;
                    if (measured >= 10000ull && measured <= 2000000ull)
                        sequencerStepPeriod = measured;
                } else {
                    sequencerStepPeriod = stepPeriod;
                }
                lastSequencerStepTime = now;
                sequenceNote = quantizedRandomNote(
                    midi.scale, clamp32(midi.octaveSpan, 1, 4), midi.rootNote);
                sequenceAccent =
                    (nextRandom() % 100u) < midi.accentProbability;
                sequenceGlide =
                    (nextRandom() % 100u) < midi.glideProbability;
                sequenceGate = true;
                gateOffTime = now +
                    (sequencerStepPeriod * midi.gateLength) / 100u;
            }
            if (sequenceGate && now >= gateOffTime)
                sequenceGate = false;

            parameters.midiNote = sequenceNote;
            parameters.phaseIncrement = midiNoteToPhaseIncrement(sequenceNote);
            parameters.pitchMillivolts =
                ((int32_t)sequenceNote - 60) * 1000 / 12;
            parameters.gate = sequenceGate;
            parameters.accent = sequenceAccent;
            parameters.glideQ15 =
                sequenceGlide ? physicalGlideQ15 : 32767;
            parameters.mute = 0;
        } else if (mode == PlayMode::CvMidi) {
            parameters.glideQ15 = physicalGlideQ15;
            bool midiTriggered = midi.takeTrigger();
            (void)midiTriggered;
            if (midi.gate) {
                parameters.midiNote = midi.note;
                parameters.phaseIncrement = midiNoteToPhaseIncrement(midi.note);
                parameters.pitchMillivolts =
                    ((int32_t)midi.note - 60) * 1000 / 12;
                parameters.gate = 1;
                parameters.accent = midi.velocity >= 112;
            } else {
                int32_t pitchUnits =
                    (hardware.cv1 * 4096) / PitchCountsPerVolt;
                int32_t semitones = (pitchUnits * 12) / 4096;
                int32_t note = clamp32(BaseMidiNote + semitones, 0, 127);
                parameters.midiNote = (uint8_t)note;
                parameters.phaseIncrement =
                    pitchUnitsToPhaseIncrement(pitchUnits);
                parameters.pitchMillivolts =
                    (hardware.cv1 * 1000) / PitchCountsPerVolt;
                parameters.gate = hardware.pulse1High;
                parameters.accent = 0;
            }
            parameters.mute = 0;
        } else {
            parameters.gate = 0;
            parameters.accent = 0;
            parameters.mute = 1;
        }

        publishSnapshot(audioShared, parameters);

        if (++ledDivider >= 20) {
            ledDivider = 0;
            updateLeds(mode, parameters.gate, parameters.accent,
                parameters.midiNote);
        }
        sleep_us(500);
    }
}

} // namespace

extern "C" void tuh_midi_mount_cb(
    uint8_t dev_addr, uint8_t in_ep, uint8_t out_ep,
    uint8_t num_cables_rx, uint16_t num_cables_tx)
{
    (void)in_ep;
    (void)out_ep;
    (void)num_cables_rx;
    (void)num_cables_tx;
    if (hostMidiDeviceAddress == 0)
        hostMidiDeviceAddress = dev_addr;
}

extern "C" void tuh_midi_umount_cb(uint8_t dev_addr, uint8_t instance)
{
    (void)instance;
    if (dev_addr == hostMidiDeviceAddress)
        hostMidiDeviceAddress = 0;
}

extern "C" void tuh_midi_rx_cb(uint8_t dev_addr, uint32_t num_packets)
{
    if (dev_addr != hostMidiDeviceAddress || num_packets == 0)
        return;

    uint8_t cable = 0;
    uint8_t bytes[128];
    while (true) {
        uint32_t count =
            tuh_midi_stream_read(dev_addr, &cable, bytes, sizeof(bytes));
        if (count == 0)
            break;
        for (uint32_t i = 0; i < count; ++i)
            midi.process(bytes[i]);
    }
}

extern "C" void tuh_midi_tx_cb(uint8_t dev_addr)
{
    (void)dev_addr;
}

int main()
{
#if defined(FR330HFR33_OVERCLOCK_KHZ) && FR330HFR33_OVERCLOCK_KHZ
    set_sys_clock_khz(FR330HFR33_OVERCLOCK_KHZ, true);
#endif
    multicore_launch_core1(controlWorker);
    card.Run();
}
