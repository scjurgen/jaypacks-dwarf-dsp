#pragma once

#include <cmath>
#include <cstddef>

namespace jp
{

/**
 * @brief Bends a set of evenly spaced values towards one end, under a single -1..+1 control.
 *
 * Delay lines spread linearly between two bounds give an even echo density, which is
 * rarely what a room does. One parameter that clusters the lengths towards the short or
 * the long end changes the character of a diffuser or a tank without changing its count or
 * its bounds, and passes through even spacing at zero.
 *
 * The curve is a power law y = x^(4^b) for b >= 0 and y = 1 - (1 - x)^(4^-b) for b < 0, the
 * exponent driven exponentially by the control, so equal steps of the parameter make equally
 * sized changes in shape across its whole range. Negative values pull the values towards
 * the upper bound (longer delays), positive ones towards the lower bound. Five values
 * between 1000 and 2000:
 *
 *   bulge   -1.0: 1000 1684 1938 1996 2000
 *            0.0: 1000 1250 1500 1750 2000
 *           +1.0: 1000 1004 1063 1316 2000
 */
class Bulge
{
  public:
    [[nodiscard]] static float getValue(const float x, const float bulge, const float bulgePower = 4.0f) noexcept
    {
        if (bulge < 0.0f)
        {
            if (x >= 1.0f)
            {
                return 1.0f;
            }
            return 1.0f - std::pow(1.0f - x, std::pow(bulgePower, -bulge));
        }
        return std::pow(x, std::pow(bulgePower, bulge));
    }

    /// Fills items values of the curve at x = i / (items - 1); a single item is 0.
    static void fillNormalizedTable(float* target, const size_t items, const float bulge) noexcept
    {
        if (items == 0)
        {
            return;
        }
        if (items == 1)
        {
            target[0] = 0.0f;
            return;
        }
        for (size_t i = 0; i < items; ++i)
        {
            target[i] = getValue(static_cast<float>(i) / static_cast<float>(items - 1), bulge);
        }
    }
};

}
