#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace jp
{

/// Response shape of a controller dimension, from bowed up (cube root) to bowed down (cube).
enum class CtrlCurve
{
    CubeRoot,
    SquareRoot,
    Linear,
    Square,
    Cube
};

/// Largest 14-bit controller value (pitch bend range 0 to 16383, centre 8192).
constexpr int c_mpeMaxValue{16383};

/// A 7-bit controller value (0 to 127) on the 14-bit scale: the bits repeated, so 0 stays 0 and 127 reaches
/// c_mpeMaxValue (a plain shift by 7 stops at 16256, 0.8 % short of full scale).
[[nodiscard]] constexpr int mpeValueFrom7Bit(const int value7Bit) noexcept
{
    const int value = std::clamp(value7Bit, 0, 127);
    return (value << 7) | value;
}

/**
 * @brief A normalised position x in [0, 1] mapped through a curve: x^(1/3), x^(1/2), x, x^2 or x^3.
 *
 * Computed, not looked up: a table of 16384 floats per curve (what a lookup would need for the 14-bit values)
 * is 320 kB for the five curves, much of a small core's cache, and a controller message arrives at most a few
 * hundred times a second, so cbrtf and sqrtf per message cost nothing. x is clamped to [0, 1]. All curves map
 * 0 to 0 and 1 to 1 and are monotonic.
 */
[[nodiscard]] inline float curveOf(const CtrlCurve curve, const float x) noexcept
{
    const float position = std::clamp(x, 0.0f, 1.0f);
    switch (curve)
    {
        case CtrlCurve::CubeRoot:
            return std::cbrt(position);
        case CtrlCurve::SquareRoot:
            return std::sqrt(position);
        case CtrlCurve::Linear:
            return position;
        case CtrlCurve::Square:
            return position * position;
        case CtrlCurve::Cube:
            return position * position * position;
    }
    return position;
}

/// A 14-bit value on [0, c_mpeMaxValue] through a curve, result in [0, 1]. Values outside are clamped.
[[nodiscard]] inline float mpeCurve(const CtrlCurve curve, const int value14Bit) noexcept
{
    const int value = std::clamp(value14Bit, 0, c_mpeMaxValue);
    return curveOf(curve, static_cast<float>(value) / static_cast<float>(c_mpeMaxValue));
}

/**
 * @brief A 14-bit value around the centre 8192 through a curve, result in [-1, 1].
 *
 * The curve is applied to the distance from the centre in units of the half range, and the sign is kept: 0
 * gives -1, 8192 gives 0, 16383 gives +1 exactly (the two halves have 8192 and 8191 steps, so each is scaled
 * by its own). Pitch bend of a MIDI channel is such a value.
 */
[[nodiscard]] inline float mpeCurveBipolar(const CtrlCurve curve, const int value14Bit) noexcept
{
    const int value = std::clamp(value14Bit, 0, c_mpeMaxValue);
    if (value >= 8192)
    {
        return curveOf(curve, static_cast<float>(value - 8192) / 8191.0f);
    }
    return -curveOf(curve, static_cast<float>(8192 - value) / 8192.0f);
}

}
