#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <numeric>
#include <tuple>
#include <vector>

#include "dsp/Generators/OrnsteinUhlenbeck.h"

namespace jp::test
{

namespace
{
struct Moments
{
    double mean;
    double deviation;
};

Moments moments(const std::vector<float>& data)
{
    const double mean = std::accumulate(data.begin(), data.end(), 0.0) / static_cast<double>(data.size());
    double sum = 0.0;
    for (const float v : data)
    {
        sum += (v - mean) * (v - mean);
    }
    return {mean, std::sqrt(sum / static_cast<double>(data.size()))};
}

std::vector<float> run(OrnsteinUhlenbeck& process, const size_t burnIn, const size_t count)
{
    for (size_t i = 0; i < burnIn; ++i)
    {
        std::ignore = process.step();
    }
    std::vector<float> data(count);
    for (auto& v : data)
    {
        v = process.step();
    }
    return data;
}
}

// The first five tests are ported from AbacDsp test/Generators/OrnsteinUhlenbeckProcess_test.cpp.

TEST(OrnsteinUhlenbeck, ZeroSigmaProducesConstantOutput)
{
    OrnsteinUhlenbeck process(48000.0f, 42u);
    process.setSigma(0.0f);
    for (int i = 0; i < 4800; ++i)
    {
        ASSERT_NEAR(process.step(), 0.0f, 0.01f);
    }
}

TEST(OrnsteinUhlenbeck, HigherSigmaProducesLargerFluctuations)
{
    OrnsteinUhlenbeck process(48000.0f);
    process.reset(42u);
    process.setSigma(0.1f);
    const auto low = moments(run(process, 0, 4800));
    process.reset(42u);
    process.setSigma(0.8f);
    const auto high = moments(run(process, 0, 4800));
    EXPECT_GT(high.deviation, low.deviation);
}

TEST(OrnsteinUhlenbeck, ResetReproducesTheSequence)
{
    OrnsteinUhlenbeck process(48000.0f);
    process.setSigma(0.6f);
    for (int i = 0; i < 1234; ++i)
    {
        std::ignore = process.step();
    }
    process.reset(4242u);
    const float afterReset = process.step();
    process.reset(4242u);
    EXPECT_FLOAT_EQ(process.step(), afterReset);
}

TEST(OrnsteinUhlenbeck, OutputRemainsConfined)
{
    constexpr float sigma = 1.0f;
    OrnsteinUhlenbeck process(48000.0f, 789u);
    process.setSigma(sigma);
    for (int i = 0; i < 2000; ++i)
    {
        std::ignore = process.step();
    }
    // AbacDsp's bound: 3 sigma / sqrt(2 theta) around mu (wider than the true deviation)
    const float theta = sigma * 20.0f + 1.0f;
    const float bound = 3.0f * std::sqrt(sigma * sigma / (2.0f * theta));
    for (int i = 0; i < 4800 * 1000; ++i)
    {
        const float x = process.step();
        ASSERT_GT(x, sigma - bound) << "sample " << i;
        ASSERT_LT(x, sigma + bound) << "sample " << i;
    }
}

TEST(OrnsteinUhlenbeck, MeanRevertsToMu)
{
    OrnsteinUhlenbeck process(48000.0f, 42u);
    process.setSigma(0.7f);
    EXPECT_NEAR(moments(run(process, 4800 * 5, 4800 * 5)).mean, 0.7, 0.07);
}

TEST(OrnsteinUhlenbeck, SetValueMovesTheStateAndStepsContinueFromIt)
{
    OrnsteinUhlenbeck a(10.0f, 3u);
    OrnsteinUhlenbeck b(10.0f, 3u);
    a.setSigma(0.4f);
    b.setSigma(0.4f);
    a.setValue(a.getMu());
    EXPECT_EQ(a.getValue(), a.getMu());
    // same noise, different start: the difference decays by e^(-theta dt) per step
    const float decay = std::exp(-a.getTheta() / 10.0f);
    const float a1 = a.step();
    const float b1 = b.step();
    EXPECT_NEAR(a1 - b1, a.getMu() * decay, 1e-6f);
}

TEST(OrnsteinUhlenbeck, SameSeedGivesSameSequence)
{
    OrnsteinUhlenbeck a(100.0f, 7u);
    OrnsteinUhlenbeck b(100.0f, 7u);
    a.setSigma(0.5f);
    b.setSigma(0.5f);
    for (int i = 0; i < 1000; ++i)
    {
        ASSERT_EQ(a.step(), b.step());
    }
}

TEST(OrnsteinUhlenbeck, DifferentSeedsGiveDifferentSequences)
{
    OrnsteinUhlenbeck a(100.0f, 7u);
    OrnsteinUhlenbeck b(100.0f, 8u);
    a.setSigma(0.5f);
    b.setSigma(0.5f);
    int equal = 0;
    for (int i = 0; i < 1000; ++i)
    {
        equal += a.step() == b.step() ? 1 : 0;
    }
    EXPECT_LT(equal, 5);
}

class OrnsteinUhlenbeckStepRates : public ::testing::TestWithParam<float>
{
};

TEST_P(OrnsteinUhlenbeckStepRates, StationaryDistributionMatchesTheory)
{
    for (const float sigma : {0.2f, 0.5f, 1.0f})
    {
        OrnsteinUhlenbeck process(GetParam(), 11u);
        process.setSigma(sigma);
        // Statistics need many correlation times 1 / theta, not many samples: over K correlation
        // times the standard error of the mean is about sqrt(2 / K) and that of the deviation about
        // sqrt(1 / K) stationary deviations. K = 1000 gives 0.045 and 0.032; tolerances are ~4 of those.
        const float stepsPerCorrelationTime = GetParam() / process.getTheta();
        const auto burnIn = static_cast<size_t>(10.0f * stepsPerCorrelationTime) + 1;
        const auto count = std::max<size_t>(static_cast<size_t>(1000.0f * stepsPerCorrelationTime), 100000);
        const auto m = moments(run(process, burnIn, count));
        const double expected = process.getStationaryDeviation();
        EXPECT_NEAR(m.mean, process.getMu(), 0.2 * expected) << "sigma " << sigma;
        EXPECT_NEAR(m.deviation, expected, 0.12 * expected) << "sigma " << sigma;
    }
}

TEST_P(OrnsteinUhlenbeckStepRates, LagOneCorrelationIsExpOfMinusThetaDt)
{
    const float stepRate = GetParam();
    OrnsteinUhlenbeck process(stepRate, 3u);
    process.setSigma(0.5f);
    const auto data = run(process, 100000, 400000);
    const auto m = moments(data);
    double sum = 0.0;
    for (size_t i = 1; i < data.size(); ++i)
    {
        sum += (data[i] - m.mean) * (data[i - 1] - m.mean);
    }
    const double correlation = sum / static_cast<double>(data.size() - 1) / (m.deviation * m.deviation);
    EXPECT_NEAR(correlation, std::exp(-process.getTheta() / stepRate), 0.01);
}

// 7 steps per second is the sample-and-hold rate of shipahoy; the Euler step diverges there
INSTANTIATE_TEST_SUITE_P(StepRates, OrnsteinUhlenbeckStepRates, ::testing::Values(7.0f, 100.0f, 48000.0f));

TEST(OrnsteinUhlenbeck, SlowStepRateStaysStableForLargeSigma)
{
    // theta dt = 21 / 7 = 3: an Euler step would multiply the displacement by -2 each step
    OrnsteinUhlenbeck process(7.0f, 5u);
    process.setSigma(1.0f);
    const float limit = 8.0f * process.getStationaryDeviation();
    for (int i = 0; i < 100000; ++i)
    {
        ASSERT_LT(std::abs(process.step() - process.getMu()), limit + 1.0f) << "step " << i;
    }
}

}
