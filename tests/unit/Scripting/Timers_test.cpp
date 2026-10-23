#include <cstdint>
#include <gtest/gtest.h>
#include <utility>
#include <vector>

#include "scripting/TempoClock.h"
#include "scripting/Timers.h"

namespace jp::script::test
{

namespace
{

TempoClock clock120()
{
    TempoClock clock;
    clock.configure(48000.0, 24);
    clock.setTempo(120.0);
    return clock;
}

using Fired = std::vector<std::pair<int, uint64_t>>;

Fired fire(TimerBank& bank, const uint64_t limit)
{
    Fired fired;
    bank.advance(limit, [&](const uint8_t id, const uint64_t due) { fired.emplace_back(id, due); });
    return fired;
}

TimerRequest request(const uint8_t id, const double ms, const double ticks = 0.0, const bool repeat = false,
                     const uint64_t base = 0)
{
    TimerRequest result;
    result.id = id;
    result.milliseconds = ms;
    result.ticks = ticks;
    result.repeat = repeat;
    result.base = base;
    return result;
}

}

TEST(TimerBank, ATimerIsDueItsDelayAfterTheEventThatSetItElseAfterNow)
{
    const TempoClock clock = clock120();
    TimerBank bank;
    bank.apply(request(1, 10.0, 0.0, false, 5000), clock, 9000); // 480 samples after the event at 5000
    bank.apply(request(2, 10.0), clock, 9000);                   // not from a hook: after now
    EXPECT_EQ(fire(bank, 5479), Fired{});
    EXPECT_EQ(fire(bank, 5481), (Fired{{1, 5480}}));
    EXPECT_EQ(fire(bank, 20000), (Fired{{2, 9480}}));
    EXPECT_EQ(bank.active(), 0u);
}

TEST(TimerBank, MillisecondsAndTicksAddUpAtThePresentTempo)
{
    TimerBank bank;
    bank.apply(request(0, 10.0, 3.0), clock120(), 0); // 480 + 3 * 1000
    EXPECT_EQ(fire(bank, 100000), (Fired{{0, 3480}}));
}

TEST(TimerBank, ARepeatingTimerKeepsItsPaceWithoutDrift)
{
    TimerBank bank;
    bank.apply(request(3, 0.0, 1.0, true), clock120(), 0);
    Fired all;
    for (uint64_t limit = 128; limit < 1000000; limit += 128)
    {
        const Fired part = fire(bank, limit);
        all.insert(all.end(), part.begin(), part.end());
    }
    ASSERT_GE(all.size(), 990u);
    for (size_t i = 0; i < all.size(); ++i)
    {
        EXPECT_EQ(all[i].second, 1000 * (i + 1));
    }
}

TEST(TimerBank, AZeroDelayCannotFloodARepeatingTimerHasAtLeastAMillisecond)
{
    TimerBank bank;
    bank.apply(request(0, 0.0, 0.0, true), clock120(), 0);
    const Fired fired = fire(bank, 481); // 10 ms
    EXPECT_EQ(fired.size(), 10u);
    EXPECT_EQ(fired[1].second - fired[0].second, 48u);
}

TEST(TimerBank, SettingATimerAgainRestartsItAndStoppingItSilencesIt)
{
    const TempoClock clock = clock120();
    TimerBank bank;
    bank.apply(request(4, 100.0), clock, 0);
    bank.apply(request(4, 200.0), clock, 1000); // restarted: 9600 samples after 1000
    EXPECT_EQ(fire(bank, 9000), Fired{});
    TimerRequest stop;
    stop.id = 4;
    stop.stop = true;
    bank.apply(stop, clock, 2000);
    EXPECT_EQ(fire(bank, 50000), Fired{});
    bank.apply(request(5, 1.0, 0.0, true), clock, 0);
    bank.clear();
    EXPECT_EQ(fire(bank, 50000), Fired{});
}

TEST(TimerBank, SeveralTimersFireInTheOrderOfTheirTimes)
{
    const TempoClock clock = clock120();
    TimerBank bank;
    bank.apply(request(0, 30.0), clock, 0);
    bank.apply(request(1, 10.0, 0.0, true), clock, 0);
    bank.apply(request(2, 20.0), clock, 0);
    const Fired fired = fire(bank, 48 * 35);
    // at equal times the timer with the lower id comes first
    const Fired wanted{{1, 480}, {1, 960}, {2, 960}, {0, 1440}, {1, 1440}};
    EXPECT_EQ(fired, wanted);
}

TEST(TimerBank, ThePerCallLimitStopsALateCallFromLoopingForEver)
{
    TimerBank bank;
    bank.apply(request(0, 1.0, 0.0, true), clock120(), 0);
    EXPECT_EQ(fire(bank, 48ull * 1000000).size(), 64u);
}

}
