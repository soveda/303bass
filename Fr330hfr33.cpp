#include "computercard.h"
#include "Fr330hfr33_LUT.h"

#include "hardware/clocks.h"
#include "hardware/sync.h"
#include "pico/multicore.h"
#include "pico/time.h"
#include "tusb.h"
#include "usb_midi_host.h"

#include <array>
#include <stdint.h>

namespace {

constexpr uint32_t SampleRate = 48000;
constexpr int32_t PitchCountsPerVolt = 341;
constexpr uint8_t BaseMidiNote = 36;
constexpr uint32_t ControlPublishDivider = 48;
constexpr int32_t PitchUnitsPerOctave = 4096;
constexpr int32_t MinPitchUnits = -2 * PitchUnitsPerOctave;
constexpr int32_t MaxPitchUnits = 8 * PitchUnitsPerOctave;

constexpr std::array<uint16_t, 257> makeReciprocalQ15Table()
{
    std::array<uint16_t, 257> table = {};
    for (uint32_t i = 0; i < table.size(); ++i) {
        uint32_t denominatorQ15 = 32768u + i * 512u;
        table[i] = (uint16_t)((1u << 30) / denominatorQ15);
    }
    return table;
}

constexpr auto ReciprocalQ15Table = makeReciprocalQ15Table();

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
    uint32_t envelopeTrigger;
    int32_t pitchMillivolts;
    int32_t cutoffQ15;
    int32_t resonanceQ12;
    int32_t envelopeDecayQ15;
    int32_t glideQ15;
    int32_t filterEnvelopeQ15;
    uint8_t midiNote;
    uint8_t gate;
    uint8_t accent;
    uint8_t powerCut;
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
    static constexpr uint32_t C2PhaseIncrement = 5852465u;

    units = clamp32(units, MinPitchUnits, MaxPitchUnits);
    int32_t octaves = units / PitchUnitsPerOctave;
    int32_t remainder = units % PitchUnitsPerOctave;
    if (remainder < 0) {
        remainder += PitchUnitsPerOctave;
        --octaves;
    }

    int32_t semitone = (remainder * 12) / PitchUnitsPerOctave;
    int32_t start = (semitone * PitchUnitsPerOctave) / 12;
    int32_t end = ((semitone + 1) * PitchUnitsPerOctave) / 12;
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
            noteOn(data[0], data[1]);
        } else if (type == 0x80u || type == 0x90u) {
            noteOff(data[0]);
        }
    }

    void allNotesOff()
    {
        for (uint32_t i = 0; i < 128; ++i)
            heldNotes[i] = false;
        gate = false;
        legatoSlide = false;
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

    bool shouldGlide() const
    {
        return legatoSlide;
    }

    uint32_t triggerSerial() const
    {
        return articulationSerial;
    }

    uint8_t note = BaseMidiNote;
    uint8_t velocity = 100;
    uint8_t midiChannel = 0;
    bool gate = false;

    // Web editor settings. The SysEx payload keeps these in a stable order:
    // scale, accent, span, tempo LSB/MSB, root, gate, glide, channel,
    // MIDI sync, and base octave.
    uint8_t scale = 0;
    uint8_t accentProbability = 32;
    uint8_t octaveSpan = 2;
    uint16_t tempo = 120;
    uint8_t rootNote = 0;
    uint8_t baseMidiNote = BaseMidiNote;
    uint8_t gateLength = 60;
    uint8_t glideProbability = 35;
    bool midiClockSync = false;

private:
    void noteOn(uint8_t nextNote, uint8_t nextVelocity)
    {
        bool hadHeldNote = gate;
        heldNotes[nextNote] = true;
        heldOrder[nextNote] = ++noteOrder;
        heldVelocity[nextNote] = nextVelocity;
        note = nextNote;
        velocity = nextVelocity;
        gate = true;
        legatoSlide = hadHeldNote;
        if (!hadHeldNote)
            ++articulationSerial;
    }

    void noteOff(uint8_t releasedNote)
    {
        heldNotes[releasedNote] = false;
        if (releasedNote != note)
            return;

        bool found = false;
        uint32_t newestOrder = 0;
        uint8_t newestNote = note;
        for (uint32_t i = 0; i < 128; ++i) {
            if (heldNotes[i] && (!found || heldOrder[i] > newestOrder)) {
                found = true;
                newestOrder = heldOrder[i];
                newestNote = (uint8_t)i;
            }
        }

        if (!found) {
            gate = false;
            legatoSlide = false;
            return;
        }

        note = newestNote;
        velocity = heldVelocity[newestNote];
        gate = true;
        legatoSlide = true;
    }

    void applySysex()
    {
        // Educational/non-commercial manufacturer ID, "F303", command 1.
        if (sysexLength != 17 ||
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
        uint8_t nextMidiChannel = sysex[14] & 0x0Fu;
        if (nextMidiChannel != midiChannel)
            allNotesOff();
        midiChannel = nextMidiChannel;
        midiClockSync = (sysex[15] & 0x01u) != 0;
        switch (sysex[16]) {
        case 24:
        case 36:
        case 48:
        case 60:
            baseMidiNote = sysex[16];
            break;
        default:
            baseMidiNote = BaseMidiNote;
            break;
        }
        if (!midiClockSync) {
            midiClockRunning = false;
            midiClockTicks = 0;
            pendingMidiSteps = 0;
        }
    }

    uint8_t runningStatus = 0;
    uint8_t data[2] = {};
    uint8_t dataCount = 0;
    bool inSysex = false;
    uint8_t sysex[20] = {};
    uint8_t sysexLength = 0;
    bool midiClockRunning = false;
    uint8_t midiClockTicks = 0;
    uint8_t pendingMidiSteps = 0;
    bool heldNotes[128] = {};
    uint8_t heldVelocity[128] = {};
    uint32_t heldOrder[128] = {};
    uint32_t noteOrder = 0;
    uint32_t articulationSerial = 0;
    bool legatoSlide = false;
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

        // Holding the switch down behaves like pulling the batteries from a
        // running 303. The virtual supply falls slowly enough to expose a
        // short pitch/filter collapse instead of producing a plain digital mute.
        int32_t supplyTarget = parameters.powerCut ? 0 : 32767;
        int32_t supplyDifference = supplyTarget - supplyQ15;
        int32_t supplyStep = parameters.powerCut
            ? (supplyDifference >> 13)
            : (supplyDifference >> 8);
        if (supplyStep == 0 && supplyDifference != 0)
            supplyStep = supplyDifference > 0 ? 1 : -1;
        supplyQ15 += supplyStep;

        bool poweredGate = parameters.gate && !parameters.powerCut;
        bool explicitTrigger = parameters.envelopeTrigger != lastEnvelopeTrigger;
        if ((poweredGate && !lastGate) || explicitTrigger)
            envelope = 0;
        lastEnvelopeTrigger = parameters.envelopeTrigger;
        lastGate = poweredGate;

        const int32_t envelopeTarget = poweredGate ? 32767 : 0;
        int32_t envelopeCoefficient = poweredGate
            ? 6144
            : parameters.envelopeDecayQ15;
        if (!poweredGate && parameters.accent) {
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
        // As the supply disappears the oscillator loses roughly five
        // semitones before its output vanishes. Splitting the multiply avoids
        // a 64-bit helper in the interrupt.
        uint32_t pitchDroop = smoothedIncrement >> 2;
        uint32_t poweredIncrement =
            smoothedIncrement - pitchDroop +
            (uint32_t)(((pitchDroop >> 15) * supplyQ15));
        phase += poweredIncrement;

        uint32_t tableIndex = phase >> 26;
        uint32_t fraction = (phase >> 14) & 0x0FFFu;
        int32_t a = Fr330hfr33SawLut[tableIndex];
        int32_t b = Fr330hfr33SawLut[tableIndex + 1u];
        int32_t saw = a + (((b - a) * (int32_t)fraction) >> 12);

        int32_t dynamicCutoff = parameters.cutoffQ15 +
            ((envelope * parameters.filterEnvelopeQ15) >> 15);
        int32_t supplyAuthority = 8192 + ((supplyQ15 * 3) >> 2);
        dynamicCutoff = (dynamicCutoff * supplyAuthority) >> 15;
        dynamicCutoff = clamp32(dynamicCutoff, 96, 30000);

        int32_t poweredResonance =
            (parameters.resonanceQ12 * supplyAuthority) >> 15;
        int32_t filtered = processLadder(saw, dynamicCutoff,
            poweredResonance);
        int32_t amplitude = envelope;
        if (parameters.accent) {
            amplitude += amplitude >> 2;
            if (amplitude > 32767)
                amplitude = 32767;
        }

        int32_t voice = (filtered * amplitude) >> 15;
        voice = (voice * supplyQ15) >> 15;
        int32_t raw = (saw * supplyQ15) >> 15;
        AudioOut1((int16_t)clamp32(voice, -2048, 2047));
        AudioOut2((int16_t)clamp32(raw, -2048, 2047));
        CVOut1Millivolts(
            (parameters.pitchMillivolts * supplyQ15) >> 15);
        PulseOut1(poweredGate);
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
        // Four trapezoidal-integrator one-poles form a TPT ladder. Each stage
        // is affine in the ladder input, which lets us solve the global
        // feedback path without iteration or division in the audio interrupt.
        int32_t oneMinusG = 32768 - cutoffQ15;
        int32_t constant = (oneMinusG * ladder[0]) >> 15;
        constant = ((cutoffQ15 * constant) >> 15) +
            ((oneMinusG * ladder[1]) >> 15);
        constant = ((cutoffQ15 * constant) >> 15) +
            ((oneMinusG * ladder[2]) >> 15);
        constant = ((cutoffQ15 * constant) >> 15) +
            ((oneMinusG * ladder[3]) >> 15);

        int32_t g2 = (cutoffQ15 * cutoffQ15) >> 15;
        int32_t g4 = (g2 * g2) >> 15;
        int32_t denominatorQ15 =
            32768 + ((resonanceQ12 * g4) >> 12);
        int32_t reciprocalQ15 = reciprocalQ15For(denominatorQ15);
        int32_t driven = input -
            ((resonanceQ12 * constant) >> 12);
        driven = (driven * reciprocalQ15) >> 15;
        driven = softClip(driveInput(driven));

        for (uint32_t i = 0; i < 4; ++i) {
            int32_t delta = driven - ladder[i];
            int32_t integrator = (delta * cutoffQ15) >> 15;
            int32_t output = integrator + ladder[i];
            ladder[i] = softClip(output + integrator);
            driven = output;
        }
        return softClip(driven);
    }

    static int32_t reciprocalQ15For(int32_t denominatorQ15)
    {
        denominatorQ15 = clamp32(denominatorQ15, 32768, 163840);
        uint32_t offset = (uint32_t)(denominatorQ15 - 32768);
        uint32_t index = offset >> 9;
        uint32_t fraction = offset & 0x1FFu;
        if (index >= 256u)
            return ReciprocalQ15Table[256];
        int32_t a = ReciprocalQ15Table[index];
        int32_t b = ReciprocalQ15Table[index + 1u];
        return a + (((b - a) * (int32_t)fraction) >> 9);
    }

    static int32_t driveInput(int32_t value)
    {
        // A small pre-drive keeps the voice lively without making high cutoff,
        // resonance, and envelope settings fizz against every ladder stage.
        return value + (value >> 3);
    }

    static int32_t softClip(int32_t value)
    {
        // Piecewise fixed-point saturation: linear through the centre, then a
        // gentler slope before the final rail. This is cheap enough to use at
        // every ladder stage and prevents resonance runaway.
        bool negative = value < 0;
        int32_t magnitude = negative ? -value : value;
        if (magnitude > 2304)
            magnitude = 2304 + ((magnitude - 2304) >> 3);
        if (magnitude > 3072)
            magnitude = 3072;
        return negative ? -magnitude : magnitude;
    }

    AudioParameters parameters = {
        midiNoteToPhaseIncrement(BaseMidiNote), 0, -2000,
        4096, 0, 128, 32767, 12000, BaseMidiNote, 0, 0, 1
    };
    uint32_t audioSlot = 0;
    uint32_t phase = 0;
    uint32_t smoothedIncrement = 0;
    uint32_t publishCounter = 0;
    uint32_t pulse1Edges = 0;
    uint32_t pulse2Edges = 0;
    int32_t envelope = 0;
    int32_t supplyQ15 = 0;
    int32_t ladder[4] = {};
    bool lastGate = false;
    uint32_t lastEnvelopeTrigger = 0;
};

Fr330hfr33 card;

namespace {

uint32_t randomState = 0xF330F333u;

uint32_t nextRandom()
{
    randomState = randomState * 1664525u + 1013904223u;
    return randomState;
}

uint8_t quantizedRandomNote(
    uint8_t scale, uint8_t octaves, uint8_t root, uint8_t baseNote)
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
    uint32_t note = baseNote + root + octave * 12u + degree;
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
    bool deviceWasMounted = false;

    HardwareSnapshot hardware = {};
    AudioParameters parameters = {};
    parameters.midiNote = BaseMidiNote;
    parameters.phaseIncrement = midiNoteToPhaseIncrement(parameters.midiNote);
    parameters.pitchMillivolts = -2000;
    parameters.powerCut = 1;

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
            bool deviceMounted = tud_mounted();
            if (deviceWasMounted && !deviceMounted)
                midi.allNotesOff();
            deviceWasMounted = deviceMounted;
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

        // This card's physical Main control reads in the opposite direction to
        // the desired musical gesture, so invert it here: counter-clockwise is
        // dark and clockwise is bright.
        int32_t cutoffKnob = 4095 - mainKnob;

        // Squared cutoff mapping gives the useful low-frequency area more room.
        parameters.cutoffQ15 = 120 +
            (int32_t)(((int64_t)cutoffKnob * cutoffKnob * 29200) >> 24);

        // Resonance uses a squared curve: most of X stays smooth and useful,
        // with strong resonance and self-oscillation reserved for the top end.
        parameters.resonanceQ12 =
            (int32_t)(((int64_t)xKnob * xKnob * 14500) >> 24);

        // One musically coupled control: clockwise gives a longer envelope and
        // more glide. Coefficients are calculated here, never in the ISR.
        parameters.envelopeDecayQ15 =
            24 + ((4095 - yKnob) * 900) / 4095;
        int32_t inverseY = 4095 - yKnob;
        int32_t physicalGlideQ15 = 32 +
            (int32_t)(((int64_t)inverseY * inverseY * 32735) >> 24);

        // X still adds some envelope sweep, but no longer stacks an extreme
        // sweep on top of maximum resonance.
        parameters.filterEnvelopeQ15 = 5500 + (xKnob * 7000) / 4095;

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
                    midi.scale, clamp32(midi.octaveSpan, 1, 4),
                    midi.rootNote, midi.baseMidiNote);
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
            parameters.envelopeTrigger = 0;
            parameters.powerCut = 0;
        } else if (mode == PlayMode::CvMidi) {
            bool forceSlide = hardware.pulse2High;
            if (midi.gate) {
                parameters.midiNote = midi.note;
                parameters.phaseIncrement = midiNoteToPhaseIncrement(midi.note);
                parameters.pitchMillivolts =
                    ((int32_t)midi.note - 60) * 1000 / 12;
                parameters.gate = 1;
                parameters.accent = midi.velocity >= 112;
                parameters.glideQ15 =
                    (forceSlide || midi.shouldGlide())
                    ? physicalGlideQ15
                    : 32767;
                parameters.envelopeTrigger = midi.triggerSerial();
            } else {
                int32_t pitchUnits =
                    (hardware.cv1 * 4096) / PitchCountsPerVolt;
                int32_t semitones = (pitchUnits * 12) / 4096;
                int32_t note = clamp32(BaseMidiNote + semitones, 0, 127);
                parameters.midiNote = (uint8_t)note;
                parameters.phaseIncrement =
                    pitchUnitsToPhaseIncrement(pitchUnits);
                parameters.pitchMillivolts =
                    -2000 + (hardware.cv1 * 1000) / PitchCountsPerVolt;
                parameters.gate = hardware.pulse1High;
                parameters.accent = 0;
                parameters.glideQ15 =
                    forceSlide ? physicalGlideQ15 : 32767;
                parameters.envelopeTrigger = 0;
            }
            parameters.powerCut = 0;
        } else {
            parameters.gate = 0;
            parameters.accent = 0;
            parameters.envelopeTrigger = 0;
            parameters.powerCut = 1;
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
    midi.allNotesOff();
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
