#pragma once

#include <array>
#include <cstddef>

#include "dsp/Generators/SynthLfo.h"
#include "dsp/Synthesizer/MpeCurve.h"
#include "dsp/Synthesizer/MpeRouting.h"

namespace jp
{

/// The waveforms of an oscillator; the numbers are those of AbacDsp's script constants.
enum class OscWaveform : size_t
{
    Triangle,
    SharkFin,
    Saw,
    Square,
    White
};

/// One oscillator: detune in semitones, level, pulse width (above 0 the pulse-width modulation, see the voice) and
/// the frequency factor against the note.
struct OscillatorPatch
{
    OscWaveform waveform{OscWaveform::Triangle};
    float level{0.0f};
    float detune{0.0f};
    float pwm{0.0f};
    float pitchFactor{1.0f};
};

/// The morph of an oscillator over three waveforms: the second and third (the first is the oscillator's waveform) and
/// the position over the three, 0 the first, 0.5 the second, 1 the third. At position 0 the oscillator sounds as it
/// does without a morph, whatever the other two are.
struct WaveMorphPatch
{
    OscWaveform second{OscWaveform::Square};
    OscWaveform third{OscWaveform::Saw};
    float position{0.0f};
};

struct EnvelopePatch
{
    float attackMs{10.0f};
    float decayMs{200.0f};
    float sustainLevel{0.5f};
    float releaseMs{100.0f};
};

/// An envelope and how many octaves it moves the cutoff (positive opens the filter as the envelope rises).
struct FilterEnvelopePatch
{
    float attackMs{10.0f};
    float decayMs{200.0f};
    float sustainLevel{0.5f};
    float releaseMs{100.0f};
    float contour{0.0f};
};

struct PitchEnvelopePatch
{
    float attackMs{0.0f};
    float decayMs{0.0f};
    float depthSemitones{0.0f};
    float glideMsPerOctave{0.0f};
};

struct LfoPatch
{
    LfoType waveform{LfoType::Sine};
    float speedHz{1.0f};
    float filterDepth{0.0f};
    float oscDepth{0.0f};
    float keyFollow{0.0f};
};

/// The base cutoff as a note number, the normalised resonance and the filter type as a row of c_poleMixingPresets
/// (3 is the 4-pole lowpass, the type of every factory patch).
struct FilterPatch
{
    float cutoff{72.0f};
    float resonance{0.0f};
    size_t preset{3};
};

/// One slot of the routing matrix; an unused slot has the target None.
struct SlotPatch
{
    CtrlDimension source{CtrlDimension::X};
    CtrlCurve curve{CtrlCurve::Linear};
    CtrlTarget target{CtrlTarget::None};
    CtrlValueType type{CtrlValueType::Abs};
    float depth{0.0f};
};

/// Off by default (mix 0).
struct PhaserPatch
{
    float rateHz{0.3f};
    float depth{0.5f};
    float feedback{0.0f};
    float mix{0.0f};
};

/// Off by default (mix 0).
struct ChorusPatch
{
    float rateHz{0.6f};
    float depth{0.5f};
    float mix{0.0f};
};

/// Off by default (mix -100 dB).
struct ReverbPatch
{
    float sizeMeters{12.0f};
    float decayMs{1500.0f};
    float dryDb{0.0f};
    float mixDb{-100.0f};
};

/**
 * @brief Everything a patch sets: the same fields as the commands of AbacDsp's patch scripts (SetOscillator,
 * SetAmpEnvelope, SetFilterEnvelope, SetPitchEnvelope, SetLfo, SetFilter, SetDistortion, SetCtrlSlot, SetPhaser,
 * SetChorus, SetReverb), with the defaults of those commands, plus the level of the patch in dB. A patch is
 * applied completely: every slot of the routing matrix is written, unused ones with the target None, so nothing of
 * the previous patch stays.
 */
struct MorphPatch
{
    const char* name{""};
    float volumeDb{0.0f};
    std::array<OscillatorPatch, 3> oscillators{};
    EnvelopePatch amplitude{};
    FilterEnvelopePatch filterEnvelope{};
    PitchEnvelopePatch pitchEnvelope{};
    LfoPatch lfo{};
    FilterPatch filter{};
    size_t distortion{0};
    std::array<SlotPatch, c_mpeSlotCount> slots{};
    PhaserPatch phaser{};
    ChorusPatch chorus{};
    ReverbPatch reverb{};
    std::array<WaveMorphPatch, 3> morphs{}; ///< last: the patches of the oracle are written positionally
};

inline bool operator==(const OscillatorPatch& a, const OscillatorPatch& b) noexcept
{
    return a.waveform == b.waveform && a.level == b.level && a.detune == b.detune && a.pwm == b.pwm &&
           a.pitchFactor == b.pitchFactor;
}

inline bool operator==(const WaveMorphPatch& a, const WaveMorphPatch& b) noexcept
{
    return a.second == b.second && a.third == b.third && a.position == b.position;
}

inline bool operator==(const EnvelopePatch& a, const EnvelopePatch& b) noexcept
{
    return a.attackMs == b.attackMs && a.decayMs == b.decayMs && a.sustainLevel == b.sustainLevel &&
           a.releaseMs == b.releaseMs;
}

inline bool operator==(const FilterEnvelopePatch& a, const FilterEnvelopePatch& b) noexcept
{
    return a.attackMs == b.attackMs && a.decayMs == b.decayMs && a.sustainLevel == b.sustainLevel &&
           a.releaseMs == b.releaseMs && a.contour == b.contour;
}

inline bool operator==(const PitchEnvelopePatch& a, const PitchEnvelopePatch& b) noexcept
{
    return a.attackMs == b.attackMs && a.decayMs == b.decayMs && a.depthSemitones == b.depthSemitones &&
           a.glideMsPerOctave == b.glideMsPerOctave;
}

inline bool operator==(const LfoPatch& a, const LfoPatch& b) noexcept
{
    return a.waveform == b.waveform && a.speedHz == b.speedHz && a.filterDepth == b.filterDepth &&
           a.oscDepth == b.oscDepth && a.keyFollow == b.keyFollow;
}

inline bool operator==(const FilterPatch& a, const FilterPatch& b) noexcept
{
    return a.cutoff == b.cutoff && a.resonance == b.resonance && a.preset == b.preset;
}

inline bool operator==(const SlotPatch& a, const SlotPatch& b) noexcept
{
    return a.source == b.source && a.curve == b.curve && a.target == b.target && a.type == b.type && a.depth == b.depth;
}

inline bool operator==(const PhaserPatch& a, const PhaserPatch& b) noexcept
{
    return a.rateHz == b.rateHz && a.depth == b.depth && a.feedback == b.feedback && a.mix == b.mix;
}

inline bool operator==(const ChorusPatch& a, const ChorusPatch& b) noexcept
{
    return a.rateHz == b.rateHz && a.depth == b.depth && a.mix == b.mix;
}

inline bool operator==(const ReverbPatch& a, const ReverbPatch& b) noexcept
{
    return a.sizeMeters == b.sizeMeters && a.decayMs == b.decayMs && a.dryDb == b.dryDb && a.mixDb == b.mixDb;
}

/// True if every setting of the two patches is equal (the name is not a setting).
inline bool sameSettings(const MorphPatch& a, const MorphPatch& b) noexcept
{
    return a.volumeDb == b.volumeDb && a.oscillators == b.oscillators && a.morphs == b.morphs &&
           a.amplitude == b.amplitude && a.filterEnvelope == b.filterEnvelope && a.pitchEnvelope == b.pitchEnvelope &&
           a.lfo == b.lfo && a.filter == b.filter && a.distortion == b.distortion && a.slots == b.slots &&
           a.phaser == b.phaser && a.chorus == b.chorus && a.reverb == b.reverb;
}

}
