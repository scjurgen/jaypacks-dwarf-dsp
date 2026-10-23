#include <cmath>
#include <cstdint>
#include <gtest/gtest.h>
#include <utility>
#include <vector>

#include "scripting/TempoClock.h"

namespace jp::script::test
{

namespace
{

using Ticks = std::vector<std::pair<uint64_t, uint64_t>>;

/// The ticks the clock makes while a pedal cuts the audio into blocks of a length and announces `lookahead` samples
/// early.
Ticks run(TempoClock& clock, const uint64_t length, const uint32_t block, const uint64_t lookahead)
{
    Ticks ticks;
    for (uint64_t start = 0; start < length; start += block)
    {
        clock.advance(start + block + lookahead,
                      [&](const uint64_t index, const uint64_t due) { ticks.emplace_back(index, due); });
    }
    return ticks;
}

TempoClock make(const double tempo = 120.0)
{
    TempoClock clock;
    clock.configure(48000.0, 24);
    clock.setTempo(tempo);
    return clock;
}

}

TEST(TempoClock, At120BpmAndTwentyFourTicksPerQuarterATickIsAThousandSamples)
{
    TempoClock clock = make();
    EXPECT_DOUBLE_EQ(clock.samplesPerTick(), 1000.0);
    EXPECT_EQ(clock.ticksToSamples(6), 6000u);
    EXPECT_EQ(clock.msToSamples(10.0), 480u);
    clock.start(500);
    const Ticks ticks = run(clock, 10000, 128, 0);
    ASSERT_GE(ticks.size(), 9u);
    for (size_t i = 0; i < ticks.size(); ++i)
    {
        EXPECT_EQ(ticks[i].first, i);
        EXPECT_EQ(ticks[i].second, 500 + 1000 * i);
    }
}

TEST(TempoClock, NothingHappensUntilItStartsAndAfterItStops)
{
    TempoClock clock = make();
    EXPECT_TRUE(run(clock, 5000, 128, 0).empty());
    clock.start(0);
    EXPECT_FALSE(run(clock, 1100, 128, 0).empty());
    clock.stop();
    EXPECT_FALSE(clock.running());
    EXPECT_TRUE(run(clock, 5000, 128, 0).empty());
    clock.start(7000);
    const Ticks again = run(clock, 7000 + 2500, 128, 0);
    ASSERT_FALSE(again.empty());
    EXPECT_EQ(again.front(), (std::pair<uint64_t, uint64_t>{0, 7000})) << "a new start begins with tick 0";
}

TEST(TempoClock, TheSameTicksComeOutWhateverTheBlockLength)
{
    Ticks reference;
    for (const uint32_t block : {1u, 7u, 64u, 128u, 513u, 4096u})
    {
        TempoClock clock = make(97.3);
        clock.start(123);
        // announced one block and a bit ahead: the limit of the last block is the same end for all lengths
        const Ticks ticks = run(clock, 480000, block, 4096);
        Ticks inside;
        for (const auto& tick : ticks)
        {
            if (tick.second < 480000)
            {
                inside.push_back(tick);
            }
        }
        if (reference.empty())
        {
            reference = inside;
        }
        EXPECT_EQ(inside, reference) << "block " << block;
    }
    EXPECT_GT(reference.size(), 200u);
}

TEST(TempoClock, TheDueTimeOfATickStaysWithinOneSampleOfTheExactTimeOverMillionsOfTicks)
{
    TempoClock clock = make(137.7);
    clock.start(0);
    const double period = 48000.0 * 60.0 / (137.7 * 24.0);
    uint64_t count = 0;
    double worst = 0.0;
    clock.advance(static_cast<uint64_t>(period * 3.0e6),
                  [&](const uint64_t index, const uint64_t due)
                  {
                      const double exact = static_cast<double>(index) * period;
                      worst = std::max(worst, std::abs(static_cast<double>(due) - exact));
                      ++count;
                  });
    EXPECT_GE(count, 3000000u);
    EXPECT_LT(worst, 1.001);
}

TEST(TempoClock, ATempoChangeTakesEffectFromTheTickAfterTheOnesAlreadyMade)
{
    TempoClock clock = make(120.0);
    clock.start(0);
    Ticks ticks;
    const auto collect = [&](const uint64_t limit)
    { clock.advance(limit, [&](const uint64_t index, const uint64_t due) { ticks.emplace_back(index, due); }); };
    collect(3500); // ticks 0 to 3 at 0, 1000, 2000, 3000; tick 4 would be due at 4000
    clock.setTempo(60.0);
    collect(12000); // the new period of 2000 counts from the last tick made, tick 3 at 3000
    ASSERT_GE(ticks.size(), 8u);
    EXPECT_EQ(ticks[3].second, 3000u);
    EXPECT_EQ(ticks[4].second, 5000u);
    EXPECT_EQ(ticks[5].second, 7000u);
    EXPECT_EQ(ticks[6].second, 9000u);
    EXPECT_EQ(ticks[7].second, 11000u);
    EXPECT_DOUBLE_EQ(clock.tempo(), 60.0);
}

TEST(TempoClock, TheTempoIsLimitedAndANonNumberIsIgnored)
{
    TempoClock clock = make();
    clock.setTempo(0.5);
    EXPECT_DOUBLE_EQ(clock.tempo(), TempoClock::c_minTempo);
    clock.setTempo(10000.0);
    EXPECT_DOUBLE_EQ(clock.tempo(), TempoClock::c_maxTempo);
    clock.setTempo(std::nan(""));
    EXPECT_DOUBLE_EQ(clock.tempo(), TempoClock::c_maxTempo);
    clock.setTempo(90.0);
    EXPECT_EQ(clock.msToSamples(-4.0), 0u);
    EXPECT_EQ(clock.ticksToSamples(0.0), 0u);
}

}
