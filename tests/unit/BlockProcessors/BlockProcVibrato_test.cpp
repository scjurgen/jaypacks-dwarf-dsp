#include <cmath>
#include <gtest/gtest.h>
#include <iostream>
#include <vector>

#include "dsp/Analysis/ZeroCrossingFrequency.h"
#include "dsp/BlockProcessors/BlockProcVibrato.h"
#include "tests/unit/TestHelpers.h"

namespace jp::test
{

namespace
{
constexpr size_t c_block{16};
constexpr float c_fs{48000.0f};
constexpr float c_rate{0.5f};
constexpr float c_maxSwingSeconds{0.004f};
using Vibrato = BlockVibrato<c_block>;

std::vector<float> sine(const double frequency, const size_t length, const double amplitude = 0.5)
{
    std::vector<float> out(length);
    for (size_t i = 0; i < length; ++i)
    {
        out[i] = static_cast<float>(amplitude * std::sin(2.0 * c_pi * frequency * static_cast<double>(i) / c_fs));
    }
    return out;
}

std::vector<float> run(Vibrato& vibrato, const std::vector<float>& input)
{
    std::vector<float> out;
    out.reserve(input.size());
    std::array<float, c_block> block{};
    for (size_t i = 0; i + c_block <= input.size(); i += c_block)
    {
        std::copy_n(input.begin() + static_cast<long>(i), c_block, block.begin());
        vibrato.process(block);
        out.insert(out.end(), block.begin(), block.end());
    }
    return out;
}

Vibrato make(const float cents, const float phase = 0.0f)
{
    Vibrato vibrato(c_fs, c_maxSwingSeconds);
    vibrato.setRate(c_rate);
    vibrato.setPhase(phase);
    vibrato.setDepthCents(cents);
    vibrato.reset();
    return vibrato;
}
}

TEST(BlockVibrato, ZeroDepthIsAnExactDelayOfTheLatency)
{
    // port of AbacDsp ZeroDepthActsAsFixedDelay; here the read falls on whole samples, so exact
    Vibrato vibrato = make(0.0f);
    EXPECT_EQ(vibrato.getLatency(), static_cast<size_t>(std::ceil(c_maxSwingSeconds * c_fs)) + 2);
    const auto input = sine(220.0, 4800);
    const auto output = run(vibrato, input);
    const size_t latency = vibrato.getLatency();
    for (size_t n = 0; n < output.size(); ++n)
    {
        ASSERT_EQ(output[n], n < latency ? 0.0f : input[n - latency]) << n;
    }
}

TEST(BlockVibrato, ResetRestoresTheInitialState)
{
    Vibrato reference = make(12.0f, 0.25f);
    const auto input = sine(330.0, 9600);
    const auto expected = run(reference, input);
    Vibrato dirtied = make(12.0f, 0.25f);
    static_cast<void>(run(dirtied, std::vector<float>(5000, 0.7f)));
    dirtied.reset();
    EXPECT_EQ(run(dirtied, input), expected);
}

TEST(BlockVibrato, PitchDeviationEqualsTheDepthInCents)
{
    // a 1 kHz tone through one LFO period (2 s): frequency per 50 ms window by zero crossings
    for (const float cents : {4.0f, 10.0f, 20.0f})
    {
        Vibrato vibrato = make(cents);
        const auto output = run(vibrato, sine(1000.0, static_cast<size_t>(c_fs * 2.5f)));
        double highest = 0.0;
        double lowest = 1e9;
        const size_t window = 2400;
        for (size_t start = 4800; start + window <= output.size(); start += window / 2)
        {
            ZeroCrossingFrequency counter(c_fs, 0.01f);
            counter.add(output.data() + start, window);
            highest = std::max(highest, counter.frequency());
            lowest = std::min(lowest, counter.frequency());
        }
        const double up = 1200.0 * std::log2(highest / 1000.0);
        const double down = -1200.0 * std::log2(lowest / 1000.0);
        std::cout << "[ vibrato  ] depth " << cents << " cents: measured +" << up << " / -" << down << " cents\n";
        // the window averages the peak a little (50 ms of a 2 s period) and the counter is
        // quantised: within 10 % or 0.5 cents
        EXPECT_NEAR(up, cents, std::max(0.1 * cents, 0.5)) << cents;
        EXPECT_NEAR(down, cents, std::max(0.1 * cents, 0.5)) << cents;
    }
}

TEST(BlockVibrato, DepthIsLimitedByTheLargestSwing)
{
    Vibrato vibrato = make(1000.0f);
    EXPECT_FLOAT_EQ(vibrato.getSwing(), vibrato.getMaxSwing());
    EXPECT_NEAR(Vibrato::swingSamplesFor(20.0f, 0.5f, c_fs), 0.011619 * c_fs / (2.0 * c_pi * 0.5), 0.01);
    EXPECT_EQ(Vibrato::swingSamplesFor(20.0f, 0.0f, c_fs), 0.0f);
}

TEST(BlockVibrato, AllpassKeepsTheTrebleThatLinearInterpolationLoses)
{
    // a 10 kHz tone at full depth: the allpass against linear interpolation along the same delay
    const float cents = 20.0f;
    Vibrato vibrato = make(cents);
    const auto input = sine(10000.0, static_cast<size_t>(c_fs * 2.0f));
    const auto output = run(vibrato, input);
    const double swing = static_cast<double>(vibrato.getSwing());
    const auto nominal = static_cast<double>(vibrato.getLatency());
    std::vector<float> linear(input.size(), 0.0f);
    double phase = 0.0;
    for (size_t n = 0; n < input.size(); ++n)
    {
        const double delay = nominal + swing * std::sin(2.0 * c_pi * phase);
        phase += 0.5 / static_cast<double>(c_fs);
        const double position = static_cast<double>(n) - delay;
        if (position >= 1.0)
        {
            const auto i = static_cast<size_t>(position);
            const double f = position - static_cast<double>(i);
            linear[n] = static_cast<float>((1.0 - f) * input[i] + f * input[i + 1]);
        }
    }
    const auto level = [](const std::vector<float>& x)
    {
        double sum = 0.0;
        for (size_t i = 4800; i < x.size(); ++i)
        {
            sum += static_cast<double>(x[i]) * x[i];
        }
        return std::sqrt(sum / static_cast<double>(x.size() - 4800));
    };
    const double reference = level(input);
    const double allpassDb = toDb(level(output) / reference);
    const double linearDb = toDb(level(linear) / reference);
    std::cout << "[ vibrato  ] 10 kHz through the swinging delay: allpass " << allpassDb << " dB, linear " << linearDb
              << " dB\n";
    EXPECT_NEAR(allpassDb, 0.0, 0.01);
    EXPECT_LT(linearDb, allpassDb - 0.5);
}

TEST(BlockVibrato, ExtremesStayFinite)
{
    // port of AbacDsp BoundaryValuesStayFinite
    for (const float cents : {0.0f, 0.5f, 20.0f, 1e6f})
    {
        for (const float rate : {0.0f, 0.1f, 20.0f, 1e4f})
        {
            Vibrato vibrato(c_fs, c_maxSwingSeconds);
            vibrato.setRate(rate);
            vibrato.setDepthCents(cents);
            std::array<float, c_block> block{};
            for (size_t b = 0; b < 300; ++b)
            {
                block.fill(1.0f);
                vibrato.process(block);
                for (const float y : block)
                {
                    ASSERT_TRUE(std::isfinite(y)) << cents << " " << rate;
                    ASSERT_LE(std::abs(y), 1.5f) << cents << " " << rate;
                }
            }
        }
    }
}

}
