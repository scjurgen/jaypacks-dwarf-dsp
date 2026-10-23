#include <algorithm>
#include <cmath>
#include <complex>
#include <gtest/gtest.h>
#include <vector>

#include "tests/unit/TestHelpers.h"
#include "dsp/Filters/Phaser24.h"

namespace jp::test
{

namespace
{
constexpr float c_sampleRate{48000.0f};

Phaser24 makeFilter(const float cutoff, const float resonance = 0.0f)
{
    Phaser24 filter(c_sampleRate);
    filter.setCutoff(cutoff);
    filter.setResonance(resonance);
    return filter;
}

/// |1 - 4 L + 12 L^2 - 16 L^3 + 8 L^4| with L = (1 - p) / (1 - p z^-1), the small-signal response without resonance.
double theoryDb(const float poleValue, const double frequency)
{
    const double pole = poleValue;
    const auto z = std::polar(1.0, -2.0 * c_pi * frequency / c_sampleRate);
    const auto stage = (1.0 - pole) / (1.0 - pole * z);
    const auto response =
        1.0 - 4.0 * stage + 12.0 * stage * stage - 16.0 * stage * stage * stage + 8.0 * stage * stage * stage * stage;
    return toDb(std::abs(response));
}

double measuredDb(Phaser24& filter, const double frequency)
{
    return toDb(measureSineGain([&filter](const float x) { return filter.process(x); }, frequency, c_sampleRate));
}
}

TEST(Phaser24, SmallSignalResponseFollowsTheMixOfTheStages)
{
    for (const float cutoff : {300.0f, 1000.0f, 4000.0f})
    {
        auto filter = makeFilter(cutoff);
        const float pole = filter.getPole();
        for (const double frequency : {50.0, 200.0, 500.0, 1000.0, 2000.0, 6000.0, 15000.0})
        {
            EXPECT_NEAR(measuredDb(filter, frequency), theoryDb(pole, frequency), 0.1) << cutoff << " " << frequency;
        }
    }
}

TEST(Phaser24, TheGainIsOneAtDcWithoutResonance)
{
    auto filter = makeFilter(1000.0f);
    EXPECT_NEAR(measuredDb(filter, 20.0), 0.0, 0.1);
}

TEST(Phaser24, ANotchLiesNearTheCutoff)
{
    auto filter = makeFilter(1000.0f);
    double deepest = 0.0;
    for (double frequency = 200.0; frequency < 5000.0; frequency *= 1.05)
    {
        deepest = std::min(deepest, measuredDb(filter, frequency));
    }
    EXPECT_LT(deepest, -20.0);
}

TEST(Phaser24, TheInputIsSaturatedByAtan)
{
    // at DC the mix of settled stages is the feed itself: atan(x)
    auto filter = makeFilter(1000.0f);
    float y = 0.0f;
    for (int i = 0; i < 48000; ++i)
    {
        y = filter.process(3.0f);
    }
    EXPECT_NEAR(y, std::atan(3.0f), 1e-4f);
}

TEST(Phaser24, ResonanceTakesAwayTheLowEndRaisesTheCutoffAndStaysBounded)
{
    auto flat = makeFilter(1000.0f, 0.0f);
    auto resonant = makeFilter(1000.0f, 0.5f);
    EXPECT_LT(measuredDb(resonant, 20.0), measuredDb(flat, 20.0) - 9.0);
    EXPECT_GT(measuredDb(resonant, 1000.0), measuredDb(flat, 1000.0) + 4.0);

    auto loud = makeFilter(1000.0f, 1.0f);
    float peak = 0.0f;
    for (int i = 0; i < 48000; ++i)
    {
        const float y = loud.process(i < 100 ? 1.0f : 0.0f);
        ASSERT_TRUE(std::isfinite(y));
        peak = std::max(peak, std::abs(y));
    }
    EXPECT_LT(peak, 64.0f);
}

TEST(Phaser24, CutoffChangesMoveThePoleInEqualSteps)
{
    Phaser24 filter(c_sampleRate);
    filter.setSmoothingSteps(16);
    filter.setCutoff(500.0f);
    std::vector<float> in(16, 0.0f);
    std::vector<float> out(16);
    filter.process(in.data(), out.data(), 16);
    const float start = filter.getPole();
    filter.setCutoff(4000.0f);
    filter.process(in.data(), out.data(), 8);
    const float middle = filter.getPole();
    filter.process(in.data(), out.data(), 8);
    const float end = filter.getPole();
    Phaser24 target(c_sampleRate);
    target.setCutoff(4000.0f);
    EXPECT_FLOAT_EQ(end, target.getPole());
    EXPECT_NEAR(middle, 0.5f * (start + end), 1e-5f);
    EXPECT_LT(end, start);
}

TEST(Phaser24, ARequestForTheCutoffItAlreadyHasDoesNotRestartTheRamp)
{
    Phaser24 filter(c_sampleRate);
    filter.setSmoothingSteps(16);
    filter.setCutoff(500.0f);
    std::vector<float> in(16, 0.0f);
    std::vector<float> out(16);
    filter.process(in.data(), out.data(), 16);
    filter.setCutoff(4000.0f);
    filter.process(in.data(), out.data(), 8);
    filter.setCutoff(4000.0f);
    filter.process(in.data(), out.data(), 8);
    Phaser24 target(c_sampleRate);
    target.setCutoff(4000.0f);
    EXPECT_FLOAT_EQ(filter.getPole(), target.getPole());
}

TEST(Phaser24, ZeroStepsJumpAtOnce)
{
    Phaser24 filter(c_sampleRate);
    filter.setCutoff(4000.0f);
    Phaser24 other(c_sampleRate);
    other.setCutoff(4000.0f);
    EXPECT_FLOAT_EQ(filter.getPole(), other.getPole());
}

TEST(Phaser24, TheCutoffIsWarpedAt48kHzAndLimited)
{
    const auto poleFor = [](const float sampleRate, const float cutoff)
    {
        Phaser24 filter(sampleRate);
        filter.setCutoff(cutoff);
        return filter.getPole();
    };
    const float x = 1000.0f;
    const float warped = 0.1409743683f + 0.9999793344f * x - 6.203395634e-05f * x * x + 2.855230937e-09f * x * x * x;
    EXPECT_NEAR(poleFor(48000.0f, x), std::exp(-2.0f * 3.14159265f * warped / 48000.0f), 1e-6f);
    EXPECT_NEAR(poleFor(32000.0f, x), std::exp(-2.0f * 3.14159265f * x / 32000.0f), 1e-6f);
    EXPECT_FLOAT_EQ(poleFor(48000.0f, 3.0f), poleFor(48000.0f, 10.0f - 0.14f));
    EXPECT_NEAR(poleFor(48000.0f, 30000.0f), std::exp(-2.0f * 3.14159265f * 22000.0f / 48000.0f), 1e-6f);
}

TEST(Phaser24, ResetClearsTheState)
{
    auto filter = makeFilter(1000.0f, 0.3f);
    for (int i = 0; i < 100; ++i)
    {
        (void) filter.process(0.5f);
    }
    filter.reset();
    auto fresh = makeFilter(1000.0f, 0.3f);
    for (int i = 0; i < 50; ++i)
    {
        const float x = 0.3f * std::sin(0.2f * static_cast<float>(i));
        EXPECT_FLOAT_EQ(filter.process(x), fresh.process(x));
    }
}

TEST(Phaser24, ABlockEqualsSamplesAlsoAcrossARamp)
{
    Phaser24 a(c_sampleRate);
    Phaser24 b(c_sampleRate);
    for (auto* filter : {&a, &b})
    {
        filter->setSmoothingSteps(40);
        filter->setCutoff(500.0f);
        filter->setResonance(0.1f);
    }
    std::vector<float> in(64);
    std::vector<float> out(64);
    for (size_t i = 0; i < in.size(); ++i)
    {
        in[i] = 0.4f * std::sin(0.13f * static_cast<float>(i));
    }
    a.setCutoff(3000.0f);
    b.setCutoff(3000.0f);
    a.process(in.data(), out.data(), 24);
    a.process(in.data() + 24, out.data() + 24, 40);
    // the second filter takes it in blocks of 8, which split the ramp differently
    for (size_t i = 0; i < 64; i += 8)
    {
        std::vector<float> chunk(8);
        b.process(in.data() + i, chunk.data(), 8);
        for (size_t k = 0; k < 8; ++k)
        {
            EXPECT_FLOAT_EQ(chunk[k], out[i + k]) << i + k;
        }
    }
}

}
