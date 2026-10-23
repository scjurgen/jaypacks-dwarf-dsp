#include <cmath>
#include <gtest/gtest.h>
#include <iostream>

#include "tests/unit/TestHelpers.h"
#include "dsp/Filters/OnePoleHighpass.h"

namespace jp::test
{

class OnePoleHighpassRates : public ::testing::TestWithParam<float>
{
};

TEST_P(OnePoleHighpassRates, ProcessMatchesTransferFunction)
{
    const float sampleRate = GetParam();
    const float pole = onePoleLowpassPole(100.0f, sampleRate);
    for (const double frequency : {30.0, 100.0, 1000.0, 15000.0})
    {
        OnePoleHighpass highpass;
        highpass.setPole(pole);
        const double measured =
            measureSineGain([&highpass](const float x) { return highpass.process(x); }, frequency, sampleRate);
        const double theory = std::abs(onePoleHighpassResponse(pole, frequency, sampleRate));
        EXPECT_NEAR(toDb(measured), toDb(theory), 0.01) << "at " << frequency << " Hz, fs " << sampleRate;
    }
}

INSTANTIATE_TEST_SUITE_P(SampleRates, OnePoleHighpassRates, ::testing::Values(44100.0f, 48000.0f, 96000.0f));

TEST(OnePoleHighpass, UnityAtNyquistAndZeroAtDc)
{
    for (const float cutoff : {20.0f, 500.0f, 5000.0f})
    {
        const float pole = onePoleLowpassPole(cutoff, 48000.0f);
        EXPECT_NEAR(std::abs(onePoleHighpassResponse(pole, 24000.0, 48000.0)), 1.0, 1e-9) << cutoff;
        EXPECT_NEAR(std::abs(onePoleHighpassResponse(pole, 0.0, 48000.0)), 0.0, 1e-12) << cutoff;
    }
    OnePoleHighpass highpass;
    highpass.setPole(onePoleLowpassPole(20.0f, 48000.0f));
    float y = 1.0f;
    for (int i = 0; i < 48000; ++i)
    {
        y = highpass.process(0.5f);
    }
    EXPECT_NEAR(y, 0.0f, 1e-6f);
}

TEST(OnePoleHighpass, MinusThreeDbPointNearCutoff)
{
    // the matched-z pole puts the -3 dB point close to fc at low cutoffs
    for (const float cutoff : {20.0f, 100.0f, 500.0f})
    {
        const float pole = onePoleLowpassPole(cutoff, 48000.0f);
        const double gainDb = toDb(std::abs(onePoleHighpassResponse(pole, cutoff, 48000.0)));
        std::cout << "[ highpass ] fc " << cutoff << " Hz: " << gainDb << " dB at fc\n";
        EXPECT_NEAR(gainDb, -3.01, 0.05) << cutoff;
    }
}

TEST(OnePoleHighpass, ResetClearsState)
{
    OnePoleHighpass highpass;
    highpass.setPole(onePoleLowpassPole(100.0f, 48000.0f));
    std::ignore = highpass.process(1.0f);
    highpass.reset();
    EXPECT_EQ(highpass.process(0.0f), 0.0f);
}

}
