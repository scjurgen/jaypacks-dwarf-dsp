#include <algorithm>
#include <cmath>
#include <complex>
#include <gtest/gtest.h>
#include <limits>
#include <tuple>
#include <vector>

#include "dsp/Analysis/ZeroCrossingFrequency.h"
#include "dsp/Generators/SynthLfo.h"

namespace jp::test
{

namespace
{
constexpr float c_rate{3000.0f}; // the tick rate of a control-rate LFO

std::vector<float> run(LfoGenerators& lfo, const size_t length)
{
    std::vector<float> out(length);
    for (float& x : out)
    {
        x = lfo.step();
    }
    return out;
}
}

TEST(SynthLfo, SineRunsAtTheSetFrequencyExactly)
{
    for (const float frequency : {0.05f, 0.7f, 5.0f, 40.0f})
    {
        LfoGenerators lfo(c_rate);
        lfo.setWaveForm(LfoType::Sine);
        lfo.setFrequency(frequency);
        ZeroCrossingFrequency crossings(c_rate, 0.05f);
        for (const float x : run(lfo, static_cast<size_t>(c_rate * 300.0f / frequency / 10.0f) + 5000))
        {
            crossings.add(x);
        }
        EXPECT_NEAR(crossings.frequency(), static_cast<double>(frequency), 1e-4 * static_cast<double>(frequency))
            << frequency;
    }
}

TEST(SynthLfo, TheWaveformsHaveTheirShapesOverOneCycle)
{
    // 100 ticks per cycle: tick k (counted from 1) is at phase k / 100
    LfoGenerators lfo(c_rate);
    lfo.setFrequency(30.0f);
    lfo.setWaveForm(LfoType::Triangle);
    auto triangle = run(lfo, 100);
    EXPECT_NEAR(triangle[24], 0.0f, 1e-5f) << "phase 0.25 crosses zero rising";
    EXPECT_NEAR(triangle[49], 1.0f, 1e-5f) << "phase 0.5 is the peak";
    EXPECT_NEAR(triangle[74], 0.0f, 1e-5f);
    EXPECT_NEAR(triangle[99], -1.0f, 1e-5f) << "phase 1 (= 0) is the trough";
    lfo.setPhase(0.0f);
    lfo.setWaveForm(LfoType::Saw);
    const auto saw = run(lfo, 101);
    EXPECT_NEAR(saw[49], 0.0f, 1e-5f);
    EXPECT_NEAR(saw[98], 0.98f, 1e-5f);
    EXPECT_NEAR(saw[100], -0.98f, 1e-5f) << "wraps at the end of the cycle";
    lfo.setPhase(0.0f);
    lfo.setWaveForm(LfoType::Square);
    const auto square = run(lfo, 101);
    EXPECT_EQ(square[0], 1.0f);
    EXPECT_EQ(square[48], 1.0f);
    EXPECT_EQ(square[50], -1.0f) << "the second half starts at phase 0.5";
    EXPECT_EQ(square[98], -1.0f);
    EXPECT_EQ(square[100], 1.0f);
    lfo.setPhase(0.0f);
    lfo.setWaveForm(LfoType::Sine);
    const auto sine = run(lfo, 100);
    EXPECT_NEAR(sine[24], 1.0f, 1e-5f);
    EXPECT_NEAR(sine[74], -1.0f, 1e-5f);
}

TEST(SynthLfo, EveryWaveformStaysWithinMinusOneAndOne)
{
    for (const LfoType type : {LfoType::Sine, LfoType::Triangle, LfoType::Saw, LfoType::Square, LfoType::Noise,
                               LfoType::SampleHoldNoise, LfoType::SampleHoldFlipFlop})
    {
        LfoGenerators lfo(c_rate);
        lfo.setWaveForm(type);
        lfo.setFrequency(13.0f);
        for (const float x : run(lfo, 50000))
        {
            ASSERT_GE(x, -1.0001f) << static_cast<int>(type);
            ASSERT_LE(x, 1.0001f) << static_cast<int>(type);
        }
    }
}

TEST(SynthLfo, SampleAndHoldHoldsTwoValuesPerCycle)
{
    LfoGenerators lfo(c_rate);
    lfo.setWaveForm(LfoType::SampleHoldNoise);
    lfo.setFrequency(10.0f); // 300 ticks per cycle, a new value every 150
    const auto out = run(lfo, 3000);
    int changes = 0;
    for (size_t i = 1; i < out.size(); ++i)
    {
        changes += out[i] != out[i - 1] ? 1 : 0;
    }
    EXPECT_NEAR(changes, 20, 1) << "10 cycles, two values each";
    for (size_t block = 1; block < 19; ++block)
    {
        EXPECT_EQ(out[block * 150 + 10], out[block * 150 + 135]) << "constant inside a hold";
    }
}

TEST(SynthLfo, NoiseIsUniformAndWhite)
{
    LfoGenerators lfo(c_rate, 9u);
    lfo.setWaveForm(LfoType::Noise);
    lfo.setFrequency(1.0f);
    const auto out = run(lfo, 100000);
    double sum = 0.0;
    double squares = 0.0;
    double lagProduct = 0.0;
    for (size_t i = 0; i < out.size(); ++i)
    {
        sum += static_cast<double>(out[i]);
        squares += static_cast<double>(out[i]) * static_cast<double>(out[i]);
        lagProduct += i > 0 ? static_cast<double>(out[i]) * static_cast<double>(out[i - 1]) : 0.0;
    }
    const auto n = static_cast<double>(out.size());
    EXPECT_NEAR(sum / n, 0.0, 0.01);
    EXPECT_NEAR(std::sqrt(squares / n), 1.0 / std::sqrt(3.0), 0.005);
    EXPECT_NEAR(lagProduct / squares, 0.0, 0.01) << "uncorrelated from tick to tick";
}

TEST(SynthLfo, ZeroOrNegativeFrequencyStopsAndHoldsTheLastValue)
{
    LfoGenerators lfo(c_rate);
    lfo.setWaveForm(LfoType::Sine);
    lfo.setFrequency(10.0f);
    std::ignore = run(lfo, 100);
    lfo.setFrequency(0.0f);
    const float held = lfo.step();
    for (int i = 0; i < 100; ++i)
    {
        EXPECT_EQ(lfo.step(), held);
    }
    lfo.setFrequency(-3.0f);
    EXPECT_EQ(lfo.step(), held);
    lfo.setFrequency(std::numeric_limits<float>::quiet_NaN());
    EXPECT_EQ(lfo.step(), held) << "NaN is not above 0";
}

TEST(SynthLfo, PauseAndPlayKeepThePhaseAndStopResetsIt)
{
    LfoGenerators lfo(c_rate);
    lfo.setWaveForm(LfoType::Saw);
    lfo.setFrequency(30.0f);
    std::ignore = run(lfo, 20);
    lfo.pause();
    const float held = lfo.step();
    EXPECT_EQ(lfo.step(), held);
    lfo.play();
    EXPECT_NEAR(lfo.step(), 2.0f * 0.21f - 1.0f, 1e-5f) << "continues at tick 21";
    lfo.stop();
    EXPECT_EQ(lfo.step(), 0.0f);
    lfo.play();
    EXPECT_NEAR(lfo.step(), 2.0f * 0.01f - 1.0f, 1e-5f) << "from phase 0 again";
}

TEST(SynthLfo, SetPhaseWrapsDegreesAndIgnoresNonFiniteValues)
{
    LfoGenerators lfo(c_rate);
    lfo.setWaveForm(LfoType::Saw);
    lfo.setFrequency(30.0f);
    for (const float degrees : {90.0f, 450.0f, -270.0f})
    {
        lfo.setPhase(degrees);
        EXPECT_NEAR(lfo.step(), 2.0f * 0.26f - 1.0f, 1e-5f) << degrees;
    }
    lfo.setPhase(0.0f);
    lfo.setPhase(std::numeric_limits<float>::infinity());
    lfo.setPhase(std::numeric_limits<float>::quiet_NaN());
    EXPECT_NEAR(lfo.step(), 2.0f * 0.01f - 1.0f, 1e-5f);
    lfo.setPhase(-1e-20f);
    EXPECT_LT(lfo.step(), 1.0f);
}

TEST(SynthLfo, NoiseWaveformsFollowTheSeed)
{
    for (const LfoType type : {LfoType::Noise, LfoType::SampleHoldNoise, LfoType::BrownNoise})
    {
        auto make = [type](const uint64_t seed)
        {
            LfoGenerators lfo(c_rate, seed);
            lfo.setWaveForm(type);
            lfo.setFrequency(20.0f);
            return run(lfo, 500);
        };
        EXPECT_EQ(make(1u), make(1u)) << static_cast<int>(type);
        EXPECT_NE(make(1u), make(2u)) << static_cast<int>(type);
    }
}

TEST(SynthLfo, BrownNoiseStaysFinite)
{
    LfoGenerators lfo(c_rate);
    lfo.setWaveForm(LfoType::BrownNoise);
    lfo.setFrequency(2.0f);
    for (const float x : run(lfo, 100000))
    {
        ASSERT_TRUE(std::isfinite(x));
    }
}

TEST(SynthLfo, TheSmootherAttenuatesASineByTheOnePoleResponseAtItsCorner)
{
    constexpr double twoPi{6.283185307179586};
    for (const float speed : {0.1f, 1.0f, 20.0f, 100.0f})
    {
        SynthLfo lfo(c_rate);
        lfo.setWaveForm(LfoType::Sine);
        lfo.setSpeed(speed);
        const double ratio = 5.0 + 10.0 * std::log10(static_cast<double>(speed) * 10.0) / 3.0;
        const double pole = std::exp(-twoPi * static_cast<double>(speed) * ratio / static_cast<double>(c_rate));
        const double omega = twoPi * static_cast<double>(speed) / static_cast<double>(c_rate);
        const double expected = (1.0 - pole) / std::abs(1.0 - pole * std::exp(std::complex<double>(0.0, -omega)));
        double sum = 0.0;
        const auto cycle = static_cast<size_t>(c_rate / speed);
        const size_t settle = 10 * cycle;
        for (size_t i = 0; i < settle + cycle; ++i)
        {
            const double v = static_cast<double>(lfo.getValue());
            sum += i >= settle ? v * v : 0.0;
        }
        const double amplitude = std::sqrt(2.0 * sum / static_cast<double>(cycle));
        EXPECT_NEAR(amplitude, expected, 0.003) << speed << " Hz";
        EXPECT_GT(amplitude, 0.8) << "a sine passes almost unchanged";
    }
}

TEST(SynthLfo, TheSmootherRoundsOffTheEdgesOfASquare)
{
    SynthLfo lfo(c_rate);
    lfo.setWaveForm(LfoType::Square);
    lfo.setSpeed(20.0f);
    std::ignore = lfo.getValue();
    float largestStep = 0.0f;
    float previous = lfo.getValue();
    for (int i = 0; i < 6000; ++i)
    {
        const float v = lfo.getValue();
        largestStep = std::max(largestStep, std::abs(v - previous));
        previous = v;
    }
    EXPECT_LT(largestStep, 1.0f) << "the raw square steps by 2";
    EXPECT_GT(largestStep, 0.1f);
}

TEST(SynthLfo, TheSpeedIsClampedForTheSmootherOnly)
{
    SynthLfo lfo(c_rate);
    lfo.setWaveForm(LfoType::Sine);
    lfo.setSpeed(1e6f); // above a quarter of the rate: the smoother is clamped, the oscillator is not
    for (int i = 0; i < 1000; ++i)
    {
        ASSERT_TRUE(std::isfinite(lfo.getValue()));
    }
    lfo.setSpeed(0.0f);
    const float held = lfo.getValue();
    EXPECT_TRUE(std::isfinite(held));
}

}
