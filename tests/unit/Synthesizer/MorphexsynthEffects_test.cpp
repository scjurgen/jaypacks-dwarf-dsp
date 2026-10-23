#include <algorithm>
#include <cmath>
#include <functional>
#include <gtest/gtest.h>
#include <memory>
#include <vector>

#include "dsp/Numbers/Random.h"
#include "dsp/Synthesizer/MorphexsynthEffects.h"

namespace jp::test
{

namespace
{
constexpr float c_sampleRate{48000.0f};
constexpr size_t c_block{16};
using Effects = MorphexsynthEffects<c_block>;

struct Stereo
{
    std::vector<float> left;
    std::vector<float> right;
};

/// Runs the bus over numSamples (a multiple of the block) with the input given per sample and channel.
Stereo run(Effects& effects, const size_t numSamples, const std::function<float(size_t, int)>& input)
{
    Stereo out;
    std::array<float, c_block> left;
    std::array<float, c_block> right;
    for (size_t n = 0; n < numSamples; n += c_block)
    {
        for (size_t k = 0; k < c_block; ++k)
        {
            left[k] = input(n + k, 0);
            right[k] = input(n + k, 1);
        }
        effects.process(left.data(), right.data());
        out.left.insert(out.left.end(), left.begin(), left.end());
        out.right.insert(out.right.end(), right.begin(), right.end());
    }
    return out;
}

double rms(const std::vector<float>& x, const size_t from, const size_t to)
{
    double sum = 0.0;
    for (size_t i = from; i < to; ++i)
    {
        sum += static_cast<double>(x[i]) * static_cast<double>(x[i]);
    }
    return std::sqrt(sum / static_cast<double>(to - from));
}

float sine(const size_t n, const float frequency, const float amplitude = 0.3f)
{
    return amplitude * std::sin(2.0f * 3.14159265f * frequency * static_cast<float>(n) / c_sampleRate);
}

std::unique_ptr<Effects> makeEffects()
{
    return std::make_unique<Effects>(c_sampleRate);
}
}

TEST(MorphexsynthEffects, EverythingIsOffAtTheStart)
{
    auto effects = makeEffects();
    const auto out =
        run(*effects, 48000, [](size_t n, int channel) { return sine(n, channel == 0 ? 440.0f : 660.0f); });
    for (size_t n = 0; n < out.left.size(); ++n)
    {
        // the reverb is added at -100 dB (1e-5)
        ASSERT_NEAR(out.left[n], sine(n, 440.0f), 1e-4f) << n;
        ASSERT_NEAR(out.right[n], sine(n, 660.0f), 1e-4f) << n;
    }
}

TEST(MorphexsynthEffects, APhaserWithMixZeroPassesTheDrySignalExactly)
{
    auto effects = makeEffects();
    effects->setPhaser(2.0f, 1.0f, 0.3f, 0.0f);
    effects->setReverbMix(-200.0f);
    const auto out = run(*effects, 4800, [](size_t n, int) { return sine(n, 500.0f); });
    for (size_t n = 0; n < out.left.size(); ++n)
    {
        ASSERT_NEAR(out.left[n], sine(n, 500.0f), 1e-8f) << n;
    }
}

TEST(MorphexsynthEffects, ThePhaserSweepsItsNotchAcrossATone)
{
    auto effects = makeEffects();
    effects->setReverbMix(-200.0f);
    effects->setPhaser(1.0f, 1.0f, 0.0f, 1.0f);
    const auto out = run(*effects, 96000, [](size_t n, int) { return sine(n, 700.0f); });
    double lowest = 1e9;
    double highest = 0.0;
    for (size_t from = 24000; from + 2400 <= out.left.size(); from += 2400)
    {
        const double level = rms(out.left, from, from + 2400);
        lowest = std::min(lowest, level);
        highest = std::max(highest, level);
    }
    EXPECT_GT(highest / lowest, 3.0) << "the tone passes through a notch once or twice per sweep";
}

TEST(MorphexsynthEffects, ThePhaserSharesOneSweepOnBothChannels)
{
    auto effects = makeEffects();
    effects->setReverbMix(-200.0f);
    effects->setPhaser(1.0f, 1.0f, 0.0f, 1.0f);
    const auto out = run(*effects, 48000, [](size_t n, int) { return sine(n, 700.0f); });
    for (size_t n = 0; n < out.left.size(); ++n)
    {
        // the reverb's stereo taps differ, at 1e-10 of their level
        ASSERT_NEAR(out.left[n], out.right[n], 1e-8f) << n;
    }
}

TEST(MorphexsynthEffects, TheChorusDelaysByTheBaseDelayWithoutDepth)
{
    auto effects = makeEffects();
    effects->setReverbMix(-200.0f);
    effects->setChorus(0.6f, 0.0f, 1.0f);
    // the depth set first is applied after a full period of the sweep (1.67 s)
    (void) run(*effects, 96000, [](size_t, int) { return 0.0f; });
    const auto out = run(*effects, 3200, [](size_t n, int) { return n == 0 ? 1.0f : 0.0f; });
    for (size_t n = 0; n < out.left.size(); ++n)
    {
        ASSERT_NEAR(out.left[n], n == 864 ? 1.0f : 0.0f, 1e-6f) << n;
        ASSERT_NEAR(out.right[n], n == 864 ? 1.0f : 0.0f, 1e-6f) << n;
    }
}

TEST(MorphexsynthEffects, TheChorusSweepsTheChannelsInOppositePhase)
{
    auto effects = makeEffects();
    effects->setReverbMix(-200.0f);
    effects->setChorus(0.6f, 1.0f, 1.0f);
    const auto out = run(*effects, 48000 * 6, [](size_t n, int) { return sine(n, 1000.0f); });
    std::vector<float> difference;
    for (size_t n = 48000 * 3; n < out.left.size(); ++n)
    {
        difference.push_back(out.left[n] - out.right[n]);
    }
    EXPECT_GT(rms(difference, 0, difference.size()), 0.1);
}

TEST(MorphexsynthEffects, AMixThatMovesHasNoStep)
{
    // the chorus delays by 863 samples while its depth is 0: at 1028.9 Hz that is 18.5 periods, so the wet signal is
    // the dry one inverted. A mix that jumps from 0 to 1 at once steps the output by 0.6 (twice the amplitude);
    // smoothed over 10 ms the steps stay near the sine's own (0.3 * 2 pi * 1028.9 / 48000 = 0.04). The phaser's wet
    // signal differs from the dry one as well.
    constexpr float c_frequency{1028.9f};
    for (const bool chorus : {true, false})
    {
        auto effects = makeEffects();
        effects->setReverbMix(-200.0f);
        const auto set = [&effects, chorus](const float mix)
        {
            if (chorus)
            {
                effects->setChorus(0.6f, 0.0f, mix);
            }
            else
            {
                effects->setPhaser(0.6f, 0.5f, 0.0f, mix);
            }
        };
        set(0.0f);
        const auto before = run(*effects, 48000, [](size_t n, int) { return sine(n, c_frequency); });
        set(1.0f);
        const auto out = run(*effects, 4800, [](size_t n, int) { return sine(n + 48000, c_frequency); });
        float largest = std::abs(out.left[0] - before.left.back()); // the step is between the two runs
        for (size_t n = 1; n < out.left.size(); ++n)
        {
            largest = std::max(largest, std::abs(out.left[n] - out.left[n - 1]));
        }
        EXPECT_LT(largest, 0.15f) << (chorus ? "chorus" : "phaser");
    }
}

TEST(MorphexsynthEffects, ReverbTailDecaysByTheSetT60)
{
    auto effects = makeEffects();
    effects->setReverbDry(-200.0f);
    effects->setReverbMix(0.0f);
    effects->setReverbDecay(1500.0f);
    effects->setReverbSize(12.0f);
    const auto out = run(*effects, 96000, [](size_t n, int) { return n == 0 ? 1.0f : 0.0f; });
    const double early = rms(out.left, 9600, 14400);
    const double late = rms(out.left, 9600 + 48000, 14400 + 48000);
    const double dropDb = 20.0 * std::log10(late / early);
    EXPECT_NEAR(dropDb, -40.0, 6.0) << "one second at T60 1.5 s is 40 dB";
}

TEST(MorphexsynthEffects, TheMixAndTheDryAreGainsInDecibels)
{
    auto effects = makeEffects();
    effects->setReverbDry(-6.0206f);
    effects->setReverbMix(-200.0f);
    const auto out = run(*effects, 9600, [](size_t n, int) { return sine(n, 500.0f); });
    EXPECT_NEAR(rms(out.left, 4800, 9600) / (0.3 / std::sqrt(2.0)), 0.5, 0.01);

    auto wet = makeEffects();
    wet->setReverbDry(-200.0f);
    wet->setReverbMix(-100.0f);
    const auto quiet = run(*wet, 48000, [](size_t n, int) { return n == 0 ? 1.0f : 0.0f; });
    wet->setReverbMix(-20.0f);
    const auto loud = run(*wet, 48000, [](size_t n, int) { return n == 0 ? 1.0f : 0.0f; });
    EXPECT_GT(rms(loud.left, 4800, 48000), 1e3 * rms(quiet.left, 4800, 48000));
}

TEST(MorphexsynthEffects, ResetSilencesTheTails)
{
    auto effects = makeEffects();
    effects->setReverbDry(-200.0f);
    effects->setReverbMix(0.0f);
    effects->setChorus(0.6f, 0.5f, 1.0f);
    (void) run(*effects, 24000, [](size_t n, int) { return sine(n, 300.0f); });
    effects->reset();
    const auto out = run(*effects, 24000, [](size_t, int) { return 0.0f; });
    EXPECT_LT(rms(out.left, 0, out.left.size()), 1e-4);
}

TEST(MorphexsynthEffects, StressedWithRandomSettingsItStaysFiniteAndBounded)
{
    auto effects = makeEffects();
    Random random(7);
    float peak = 0.0f;
    for (int round = 0; round < 40; ++round)
    {
        effects->setPhaser(5.0f * random.uniform(), random.uniform(), 0.5f * random.uniform(), random.uniform());
        effects->setChorus(5.0f * random.uniform(), random.uniform(), random.uniform());
        effects->setReverbSize(1.0f + 59.0f * random.uniform());
        effects->setReverbDecay(100.0f + 5000.0f * random.uniform());
        effects->setReverbMix(-30.0f * random.uniform());
        const auto out =
            run(*effects, 4800, [&](size_t n, int channel) { return sine(n, channel == 0 ? 220.0f : 330.0f, 0.5f); });
        for (const float v : out.left)
        {
            ASSERT_TRUE(std::isfinite(v));
            peak = std::max(peak, std::abs(v));
        }
    }
    EXPECT_LT(peak, 20.0f);
}

}
