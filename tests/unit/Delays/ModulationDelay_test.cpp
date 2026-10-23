#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <vector>

#include "dsp/Delays/ModulationDelay.h"

namespace jp::test
{

namespace
{
constexpr float c_sampleRate{48000.0f};
using Delay = ModulationDelay<4096>;

/// The delay in samples at every step, read from a ramp input (output n - delay).
std::vector<float> delaysOfRamp(Delay& delay, const size_t numSamples, const size_t start = 0)
{
    std::vector<float> delays;
    for (size_t n = start; n < start + numSamples; ++n)
    {
        const float y = delay.process(static_cast<float>(n));
        delays.push_back(static_cast<float>(n) - y);
    }
    return delays;
}
}

TEST(ModulationDelay, WithoutModulationTheDelayIsTheWidthInWholeSamples)
{
    Delay delay(c_sampleRate);
    delay.setWidthSamples(300);
    delay.setModDepth(0.0f);
    // the first depth waits for a full period of the sweep: 200 ticks of 16 samples at the default rate
    for (int n = 0; n < 4000; ++n)
    {
        (void) delay.process(0.0f);
    }
    std::vector<float> out;
    for (int n = 0; n < 600; ++n)
    {
        out.push_back(delay.process(n == 0 ? 1.0f : 0.0f));
    }
    for (int n = 0; n < 600; ++n)
    {
        EXPECT_FLOAT_EQ(out[static_cast<size_t>(n)], n == 300 ? 1.0f : 0.0f) << n;
    }
}

TEST(ModulationDelay, TheWidthIsInMillisecondsAndLimited)
{
    Delay delay(c_sampleRate);
    delay.setWidthMs(18.0f);
    EXPECT_EQ(delay.getWidth(), 864u);
    delay.setWidthMs(0.1f);
    EXPECT_EQ(delay.getWidth(), 48u);
    delay.setWidthMs(1000.0f);
    EXPECT_EQ(delay.getWidth(), 4095u);
}

TEST(ModulationDelay, TheSweepStaysBetweenTheWidthMinusOneMinusDepthAndTheWidthMinusOne)
{
    Delay delay(c_sampleRate);
    delay.setWidthSamples(864);
    delay.setModSpeed(2.0f);
    delay.setModDepth(0.3f);
    const auto delays = delaysOfRamp(delay, 96000);
    // the depth (150 samples) is applied after a full period (0.5 s = 24000 samples) from the start
    const auto settled = std::vector<float>(delays.begin() + 25000, delays.end());
    const auto [low, high] = std::minmax_element(settled.begin(), settled.end());
    EXPECT_NEAR(*high, 863.0f, 0.05f);
    EXPECT_NEAR(*low, 863.0f - 150.0f, 0.5f);
}

TEST(ModulationDelay, TheSweepRepeatsAtTheSpeed)
{
    Delay delay(c_sampleRate);
    delay.setWidthSamples(864);
    delay.setModSpeed(3.0f);
    delay.setModDepth(0.2f);
    const auto delays = delaysOfRamp(delay, 96000);
    // peaks (the longest delay, the phase at the wrap) three times per second: 16000 samples apart
    std::vector<size_t> peaks;
    for (size_t n = 30000; n + 1 < delays.size(); ++n)
    {
        if (delays[n] > 862.9f && delays[n - 1] <= delays[n] && delays[n + 1] < delays[n])
        {
            if (peaks.empty() || n - peaks.back() > 4000)
            {
                peaks.push_back(n);
            }
        }
    }
    ASSERT_GE(peaks.size(), 3u);
    EXPECT_NEAR(static_cast<double>(peaks[1] - peaks[0]), 16000.0, 40.0);
}

TEST(ModulationDelay, TheDelayMovesInStepsOncePerTickAndEachStepIsSmall)
{
    Delay delay(c_sampleRate);
    delay.setWidthSamples(864);
    delay.setModSpeed(1.0f);
    delay.setModDepth(0.6f);
    const auto delays = delaysOfRamp(delay, 144000);
    float largestStep = 0.0f;
    for (size_t n = 30000; n < delays.size(); ++n)
    {
        largestStep = std::max(largestStep, std::abs(delays[n] - delays[n - 1]));
    }
    // 300 samples of sweep, 1 Hz: the largest step is 300 pi / 2 * (2 16 / 48000) = 0.314 samples every 16 samples
    // (the ramp is read in float at values near 1e5, which adds up to 0.016)
    EXPECT_LT(largestStep, 0.34f);
    EXPECT_GT(largestStep, 0.29f);
}

TEST(ModulationDelay, ANewDepthWaitsForTheZeroOfTheSweepAndDoesNotStep)
{
    Delay delay(c_sampleRate);
    delay.setWidthSamples(864);
    delay.setModSpeed(2.0f);
    delay.setModDepth(0.1f);
    (void) delaysOfRamp(delay, 48000);
    delay.setModDepth(0.6f);
    const auto more = delaysOfRamp(delay, 144000, 48000);
    float largestStep = 0.0f;
    for (size_t n = 1; n < more.size(); ++n)
    {
        largestStep = std::max(largestStep, std::abs(more[n] - more[n - 1]));
    }
    EXPECT_LT(largestStep, 0.65f)
        << "the steps of a 2 Hz sweep of 300 samples (0.63), no larger one when the depth arrives";
    const auto [low, high] = std::minmax_element(more.begin() + 100000, more.end());
    EXPECT_NEAR(*high - *low, 300.0f, 1.0f);
}

TEST(ModulationDelay, ResetClearsTheLine)
{
    Delay delay(c_sampleRate);
    delay.setWidthSamples(200);
    for (int n = 0; n < 150; ++n)
    {
        (void) delay.process(1.0f);
    }
    delay.reset();
    for (int n = 0; n < 400; ++n)
    {
        EXPECT_EQ(delay.process(0.0f), 0.0f) << n;
    }
}

TEST(ModulationDelay, ABlockEqualsSamples)
{
    Delay a(c_sampleRate);
    Delay b(c_sampleRate);
    for (auto* delay : {&a, &b})
    {
        delay->setWidthSamples(500);
        delay->setModSpeed(0.7f);
        delay->setModDepth(0.4f);
    }
    std::vector<float> in(5000);
    std::vector<float> out(5000);
    for (size_t i = 0; i < in.size(); ++i)
    {
        in[i] = std::sin(0.03f * static_cast<float>(i));
    }
    a.process(in.data(), out.data(), in.size());
    for (size_t i = 0; i < in.size(); ++i)
    {
        EXPECT_FLOAT_EQ(out[i], b.process(in[i]));
    }
}

}
