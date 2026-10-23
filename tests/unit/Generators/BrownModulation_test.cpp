#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <tuple>
#include <vector>

#include "dsp/Generators/BrownModulation.h"

namespace jp::test
{

namespace
{
std::vector<float> run(BrownModulation& noise, const size_t length)
{
    std::vector<float> out(length);
    for (float& x : out)
    {
        x = noise.step();
    }
    return out;
}

double rms(const std::vector<float>& signal, const size_t from = 0)
{
    double sum = 0.0;
    for (size_t i = from; i < signal.size(); ++i)
    {
        sum += static_cast<double>(signal[i]) * static_cast<double>(signal[i]);
    }
    return std::sqrt(sum / static_cast<double>(signal.size() - from));
}
}

TEST(BrownModulation, IsFiniteAndHasNoDrift)
{
    BrownModulation noise(1000.0f);
    noise.setCenterFrequency(2.0f);
    const auto out = run(noise, 200000);
    double mean = 0.0;
    for (const float x : out)
    {
        ASSERT_TRUE(std::isfinite(x));
        mean += static_cast<double>(x);
    }
    EXPECT_NEAR(mean / static_cast<double>(out.size()), 0.0, 0.02);
}

TEST(BrownModulation, LevelStaysRoughlyConstantAcrossTheCentreFrequency)
{
    // the compensation gain keeps the level within a factor of about two over three decades
    std::vector<double> levels;
    for (const float centre : {0.05f, 0.5f, 5.0f, 50.0f})
    {
        BrownModulation noise(1000.0f, 7u);
        noise.setCenterFrequency(centre);
        const auto out = run(noise, 400000);
        levels.push_back(rms(out, 20000));
    }
    const double lowest = *std::min_element(levels.begin(), levels.end());
    const double highest = *std::max_element(levels.begin(), levels.end());
    EXPECT_LT(highest / lowest, 2.5);
    EXPECT_GT(lowest, 0.01);
}

TEST(BrownModulation, MostOfTheEnergyIsBelowTheCentreFrequencyTimesTheRatio)
{
    // the one-pole lowpass at 14 f (pole 0.915 at 1 Hz and 1 kHz): the power of the sample-to-sample difference
    // relative to the signal is 2 (1 - lag-one correlation), about 0.17, where white noise gives 2
    BrownModulation noise(1000.0f, 3u);
    noise.setCenterFrequency(1.0f);
    const auto out = run(noise, 200000);
    double signalPower = 0.0;
    double differencePower = 0.0;
    for (size_t i = 1; i < out.size(); ++i)
    {
        signalPower += static_cast<double>(out[i]) * static_cast<double>(out[i]);
        const double difference = static_cast<double>(out[i]) - static_cast<double>(out[i - 1]);
        differencePower += difference * difference;
    }
    EXPECT_NEAR(differencePower / signalPower, 0.17, 0.03) << "a smooth, low-frequency signal";
}

TEST(BrownModulation, FollowsTheSeed)
{
    BrownModulation a(1000.0f, 5u);
    BrownModulation b(1000.0f, 5u);
    BrownModulation c(1000.0f, 6u);
    EXPECT_EQ(run(a, 1000), run(b, 1000));
    EXPECT_NE(run(b, 1000), run(c, 1000));
    // re-seeding restarts the white input, not the filter state: the same seed on a fresh generator and after
    // seed() give the same white values, so the first output differs only by the filter memory
    a.seed(7u);
    c.seed(7u);
    std::ignore = run(c, 1);
    EXPECT_NE(a.step(), 0.0f);
}

}
