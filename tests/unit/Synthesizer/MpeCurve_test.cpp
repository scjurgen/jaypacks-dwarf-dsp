#include <cmath>
#include <gtest/gtest.h>

#include "dsp/Synthesizer/MpeCurve.h"

namespace jp::test
{

namespace
{
constexpr CtrlCurve c_allCurves[]{CtrlCurve::CubeRoot, CtrlCurve::SquareRoot, CtrlCurve::Linear, CtrlCurve::Square,
                                  CtrlCurve::Cube};
}

TEST(MpeCurve, LinearCurveIsTheNormalisedValue)
{
    EXPECT_FLOAT_EQ(mpeCurve(CtrlCurve::Linear, 0), 0.0f);
    EXPECT_FLOAT_EQ(mpeCurve(CtrlCurve::Linear, c_mpeMaxValue), 1.0f);
    EXPECT_NEAR(mpeCurve(CtrlCurve::Linear, 8192), 8192.0f / 16383.0f, 1e-6f);
}

TEST(MpeCurve, EachCurveIsThePowerItIsNamedAfter)
{
    const int value = 4096;
    const float x = 4096.0f / 16383.0f;
    EXPECT_NEAR(mpeCurve(CtrlCurve::CubeRoot, value), std::pow(x, 1.0f / 3.0f), 1e-6f);
    EXPECT_NEAR(mpeCurve(CtrlCurve::SquareRoot, value), std::sqrt(x), 1e-6f);
    EXPECT_NEAR(mpeCurve(CtrlCurve::Square, value), x * x, 1e-7f);
    EXPECT_NEAR(mpeCurve(CtrlCurve::Cube, value), x * x * x, 1e-7f);
}

TEST(MpeCurve, SquareBowsBelowAndCubeRootAboveLinearInTheMiddle)
{
    EXPECT_LT(mpeCurve(CtrlCurve::Square, 8192), mpeCurve(CtrlCurve::Linear, 8192));
    EXPECT_LT(mpeCurve(CtrlCurve::Cube, 8192), mpeCurve(CtrlCurve::Square, 8192));
    EXPECT_GT(mpeCurve(CtrlCurve::SquareRoot, 8192), mpeCurve(CtrlCurve::Linear, 8192));
    EXPECT_GT(mpeCurve(CtrlCurve::CubeRoot, 8192), mpeCurve(CtrlCurve::SquareRoot, 8192));
}

TEST(MpeCurve, EveryCurveMapsTheEndsToTheEndsAndIsMonotonic)
{
    for (const CtrlCurve curve : c_allCurves)
    {
        EXPECT_EQ(mpeCurve(curve, 0), 0.0f);
        EXPECT_EQ(mpeCurve(curve, c_mpeMaxValue), 1.0f);
        float previous = 0.0f;
        for (int value = 0; value <= c_mpeMaxValue; ++value)
        {
            const float current = mpeCurve(curve, value);
            ASSERT_GE(current, previous) << static_cast<int>(curve) << " at " << value;
            previous = current;
        }
    }
}

TEST(MpeCurve, OutOfRangeValuesAreClamped)
{
    for (const CtrlCurve curve : c_allCurves)
    {
        EXPECT_EQ(mpeCurve(curve, -5), mpeCurve(curve, 0));
        EXPECT_EQ(mpeCurve(curve, 100000), mpeCurve(curve, c_mpeMaxValue));
        EXPECT_EQ(mpeCurveBipolar(curve, -5), mpeCurveBipolar(curve, 0));
        EXPECT_EQ(mpeCurveBipolar(curve, 100000), mpeCurveBipolar(curve, c_mpeMaxValue));
    }
    EXPECT_EQ(curveOf(CtrlCurve::Linear, -1.0f), 0.0f);
    EXPECT_EQ(curveOf(CtrlCurve::Linear, 2.0f), 1.0f);
}

TEST(MpeCurve, BipolarReachesMinusOneZeroAndPlusOneExactly)
{
    for (const CtrlCurve curve : c_allCurves)
    {
        EXPECT_EQ(mpeCurveBipolar(curve, 0), -1.0f);
        EXPECT_EQ(mpeCurveBipolar(curve, 8192), 0.0f);
        EXPECT_EQ(mpeCurveBipolar(curve, c_mpeMaxValue), 1.0f);
    }
}

TEST(MpeCurve, BipolarIsMonotonicAndKeepsTheSignOfTheDeflection)
{
    for (const CtrlCurve curve : c_allCurves)
    {
        float previous = -1.0f;
        for (int value = 0; value <= c_mpeMaxValue; ++value)
        {
            const float current = mpeCurveBipolar(curve, value);
            ASSERT_GE(current, previous) << static_cast<int>(curve) << " at " << value;
            ASSERT_EQ(current > 0.0f, value > 8192) << value;
            ASSERT_EQ(current < 0.0f, value < 8192) << value;
            previous = current;
        }
    }
}

TEST(MpeCurve, BipolarLinearIsSymmetricAroundTheCentre)
{
    // one step above and below the centre differ by the half-range step sizes (8191 against 8192)
    EXPECT_NEAR(mpeCurveBipolar(CtrlCurve::Linear, 8192 + 1000), 1000.0f / 8191.0f, 1e-6f);
    EXPECT_NEAR(mpeCurveBipolar(CtrlCurve::Linear, 8192 - 1000), -1000.0f / 8192.0f, 1e-6f);
    EXPECT_NEAR(mpeCurveBipolar(CtrlCurve::Square, 8192 + 4096), (4096.0f / 8191.0f) * (4096.0f / 8191.0f), 1e-6f);
    EXPECT_NEAR(mpeCurveBipolar(CtrlCurve::Square, 8192 - 4096), -(4096.0f / 8192.0f) * (4096.0f / 8192.0f), 1e-6f);
}

TEST(MpeCurve, SevenBitValuesReachFullScale)
{
    EXPECT_EQ(mpeValueFrom7Bit(0), 0);
    EXPECT_EQ(mpeValueFrom7Bit(127), c_mpeMaxValue);
    EXPECT_EQ(mpeValueFrom7Bit(64), (64 << 7) | 64);
    EXPECT_EQ(mpeValueFrom7Bit(-3), 0);
    EXPECT_EQ(mpeValueFrom7Bit(300), c_mpeMaxValue);
    int previous = -1;
    for (int value = 0; value <= 127; ++value)
    {
        EXPECT_GT(mpeValueFrom7Bit(value), previous);
        previous = mpeValueFrom7Bit(value);
    }
}

}
