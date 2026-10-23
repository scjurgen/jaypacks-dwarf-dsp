#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <iostream>
#include <vector>

#include "tests/unit/TestHelpers.h"
#include "dsp/Analysis/ZeroCrossingFrequency.h"
#include "dsp/Delays/PitchFadeWindowDelay.h"

namespace jp::test
{

namespace
{
constexpr size_t c_maxSize{4800};
constexpr float c_sampleRate{48000.0f};
// 4800 samples hold a whole number of periods of e.g. 220 Hz, which would make any
// buffer-length jump land on the same phase and hide the defect under test
constexpr float c_probe{237.0f};

struct Config
{
    float semitones{0.0f};
    bool reverse{false};
    size_t window{c_maxSize / 2};
    size_t fadeTime{c_maxSize / 2};
};

[[nodiscard]] float sineAt(const float frequency, const size_t i)
{
    return static_cast<float>(std::sin(2.0 * c_pi * frequency * static_cast<double>(i) / c_sampleRate));
}

[[nodiscard]] std::vector<float> render(const Config& cfg, const float frequency, const size_t numSamples,
                                        const uint64_t seed = Random::c_defaultSeed)
{
    PitchFadeWindowDelay<c_maxSize> sut(seed);
    sut.setSize(cfg.window);
    sut.setFadeTime(cfg.fadeTime);
    sut.setReverse(cfg.reverse);
    sut.setPitch(cfg.semitones);
    std::vector<float> out(numSamples);
    for (size_t i = 0; i < numSamples; ++i)
    {
        out[i] = sut.step(sineAt(frequency, i));
    }
    return out;
}

// A pitch shifted sine is still a sine, so its sample-to-sample step cannot exceed the
// slope of a sine at the shifted frequency; anything beyond is a discontinuity, a click.
[[nodiscard]] float worstJump(const std::vector<float>& out)
{
    float worst = 0.0f;
    // skip the start: the buffer holds silence until it has been filled once
    for (size_t i = c_maxSize + 1; i < out.size(); ++i)
    {
        worst = std::max(worst, std::fabs(out[i] - out[i - 1]));
    }
    return worst;
}

[[nodiscard]] float maxSineSlope(const float semitones)
{
    const auto ratio = std::pow(2.0f, semitones / 12.0f);
    return static_cast<float>(2.0 * c_pi * c_probe * std::max(ratio, 1.0f) / c_sampleRate);
}
}

TEST(PitchFadeWindowDelay, ForwardShiftIsFreeOfDiscontinuities)
{
    for (const float semitones : {-12.0f, -5.0f, 0.0f, 7.0f, 12.0f})
    {
        const auto out = render({semitones, false}, c_probe, 96000);
        EXPECT_LT(worstJump(out), maxSineSlope(semitones) * 1.5f) << "semitones " << semitones;
    }
}

TEST(PitchFadeWindowDelay, ReverseShiftIsFreeOfDiscontinuities)
{
    for (const float semitones : {-12.0f, 0.0f, 12.0f})
    {
        const auto out = render({semitones, true}, c_probe, 96000);
        EXPECT_LT(worstJump(out), maxSineSlope(semitones) * 1.5f) << "semitones " << semitones;
    }
}

TEST(PitchFadeWindowDelay, DefaultGeometryIsFreeOfDiscontinuities)
{
    PitchFadeWindowDelay<c_maxSize> sut;
    sut.setPitch(12.0f);
    std::vector<float> out(96000);
    for (size_t i = 0; i < out.size(); ++i)
    {
        out[i] = sut.step(sineAt(c_probe, i));
    }
    EXPECT_LT(worstJump(out), maxSineSlope(12.0f) * 1.5f);
}

TEST(PitchFadeWindowDelay, DefaultIsUnityPitch)
{
    // without setPitch() the shifter passes the signal at its own pitch (AbacDsp started at
    // a read rate of 0.001)
    PitchFadeWindowDelay<c_maxSize> sut;
    ZeroCrossingFrequency zc(c_sampleRate, 0.1f);
    for (size_t i = 0; i < 96000; ++i)
    {
        const float y = sut.step(sineAt(c_probe, i));
        if (i > c_maxSize)
        {
            zc.add(y);
        }
    }
    EXPECT_NEAR(zc.frequency(), c_probe, 1.0);
}

TEST(PitchFadeWindowDelay, FadeTimeLargerThanWindowStillProducesGrains)
{
    PitchFadeWindowDelay<c_maxSize> sut;
    sut.setSize(600);
    sut.setFadeTime(c_maxSize);
    sut.setPitch(7.0f);
    for (size_t i = 0; i < c_maxSize * 4; ++i)
    {
        const auto value = sut.step(1.0f);
        ASSERT_TRUE(std::isfinite(value));
        ASSERT_LE(std::fabs(value), 1.5f) << "at sample " << i;
    }
}

TEST(PitchFadeWindowDelay, GrainHandoffAddsNoBroadbandSpray)
{
    // a 200 Hz tone must not gain broadband content: a stalled handoff measures about
    // -60 dB above 5 kHz, a clean one about -89 dB
    constexpr size_t fftSize = 16384;
    for (const float semitones : {0.0f, 7.0f})
    {
        const auto out = render({semitones, false}, 200.0f, c_maxSize * 2 + fftSize);
        const std::vector<float> settled(out.end() - static_cast<long>(fftSize), out.end());
        const double energy = highBandEnergyDb(settled, 5000.0, c_sampleRate);
        std::cout << "[ pitch    ] " << semitones << " st: energy above 5 kHz " << energy << " dB\n";
        EXPECT_LT(energy, -80.0) << semitones << " semitones";
    }
}

TEST(PitchFadeWindowDelay, UnityPitchForwardReproducesInput)
{
    const auto out = render({0.0f, false, c_maxSize / 2, c_maxSize / 8}, c_probe, 48000);
    float worst = 0.0f;
    for (size_t i = c_maxSize + 1; i < out.size(); ++i)
    {
        worst = std::max(worst, std::fabs(out[i]));
    }
    EXPECT_GT(worst, 0.9f);
    EXPECT_LT(worst, 1.1f);
}

TEST(PitchFadeWindowDelay, ChangingPitchWhileRunningStaysBounded)
{
    PitchFadeWindowDelay<c_maxSize> sut;
    sut.setSize(c_maxSize / 2);
    sut.setFadeTime(c_maxSize / 4);
    for (size_t i = 0; i < 48000; ++i)
    {
        sut.setPitch(12.0f * std::sin(static_cast<float>(i) / 4000.0f));
        const auto value = sut.step(sineAt(c_probe, i));
        ASSERT_TRUE(std::isfinite(value));
        ASSERT_LE(std::fabs(value), 1.5f) << "at sample " << i;
    }
}

TEST(PitchFadeWindowDelay, IntervalWithinEightCents)
{
    // the spectral centroid within +/-10 % of 2.7 s of settled output: the grains
    // spread the carrier over about the grain rate with random fine structure, so neither
    // the highest bin nor zero crossings (missed where two heads at random phase cancel
    // in the crossfade) measure the pitch; the grain resets add a small bias (see the header).
    // Measured +0.9 .. +6.0 cents over -12 .. +12 semitones.
    constexpr size_t fftSize = 131072;
    for (const float semitones : {-12.0f, -5.0f, 7.0f, 12.0f})
    {
        const auto out = render({semitones, false}, c_probe, fftSize + c_maxSize * 2);
        const std::vector<float> settled(out.end() - static_cast<long>(fftSize), out.end());
        const double expected = c_probe * std::pow(2.0, semitones / 12.0);
        const double measured = spectralCentroid(settled, c_sampleRate, expected * 0.9, expected * 1.1);
        const double cents = 1200.0 * std::log2(measured / expected);
        std::cout << "[ pitch    ] " << semitones << " st: " << measured << " Hz, " << cents << " cents\n";
        EXPECT_LT(std::fabs(cents), 8.0) << semitones << " semitones";
    }
}

TEST(PitchFadeWindowDelay, SameSeedSameOutputOtherSeedDiffers)
{
    const Config cfg{7.0f, false};
    const auto a = render(cfg, c_probe, 20000, 1);
    const auto b = render(cfg, c_probe, 20000, 1);
    const auto c = render(cfg, c_probe, 20000, 2);
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

}
