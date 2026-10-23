#pragma once

#include <cmath>
#include <type_traits>

namespace jp
{

/**
 * @brief Moves a value toward its nearest integer by level: 0 leaves it, 1 snaps it, in between blends.
 *
 * result = r + (x - r)(1 - level) with r = round(x), so level is the fraction of the distance to the nearest
 * integer that is removed. Applied to a pitch in semitones it is a continuous quantiser of a pitch bend:
 * halves round away from zero (0.5 gives 1, -0.5 gives -1), as std::round does. level is not clamped.
 */
template <typename FP>
[[nodiscard]] FP pitchQuantize(const FP in, const FP level) noexcept
{
    static_assert(std::is_floating_point_v<FP>, "pitchQuantize works on floating point values");
    const FP slot = std::round(in);
    const FP distance = in - slot;
    return slot + distance * (FP{1} - level);
}

}
