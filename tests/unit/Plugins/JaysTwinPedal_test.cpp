#include <cmath>
#include <gtest/gtest.h>
#include <iostream>
#include <vector>

#include "plugins/jaystwin/JaysTwinPedal.h"
#include "tests/unit/TestHelpers.h"

namespace jp::test
{

namespace
{
constexpr double c_fs{48000.0};

struct Knobs
{
    float bass{JaysTwinPedal::c_defaultBass};
    float mid{JaysTwinPedal::c_defaultMid};
    float treble{JaysTwinPedal::c_defaultTreble};
    float gain{JaysTwinPedal::c_defaultGain};
    float level{JaysTwinPedal::c_defaultLevel};
    float bright{JaysTwinPedal::c_defaultBright};
};

void apply(JaysTwinPedal& pedal, const Knobs& k)
{
    pedal.setBass(k.bass);
    pedal.setMid(k.mid);
    pedal.setTreble(k.treble);
    pedal.setGain(k.gain);
    pedal.setLevel(k.level);
    pedal.setBright(k.bright);
}

/// The designed linear chain of a pedal: makeup x tone stack x bright x small-signal drive gain
/// x cabinet x level x output trim.
double designedDb(const JaysTwinPedal& pedal, const double f, const float levelDb)
{
    const double chain = std::abs(pedal.getToneStack().response(f) * pedal.getBright().response(f) *
                                  pedal.getCabinet().response(f, c_fs));
    return toDb(chain) + static_cast<double>(JaysTwinPedal::c_toneStackMakeupDb) +
           toDb(static_cast<double>(pedal.getDrive().getSmallSignalGain())) +
           static_cast<double>(levelDb + JaysTwinPedal::c_outputDb);
}
}

TEST(JaysTwinPedal, SmallSignalsFollowTheDesignedChain)
{
    // at an amplitude of 1e-4 the atan is linear within 0.01 dB even at gain 11 (d x < 0.05)
    for (const Knobs k :
         {Knobs{}, Knobs{2.0f, 8.0f, 7.0f, 0.0f, 3.0f, 1.0f}, Knobs{10.0f, 0.0f, 0.0f, 11.0f, -6.0f, 1.0f}})
    {
        JaysTwinPedal pedal;
        apply(pedal, k);
        pedal.reset();
        for (const double f : {82.0, 250.0, 1000.0, 2300.0, 5000.0})
        {
            pedal.reset();
            const double measured = toDb(measureSineGain(
                [&pedal](const float x)
                {
                    float y = 0.0f;
                    pedal.process(&x, &y, 1);
                    return y;
                },
                f, c_fs, 1e-4, 0.5, 0.5));
            EXPECT_NEAR(measured, designedDb(pedal, f, k.level), 0.02)
                << "bass " << k.bass << " gain " << k.gain << " bright " << k.bright << " at " << f << " Hz";
        }
    }
}

TEST(JaysTwinPedal, MakeupPutsTheToneStackMaximumAtZeroDecibels)
{
    JaysTwinPedal pedal;
    double peak = -1e9;
    for (double f = 20.0; f < 20000.0; f *= 1.01)
    {
        peak = std::max(peak, toDb(std::abs(pedal.getToneStack().response(f))));
    }
    std::cout << "[ twin     ] tone stack maximum at 5/5/5 with makeup: "
              << peak + static_cast<double>(JaysTwinPedal::c_toneStackMakeupDb) << " dB\n";
    EXPECT_NEAR(peak + static_cast<double>(JaysTwinPedal::c_toneStackMakeupDb), 0.0, 0.1);
}

TEST(JaysTwinPedal, LevelIsExactInDecibels)
{
    std::vector<float> input(4800);
    for (size_t i = 0; i < input.size(); ++i)
    {
        input[i] = 0.3f * std::sin(0.03f * static_cast<float>(i));
    }
    std::vector<float> reference(input.size());
    std::vector<float> louder(input.size());
    JaysTwinPedal a;
    JaysTwinPedal b;
    b.setLevel(6.0f);
    b.reset();
    a.process(input.data(), reference.data(), input.size());
    b.process(input.data(), louder.data(), input.size());
    const float ratio = std::pow(10.0f, 6.0f / 20.0f);
    for (size_t i = 100; i < input.size(); ++i)
    {
        EXPECT_NEAR(louder[i], reference[i] * ratio, 1e-6f + 1e-5f * std::abs(louder[i])) << i;
    }
}

TEST(JaysTwinPedal, BrightLiftsTheTrebleAtLowGainOnly)
{
    const auto at = [](const float gain, const float bright, const double f)
    {
        JaysTwinPedal pedal;
        pedal.setGain(gain);
        pedal.setBright(bright);
        pedal.reset();
        return toDb(std::abs(pedal.getBright().response(f)));
    };
    std::cout << "[ twin     ] bright lift at 3 kHz: gain 2 " << at(2.0f, 1.0f, 3000.0) << " dB, gain 10 "
              << at(10.0f, 1.0f, 3000.0) << " dB\n";
    EXPECT_GT(at(2.0f, 1.0f, 3000.0), 6.0);
    EXPECT_LT(at(10.0f, 1.0f, 3000.0), 0.01);
    EXPECT_EQ(at(11.0f, 1.0f, 3000.0), at(10.0f, 1.0f, 3000.0));
    EXPECT_NEAR(at(2.0f, 0.0f, 3000.0), 0.0, 1e-12);
}

TEST(JaysTwinPedal, DriveRangeAndMapping)
{
    EXPECT_FLOAT_EQ(JaysTwinPedal::driveForGain(0.0f), 1.0f);
    EXPECT_NEAR(JaysTwinPedal::driveForGain(10.0f), std::pow(10.0f, 36.0f / 20.0f), 1e-3f);
    EXPECT_NEAR(JaysTwinPedal::driveForGain(5.0f), std::pow(10.0f, 18.0f / 20.0f), 1e-4f);
    EXPECT_FLOAT_EQ(JaysTwinPedal::driveForGain(-3.0f), 1.0f);
    // up to eleven: one more step of 3.6 dB
    EXPECT_NEAR(JaysTwinPedal::driveForGain(11.0f), std::pow(10.0f, 39.6f / 20.0f), 1e-3f);
    EXPECT_FLOAT_EQ(JaysTwinPedal::driveForGain(12.0f), JaysTwinPedal::driveForGain(11.0f));
    JaysTwinPedal pedal;
    EXPECT_NEAR(pedal.getDrive().getDrive(), JaysTwinPedal::driveForGain(JaysTwinPedal::c_defaultGain), 1e-5f);
}

TEST(JaysTwinPedal, BlockSplitDoesNotChangeTheOutputWhileKnobsMove)
{
    std::vector<float> input(24000);
    for (size_t i = 0; i < input.size(); ++i)
    {
        input[i] = 0.4f * std::sin(0.021f * static_cast<float>(i)) + 0.1f * std::sin(0.31f * static_cast<float>(i));
    }
    const auto run = [&input](const size_t block)
    {
        JaysTwinPedal pedal;
        std::vector<float> out(input.size());
        for (size_t start = 0; start < input.size(); start += block)
        {
            if (start >= 4000 && start < 4000 + block)
            {
                pedal.setTreble(10.0f);
                pedal.setGain(8.0f);
                pedal.setBright(1.0f);
            }
            if (start >= 9000 && start < 9000 + block)
            {
                pedal.setBass(0.0f);
                pedal.setMid(10.0f);
            }
            const size_t n = std::min(block, input.size() - start);
            pedal.process(input.data() + start, out.data() + start, n);
        }
        return out;
    };
    // the knobs change at the first block boundary at or after the sample, so compare blocks
    // that divide 4000 and 9000 alike
    const auto reference = run(1);
    EXPECT_EQ(run(1000), reference);
    EXPECT_EQ(run(40), reference);
}

TEST(JaysTwinPedal, ResetClearsTheFilters)
{
    JaysTwinPedal pedal;
    std::vector<float> noise(4800);
    for (size_t i = 0; i < noise.size(); ++i)
    {
        noise[i] = (i % 3 == 0) ? 0.9f : -0.4f;
    }
    pedal.process(noise.data(), noise.data(), noise.size());
    pedal.reset();
    std::vector<float> silence(256, 0.0f);
    pedal.process(silence.data(), silence.data(), silence.size());
    for (const float y : silence)
    {
        EXPECT_EQ(y, 0.0f);
    }
}

}
