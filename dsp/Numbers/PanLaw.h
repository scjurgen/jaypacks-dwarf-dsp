#pragma once

#include <algorithm>
#include <cmath>

namespace jp
{

/// Dry and wet gains of a mix control.
struct DryWet
{
    float dry;
    float wet;
};

/**
 * @brief Constant-power dry/wet law for a mix control m in [-1, 1].
 *
 * dry = (cos a - sin a) / sqrt(2), wet = (cos a + sin a) / sqrt(2) with a = m pi / 4, i.e.
 * dry = cos(pi/4 (m + 1)), wet = sin(pi/4 (m + 1)): -1 is dry only, +1 is wet only, 0 is
 * both at -3 dB, and dry^2 + wet^2 = 1 everywhere, so uncorrelated dry and wet signals keep
 * their combined power across the whole range. Values outside [-1, 1] are clamped.
 */
[[nodiscard]] inline DryWet constantPowerMix(const float mix) noexcept
{
    constexpr float quarterPi{0.78539816339744830962f};
    const float angle = quarterPi * (std::clamp(mix, -1.0f, 1.0f) + 1.0f);
    return {std::cos(angle), std::sin(angle)};
}

}
