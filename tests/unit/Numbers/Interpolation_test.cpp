#include <algorithm>
#include <array>
#include <gtest/gtest.h>

#include "dsp/Numbers/Interpolation.h"

namespace jp::test
{

TEST(Interpolation, HermitePassesThroughTheInnerPoints)
{
    const std::array<float, 4> y{0.3f, -1.0f, 2.0f, 0.7f};
    EXPECT_FLOAT_EQ(hermite43x(y.data(), 0.0f), -1.0f);
    EXPECT_NEAR(hermite43x(y.data(), 1.0f), 2.0f, 1e-6f);
}

TEST(Interpolation, HermiteIsExactForQuadratics)
{
    // y = 2 t^2 - 3 t + 1 at t = -1, 0, 1, 2
    const auto f = [](const float t) { return 2.0f * t * t - 3.0f * t + 1.0f; };
    const std::array<float, 4> y{f(-1.0f), f(0.0f), f(1.0f), f(2.0f)};
    for (const float x : {0.0f, 0.1f, 0.25f, 0.5f, 0.9f})
    {
        EXPECT_NEAR(hermite43x(y.data(), x), f(x), 1e-5f) << x;
    }
}

TEST(Interpolation, HermiteKeepsAConstant)
{
    const std::array<float, 4> y{0.25f, 0.25f, 0.25f, 0.25f};
    for (const float x : {0.0f, 0.3f, 0.77f})
    {
        EXPECT_FLOAT_EQ(hermite43x(y.data(), x), 0.25f);
    }
}


TEST(Interpolation, ZeroOrderHoldReadsTheFirstSampleWhateverTheFraction)
{
    const std::array<float, 4> y{0.3f, -1.0f, 2.0f, 0.7f};
    for (const float x : {0.0f, 0.5f, 0.999f})
    {
        EXPECT_EQ(zeroOrderHold(y.data(), x), 0.3f);
    }
}

TEST(Interpolation, LinearIsExactForLinesAndTouchesTheEnds)
{
    const std::array<float, 2> y{-1.0f, 3.0f};
    EXPECT_FLOAT_EQ(linearPt2(y.data(), 0.0f), -1.0f);
    EXPECT_FLOAT_EQ(linearPt2(y.data(), 0.25f), 0.0f);
    EXPECT_FLOAT_EQ(linearPt2(y.data(), 0.5f), 1.0f);
}

TEST(Interpolation, BsplineKeepsAConstantAndFollowsAStraightLine)
{
    const std::array<float, 4> constant{0.25f, 0.25f, 0.25f, 0.25f};
    const std::array<float, 4> line{-1.0f, 1.0f, 3.0f, 5.0f}; // y = 2 t + 1 at t = -1, 0, 1, 2
    for (const float x : {0.0f, 0.3f, 0.77f})
    {
        EXPECT_NEAR(bspline43x(constant.data(), x), 0.25f, 1e-6f);
        EXPECT_NEAR(bspline43x(line.data(), x), 2.0f * x + 1.0f, 1e-5f) << x;
    }
}

TEST(Interpolation, BsplineSmoothsInsteadOfPassingThroughThePoints)
{
    // y = t^2 at t = -1, 0, 1, 2: a cubic B-spline reproduces it too high by the second difference over 6
    const std::array<float, 4> y{1.0f, 0.0f, 1.0f, 4.0f};
    EXPECT_NEAR(bspline43x(y.data(), 0.0f), 1.0f / 3.0f, 1e-6f);
    EXPECT_NEAR(bspline43x(y.data(), 1.0f), 1.0f + 1.0f / 3.0f, 1e-6f);
}

TEST(Interpolation, BsplineIsSmootherAtAKinkThanHermite)
{
    // a plateau of two points between zeros: the B-spline stays inside the data range, the Hermite overshoots
    const std::array<float, 4> y{0.0f, 1.0f, 1.0f, 0.0f};
    float lowestBspline = 1e9f;
    float highestBspline = -1e9f;
    float highestHermite = -1e9f;
    for (float x = 0.0f; x <= 1.0f; x += 0.01f)
    {
        lowestBspline = std::min(lowestBspline, bspline43x(y.data(), x));
        highestBspline = std::max(highestBspline, bspline43x(y.data(), x));
        highestHermite = std::max(highestHermite, hermite43x(y.data(), x));
    }
    EXPECT_GE(lowestBspline, 0.0f);
    EXPECT_LE(highestBspline, 1.0f);
    EXPECT_GT(highestHermite, 1.0f);
}

}
