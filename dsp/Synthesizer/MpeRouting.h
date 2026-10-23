#pragma once

#include <cstddef>

#include "dsp/Synthesizer/MpeCurve.h"

namespace jp
{

/// Where a routing slot reads its value. The values are fixed numbers, those of AbacDsp's script constants (they are
/// stored and exchanged as numbers): do not reorder.
enum class CtrlDimension
{
    X, ///< per-note pitch bend of the channel
    Y, ///< per-note CC74 (timbre) and the mod wheel
    Z, ///< per-note channel pressure
    Velocity,
    Note,
    EnvelopeFilter,
    EnvelopeAmplitude
};

/// What a routing slot drives. The values are fixed numbers, those of AbacDsp's script constants (they are stored and
/// exchanged as numbers): do not reorder.
enum class CtrlTarget
{
    None,
    PitchBend,
    PitchBendSecondary,
    FilterCutoff,
    FilterResonance,
    Pan,
    OscFreq2,
    OscFreq3,
    OscLevel1,
    OscLevel2,
    OscLevel3,
    LfoLevel,
    SustainVol,
    AttackTime,
    DecayTime,
    ReleaseTime,
    Distortion
};

/// How a dimension value is read: Abs from 0 to 1, BiPolar from -1 to 1 around the centre of the range.
enum class CtrlValueType
{
    Abs,
    BiPolar
};

/// Number of routing slots of a voice.
constexpr size_t c_mpeSlotCount{10};

/**
 * @brief One routing slot: reads `source`, shapes it through `curve`, scales by `depth` and adds the result into
 * `target`.
 *
 * Several slots may read the same source or drive the same target; their contributions add (the voice sums them from
 * the last value of each source). The scale of depth is the target's own (octaves, semitones, a fraction), set by the
 * voice that applies the slot.
 */
struct MpeCtrl
{
    CtrlDimension source{CtrlDimension::X};
    CtrlCurve curve{CtrlCurve::Linear};
    CtrlTarget target{CtrlTarget::None};
    CtrlValueType type{CtrlValueType::Abs};
    float depth{0.0f};
};

/// A slot's value for a 14-bit dimension value: the curve (read as Abs or BiPolar by the slot's type) times depth.
[[nodiscard]] inline float mpeCtrlValue(const MpeCtrl& slot, const int value14Bit) noexcept
{
    const float shaped = slot.type == CtrlValueType::BiPolar ? mpeCurveBipolar(slot.curve, value14Bit)
                                                             : mpeCurve(slot.curve, value14Bit);
    return shaped * slot.depth;
}

}
