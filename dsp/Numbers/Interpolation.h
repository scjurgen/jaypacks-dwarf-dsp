#pragma once

namespace jp
{

/// An interpolation kernel: reads a window of samples starting one before the integer position and returns the
/// value at fraction x in [0, 1) after the second one (a kernel that needs fewer points ignores the rest).
using InterpolationFn = float (*)(const float* y, float x);

/// Zero-order hold: the sample at the integer position (y[0]), a staircase.
[[nodiscard]] inline float zeroOrderHold(const float* y, const float /*x*/) noexcept
{
    return y[0];
}

/// Linear interpolation between y[0] and y[1].
[[nodiscard]] inline float linearPt2(const float* y, const float x) noexcept
{
    return y[0] + (y[1] - y[0]) * x;
}

/// 4-point, 3rd-order cubic B-spline between y[1] and y[2] at fraction x in [0, 1): reads y[0] .. y[3]. Continuous
/// to the second derivative, but a smoothing kernel: it does not pass through the points (a quadratic comes out
/// too high by its second difference over 6), only through straight lines, and a constant stays constant.
/// @see http://yehar.com/blog/wp-content/uploads/2009/08/deip.pdf
[[nodiscard]] inline float bspline43x(const float* y, const float x) noexcept
{
    constexpr float oneOverSix = 1.0f / 6.0f;
    constexpr float twoOverThree = 2.0f / 3.0f;
    const float ym1py1 = y[0] + y[2];
    const float c0 = oneOverSix * ym1py1 + twoOverThree * y[1];
    const float c1 = 0.5f * (y[2] - y[0]);
    const float c2 = 0.5f * ym1py1 - y[1];
    const float c3 = 0.5f * (y[1] - y[2]) + oneOverSix * (y[3] - y[0]);
    return ((c3 * x + c2) * x + c1) * x + c0;
}

/// 4-point, 3rd-order Hermite (Catmull-Rom) interpolation between y[1] and y[2] at fraction
/// x in [0, 1): reads y[0] .. y[3], so a caller places the window one sample before the
/// integer position. Continuous first derivative, exact for quadratics.
/// @see http://yehar.com/blog/wp-content/uploads/2009/08/deip.pdf
[[nodiscard]] inline float hermite43x(const float* y, const float x) noexcept
{
    const float c0 = y[1];
    const float c1 = 0.5f * (y[2] - y[0]);
    const float c2 = y[0] - 2.5f * y[1] + 2.0f * y[2] - 0.5f * y[3];
    const float c3 = 0.5f * (y[3] - y[0]) + 1.5f * (y[1] - y[2]);
    return ((c3 * x + c2) * x + c1) * x + c0;
}

}
