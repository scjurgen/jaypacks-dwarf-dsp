// Unit tests of the siggen test plugin's pedal: every waveform, restart, on/off and the RMS
// report. Measurements use the analysis blocks.

#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <vector>

#include "tests/unit/TestHelpers.h"
#include "dsp/Analysis/SignalStats.h"
#include "dsp/Analysis/ToneFit.h"
#include "dsp/Analysis/ZeroCrossingFrequency.h"
#include "plugins/siggen/SigGenPedal.h"

namespace jp::test
{

namespace
{
constexpr float c_fs{48000.0f};

float waveformValue(const SigGenPedal::Waveform waveform)
{
    return static_cast<float>(waveform);
}

SigGenPedal make(const SigGenPedal::Waveform waveform, const float frequency, const float levelDb)
{
    SigGenPedal pedal;
    pedal.setSampleRate(c_fs);
    pedal.setWaveform(waveformValue(waveform));
    pedal.setFrequency(frequency);
    pedal.setLevel(levelDb);
    pedal.reset();
    return pedal;
}

std::vector<float> run(SigGenPedal& pedal, const size_t length, const size_t blockSize = 128)
{
    std::vector<float> out(length);
    for (size_t pos = 0; pos < length; pos += blockSize)
    {
        pedal.process(out.data() + pos, std::min(blockSize, length - pos));
    }
    return out;
}
}

TEST(SigGenPedal, SineHasItsLevelFrequencyAndStartsAtZero)
{
    auto pedal = make(SigGenPedal::Waveform::Sine, 997.0f, -6.0f);
    const auto out = run(pedal, 48000);
    EXPECT_EQ(out[0], 0.0f);
    EXPECT_GT(out[1], 0.0f);
    ToneFit fit(997.0, c_fs);
    fit.add(out.data(), out.size());
    EXPECT_NEAR(fit.result().amplitude, std::pow(10.0, -6.0 / 20.0), 1e-6);
    EXPECT_LT(fit.result().residualRms, 1e-7);
    ZeroCrossingFrequency counter(c_fs, 0.01f);
    counter.add(out.data(), out.size());
    EXPECT_NEAR(counter.frequency(), 997.0, 1e-3);
}

TEST(SigGenPedal, SquareSawAndImpulsesFollowThePhase)
{
    // 1 kHz at 48 kHz: 48 samples per period; the sign changes where the accumulated phase
    // crosses 0.5, which rounding may move by one sample, so the samples at the edges are left out
    auto square = make(SigGenPedal::Waveform::Square, 1000.0f, 0.0f);
    const auto sq = run(square, 96);
    for (size_t i = 0; i < 96; ++i)
    {
        const size_t inPeriod = i % 48;
        if (inPeriod < 23)
        {
            EXPECT_EQ(sq[i], 1.0f) << i;
        }
        else if (inPeriod > 25 && inPeriod < 47)
        {
            EXPECT_EQ(sq[i], -1.0f) << i;
        }
    }

    auto saw = make(SigGenPedal::Waveform::Saw, 1000.0f, 0.0f);
    const auto sw = run(saw, 96);
    EXPECT_FLOAT_EQ(sw[0], -1.0f);
    EXPECT_NEAR(sw[24], 0.0f, 1e-6f);
    EXPECT_NEAR(sw[47], 2.0f * 47.0f / 48.0f - 1.0f, 1e-6f);

    auto impulses = make(SigGenPedal::Waveform::Impulses, 1000.0f, -20.0f);
    const auto im = run(impulses, 4800);
    size_t count = 0;
    for (size_t i = 0; i < im.size(); ++i)
    {
        if (im[i] != 0.0f)
        {
            EXPECT_EQ(i % 48, 0u) << i;
            EXPECT_FLOAT_EQ(im[i], 0.1f);
            ++count;
        }
    }
    EXPECT_EQ(count, 100u);
}

TEST(SigGenPedal, StepJumpsAfterOnePeriod)
{
    auto pedal = make(SigGenPedal::Waveform::Step, 100.0f, -6.0f);
    const auto out = run(pedal, 1000);
    EXPECT_EQ(out[479], 0.0f);
    EXPECT_FLOAT_EQ(out[480], static_cast<float>(std::pow(10.0, -6.0 / 20.0)));
    EXPECT_EQ(out[999], out[480]);
}

TEST(SigGenPedal, WhiteNoiseIsUniformAndRepeatsAfterARestart)
{
    auto pedal = make(SigGenPedal::Waveform::WhiteNoise, 1000.0f, 0.0f);
    const auto first = run(pedal, 48000 * 4);
    SignalStats stats;
    stats.add(first.data(), first.size());
    EXPECT_NEAR(stats.rms(), 1.0 / std::sqrt(3.0), 2e-3);
    EXPECT_NEAR(stats.dc(), 0.0, 3e-3);
    EXPECT_LE(stats.peak(), 1.0);
    pedal.setRestart(1.0f);
    EXPECT_EQ(run(pedal, 48000 * 4), first);
    EXPECT_EQ(pedal.getRestartId(), 1.0f);
}

TEST(SigGenPedal, RestartOnlyOnANewValueAndAtPhaseZero)
{
    auto pedal = make(SigGenPedal::Waveform::Sine, 440.0f, 0.0f);
    const auto first = run(pedal, 1000);
    pedal.setRestart(0.0f); // the value it has: no restart
    const auto continued = run(pedal, 1000);
    EXPECT_NE(continued[0], 0.0f);
    pedal.setRestart(7.0f);
    EXPECT_EQ(run(pedal, 1000), first);
    pedal.setRestart(7.0f);
    EXPECT_NE(run(pedal, 1000), first);
    EXPECT_EQ(pedal.getRestartId(), 7.0f);
}

TEST(SigGenPedal, LogSweepMovesFromStartToEndAndRepeats)
{
    auto pedal = make(SigGenPedal::Waveform::LogSweep, 100.0f, 0.0f);
    pedal.setSweepEnd(10000.0f);
    pedal.setSweepTime(2.0f);
    pedal.reset();
    const auto out = run(pedal, 48000 * 4);
    EXPECT_EQ(out[0], 0.0f);
    // frequency over a 10 ms window at time t: 100 * 100^(t / 2 s) (from 0.5 s on, where the
    // window holds at least three periods)
    for (const double t : {0.5, 1.0, 1.5, 1.9})
    {
        ZeroCrossingFrequency counter(c_fs, 0.01f);
        const auto start = static_cast<size_t>(t * c_fs);
        counter.add(out.data() + start, 480);
        const double expected = 100.0 * std::pow(100.0, (t + 0.005) / 2.0);
        EXPECT_NEAR(counter.frequency() / expected, 1.0, 0.02) << t;
    }
    // the second pass equals the first
    const std::vector<float> firstPass(out.begin(), out.begin() + 96000);
    const std::vector<float> secondPass(out.begin() + 96000, out.end());
    EXPECT_EQ(secondPass, firstPass);
}

TEST(SigGenPedal, OffIsSilentAndKeepsTheTiming)
{
    auto running = make(SigGenPedal::Waveform::Sine, 440.0f, 0.0f);
    auto switched = make(SigGenPedal::Waveform::Sine, 440.0f, 0.0f);
    switched.setOn(0.0f);
    const auto silent = run(switched, 1000);
    EXPECT_TRUE(std::all_of(silent.begin(), silent.end(), [](const float x) { return x == 0.0f; }));
    std::ignore = run(running, 1000);
    switched.setOn(1.0f);
    EXPECT_EQ(run(switched, 1000), run(running, 1000));
}

TEST(SigGenPedal, RmsReportsTheLastFullSecond)
{
    auto pedal = make(SigGenPedal::Waveform::Sine, 1000.0f, -20.0f);
    std::ignore = run(pedal, 47999);
    EXPECT_EQ(pedal.getRms(), SigGenPedal::c_floorDb);
    std::ignore = run(pedal, 1);
    EXPECT_NEAR(pedal.getRms(), -20.0 - 10.0 * std::log10(2.0), 1e-4);
    pedal.setWaveform(waveformValue(SigGenPedal::Waveform::Silence));
    std::ignore = run(pedal, 48000);
    EXPECT_EQ(pedal.getRms(), SigGenPedal::c_floorDb);
}

TEST(SigGenPedal, OutputDoesNotDependOnTheBlockSize)
{
    for (const auto waveform : {SigGenPedal::Waveform::Sine, SigGenPedal::Waveform::WhiteNoise,
                                SigGenPedal::Waveform::LogSweep, SigGenPedal::Waveform::Impulses})
    {
        auto a = make(waveform, 777.0f, -3.0f);
        auto b = make(waveform, 777.0f, -3.0f);
        EXPECT_EQ(run(a, 20000, 1), run(b, 20000, 4096)) << static_cast<int>(waveform);
    }
}

}
