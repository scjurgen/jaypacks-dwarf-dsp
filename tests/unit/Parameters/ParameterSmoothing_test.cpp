#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <tuple>

#include "dsp/Parameters/ParameterSmoothing.h"

namespace jp::test
{

namespace
{
OnePoleSmoother makeSmoother(const float sampleRate, const float timeMs)
{
    OnePoleSmoother smoother;
    smoother.setSampleRate(sampleRate);
    smoother.setTimeMs(timeMs);
    smoother.snapTo(0.0f);
    smoother.setTarget(1.0f);
    return smoother;
}
}

class OnePoleSmootherRates : public ::testing::TestWithParam<float>
{
};

TEST_P(OnePoleSmootherRates, ReachesOneMinusOneOverEAfterTheTimeConstant)
{
    const float sampleRate = GetParam();
    auto smoother = makeSmoother(sampleRate, 10.0f);
    const auto samples = static_cast<int>(std::lround(0.010 * sampleRate));
    float y = 0.0f;
    for (int i = 0; i < samples; ++i)
    {
        y = smoother.next();
    }
    EXPECT_NEAR(y, 1.0f - std::exp(-1.0f), 0.002f);
}

INSTANTIATE_TEST_SUITE_P(SampleRates, OnePoleSmootherRates, ::testing::Values(44100.0f, 48000.0f, 96000.0f));

TEST(OnePoleSmoother, ApproachesWithoutOvershoot)
{
    auto smoother = makeSmoother(48000.0f, 20.0f);
    float previous = 0.0f;
    for (int i = 0; i < 48000; ++i)
    {
        const float y = smoother.next();
        ASSERT_GE(y, previous);
        ASSERT_LE(y, 1.0f);
        previous = y;
    }
}

TEST(OnePoleSmoother, LargestStepIsOneMinusCoefficientTimesDistance)
{
    auto smoother = makeSmoother(48000.0f, 20.0f);
    const float expected = 1.0f - smoother.getCoefficient();
    float previous = 0.0f;
    float largest = 0.0f;
    for (int i = 0; i < 4800; ++i)
    {
        const float y = smoother.next();
        largest = std::max(largest, y - previous);
        previous = y;
    }
    EXPECT_NEAR(largest, expected, 1e-6f);
    EXPECT_NEAR(expected, 1.0f / 960.0f, 1e-6f); // 20 ms at 48 kHz
}

TEST(OnePoleSmoother, SettlesExactlyOnTheTarget)
{
    auto smoother = makeSmoother(48000.0f, 5.0f);
    for (int i = 0; i < 48000 && !smoother.isSettled(); ++i)
    {
        std::ignore = smoother.next();
    }
    EXPECT_TRUE(smoother.isSettled());
    EXPECT_EQ(smoother.getCurrent(), 1.0f);
}

TEST(OnePoleSmoother, ZeroTimeJumpsImmediately)
{
    auto smoother = makeSmoother(48000.0f, 0.0f);
    EXPECT_EQ(smoother.next(), 1.0f);
}

TEST(OnePoleSmoother, SnapToSkipsTheRamp)
{
    auto smoother = makeSmoother(48000.0f, 50.0f);
    smoother.snapTo(0.25f);
    EXPECT_EQ(smoother.next(), 0.25f);
    EXPECT_TRUE(smoother.isSettled());
}

}
