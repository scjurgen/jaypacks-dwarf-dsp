#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <vector>

#include "tests/unit/TestHelpers.h"
#include "dsp/Filters/PoleMixingBandpassFilter.h"

namespace jp::test
{

namespace
{
constexpr float c_sampleRate{48000.0f};

PoleMixingBandpassFilter makeFilter(const float cutoff, const float resonance, const size_t preset = 3)
{
    PoleMixingBandpassFilter filter(c_sampleRate);
    filter.setPreset(preset);
    filter.setCutoffFrequency(cutoff);
    filter.setResonance(resonance);
    filter.reset();
    return filter;
}

double gainDb(const float cutoff, const float resonance, const double frequency, const size_t preset = 3)
{
    auto filter = makeFilter(cutoff, resonance, preset);
    return toDb(measureSineGain([&filter](const float x) { return filter.process(x); }, frequency, c_sampleRate));
}
}

TEST(PoleMixingBandpassFilter, FourPoleLowpassWithoutResonanceFallsByTwentyFourDbPerOctaveAboveTheCorner)
{
    EXPECT_NEAR(gainDb(1000.0f, 0.0f, 50.0), -0.04, 0.05);
    EXPECT_NEAR(gainDb(1000.0f, 0.0f, 2000.0), -27.3, 0.2);
    EXPECT_NEAR(gainDb(1000.0f, 0.0f, 8000.0), -70.2, 0.5);
}

TEST(PoleMixingBandpassFilter, ResonancePeaksAtTheCutoffAndLeavesThePassBandAlone)
{
    // the pass band falls by 0.1 dB at 0.9 of the threshold (the stage-4 feedback of PoleMixingFilter takes 5 dB)
    EXPECT_NEAR(gainDb(1000.0f, 0.9f, 50.0), gainDb(1000.0f, 0.0f, 50.0), 0.15);
    EXPECT_NEAR(gainDb(1000.0f, 0.9f, 1000.0), 7.5, 0.3);
    EXPECT_GT(gainDb(1000.0f, 0.9f, 1000.0), gainDb(1000.0f, 0.5f, 1000.0) + 10.0);
    EXPECT_NEAR(gainDb(1000.0f, 0.9f, 8000.0), gainDb(1000.0f, 0.0f, 8000.0), 1.0);
}

TEST(PoleMixingBandpassFilter, AboveTheThresholdItOscillatesBounded)
{
    auto filter = makeFilter(1000.0f, 1.3f);
    float peak = 0.0f;
    for (int i = 0; i < 48000; ++i)
    {
        const float x = i < 16 ? 0.1f : 0.0f;
        const float y = filter.process(x);
        ASSERT_TRUE(std::isfinite(y));
        if (i > 24000)
        {
            peak = std::max(peak, std::abs(y));
        }
    }
    EXPECT_GT(peak, 0.1f);
    EXPECT_LT(peak, 4.0f);
}

TEST(PoleMixingBandpassFilter, BelowTheThresholdItRingsOutAfterAnImpulse)
{
    auto filter = makeFilter(1000.0f, 0.8f);
    (void) filter.process(1.0f);
    float last = 0.0f;
    for (int i = 0; i < 96000; ++i)
    {
        last = filter.process(0.0f);
    }
    EXPECT_LT(std::abs(last), 1e-6f);
}

TEST(PoleMixingBandpassFilter, PresetsMixTheStagesWithAlternatingSigns)
{
    // "2 pole hp" {1, {2, 1, 0, 0}} passes highs and stops lows by 12 dB per octave
    const size_t hp2 = findPoleMixingPreset("2 pole hp");
    EXPECT_LT(gainDb(1000.0f, 0.0f, 100.0, hp2), -30.0);
    EXPECT_NEAR(gainDb(1000.0f, 0.0f, 15000.0, hp2), -1.2, 0.3);
    EXPECT_EQ(makeFilter(1000.0f, 0.0f, hp2).getPresetIndex(), hp2);
}

TEST(PoleMixingBandpassFilter, APresetOutsideTheTableIsIgnored)
{
    auto filter = makeFilter(1000.0f, 0.0f, 5);
    filter.setPreset(c_poleMixingPresetCount);
    EXPECT_EQ(filter.getPresetIndex(), 5u);
}

TEST(PoleMixingBandpassFilter, PoleAndResonanceFollowTheirTargetsSmoothly)
{
    auto filter = makeFilter(200.0f, 0.0f);
    filter.setSmoothingMs(3.0f);
    filter.setCutoffFrequency(4000.0f);
    // a sine at 1 kHz: the gain rises from the 200 Hz level to the 4 kHz one over about 3 ms (144 samples) per e-fold,
    // without a step in the first sample
    std::vector<float> out;
    for (int i = 0; i < 2400; ++i)
    {
        out.push_back(
            filter.process(0.5f * std::sin(2.0f * 3.14159265f * 1000.0f * static_cast<float>(i) / c_sampleRate)));
    }
    float early = 0.0f;
    float late = 0.0f;
    for (size_t i = 0; i < 48; ++i)
    {
        early = std::max(early, std::abs(out[i]));
        late = std::max(late, std::abs(out[2352 + i]));
    }
    EXPECT_LT(early, 0.5f * late);
    EXPECT_GT(late, 0.4f);
}

TEST(PoleMixingBandpassFilter, ResetClearsTheStagesAndSnapsToTheTargets)
{
    auto filter = makeFilter(1000.0f, 0.9f);
    for (int i = 0; i < 1000; ++i)
    {
        (void) filter.process(0.5f);
    }
    filter.setCutoffFrequency(5000.0f);
    filter.reset();
    PoleMixingBandpassFilter fresh(c_sampleRate);
    fresh.setCutoffFrequency(5000.0f);
    fresh.setResonance(0.9f);
    fresh.reset();
    for (int i = 0; i < 100; ++i)
    {
        const float x = 0.3f * std::sin(0.2f * static_cast<float>(i));
        EXPECT_FLOAT_EQ(filter.process(x), fresh.process(x));
    }
}

TEST(PoleMixingBandpassFilter, BlockProcessingEqualsSampleProcessing)
{
    auto a = makeFilter(700.0f, 0.7f);
    auto b = makeFilter(700.0f, 0.7f);
    std::vector<float> in(256);
    std::vector<float> out(256);
    for (size_t i = 0; i < in.size(); ++i)
    {
        in[i] = 0.4f * std::sin(0.13f * static_cast<float>(i));
    }
    a.process(in.data(), out.data(), in.size());
    for (size_t i = 0; i < in.size(); ++i)
    {
        EXPECT_FLOAT_EQ(out[i], b.process(in[i]));
    }
}

}
