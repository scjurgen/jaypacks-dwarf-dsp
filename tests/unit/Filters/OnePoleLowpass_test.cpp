#include <cmath>
#include <gtest/gtest.h>
#include <tuple>

#include "tests/unit/TestHelpers.h"
#include "dsp/Filters/OnePoleLowpass.h"

namespace jp::test
{

class OnePoleLowpassRates : public ::testing::TestWithParam<float>
{
};

TEST_P(OnePoleLowpassRates, MinusThreeDbAtCutoffForLowCutoffs)
{
    const float sampleRate = GetParam();
    for (const float cutoff : {50.0f, 200.0f, 1000.0f})
    {
        const float pole = onePoleLowpassPole(cutoff, sampleRate);
        const double gainDb = toDb(std::abs(onePoleLowpassResponse(pole, cutoff, sampleRate)));
        EXPECT_NEAR(gainDb, -3.01, 0.02) << "cutoff " << cutoff << " at " << sampleRate;
    }
}

TEST_P(OnePoleLowpassRates, ProcessMatchesTransferFunction)
{
    const float sampleRate = GetParam();
    const float pole = onePoleLowpassPole(1000.0f, sampleRate);
    for (const double frequency : {100.0, 1000.0, 5000.0, 15000.0})
    {
        OnePoleLowpass lowpass;
        lowpass.setPole(pole);
        const double measured =
            measureSineGain([&lowpass](const float x) { return lowpass.process(x); }, frequency, sampleRate);
        const double theory = std::abs(onePoleLowpassResponse(pole, frequency, sampleRate));
        EXPECT_NEAR(toDb(measured), toDb(theory), 0.01) << "at " << frequency << " Hz, fs " << sampleRate;
    }
}

INSTANTIATE_TEST_SUITE_P(SampleRates, OnePoleLowpassRates, ::testing::Values(44100.0f, 48000.0f, 96000.0f));

TEST(OnePoleLowpass, UnityGainAtDc)
{
    OnePoleLowpass lowpass;
    lowpass.setPole(onePoleLowpassPole(100.0f, 48000.0f));
    float y = 0.0f;
    for (int i = 0; i < 48000; ++i)
    {
        y = lowpass.process(0.5f);
    }
    // in float the state stops where p * (y - x) rounds away (a dead band of roughly one ulp / (1 - p)),
    // about 1e-6 here, so exact equality is not reached
    EXPECT_NEAR(y, 0.5f, 1e-5f);
}

TEST(OnePoleLowpass, CutoffMovesAboveTargetNearNyquist)
{
    // the impulse-invariant mapping places the -3 dB point above fc at high cutoffs (see header)
    constexpr float sampleRate = 48000.0f;
    const float pole = onePoleLowpassPole(sampleRate / 8.0f, sampleRate);
    EXPECT_GT(toDb(std::abs(onePoleLowpassResponse(pole, sampleRate / 8.0, sampleRate))), -3.0);
}

TEST(OnePoleLowpass, PoleIsStableForAnyCutoff)
{
    for (const float cutoff : {-10.0f, 0.0f, 1.0f, 1000.0f, 23999.0f, 24000.0f, 100000.0f})
    {
        const float pole = onePoleLowpassPole(cutoff, 48000.0f);
        EXPECT_GE(pole, 0.0f) << cutoff;
        EXPECT_LE(pole, 1.0f) << cutoff;
    }
    EXPECT_EQ(onePoleLowpassPole(-10.0f, 48000.0f), 1.0f);
}

TEST(OnePoleLowpass, ProcessClampedLimitsState)
{
    OnePoleLowpass lowpass;
    lowpass.setPole(onePoleLowpassPole(1000.0f, 48000.0f));
    float y = 0.0f;
    for (int i = 0; i < 4800; ++i)
    {
        y = lowpass.processClamped(10.0f, 1.0f);
        ASSERT_LE(y, 1.0f);
    }
    EXPECT_EQ(y, 1.0f);
    for (int i = 0; i < 4800; ++i)
    {
        y = lowpass.processClamped(-10.0f, 1.0f);
        ASSERT_GE(y, -1.0f);
    }
    EXPECT_EQ(y, -1.0f);
}

TEST(OnePoleLowpass, ResetClearsState)
{
    OnePoleLowpass lowpass;
    lowpass.setPole(0.9f);
    std::ignore = lowpass.process(1.0f);
    lowpass.reset();
    EXPECT_EQ(lowpass.process(0.0f), 0.0f);
}

}
