#include <cmath>
#include <gtest/gtest.h>
#include <iostream>
#include <vector>

#include "dsp/NonLinear/AtanDrive.h"
#include "tests/unit/TestHelpers.h"

namespace jp::test
{

TEST(AtanDrive, OddBoundedAndUnityAtOne)
{
    AtanDrive drive;
    for (const float d : {0.001f, 0.5f, 1.0f, 4.0f, 60.0f})
    {
        drive.setDrive(d);
        EXPECT_NEAR(drive.process(1.0f), 1.0f, 1e-6f) << d;
        const float bound = 1.5707963f / std::atan(d);
        for (float x = -5.0f; x <= 5.0f; x += 0.01f)
        {
            EXPECT_EQ(drive.process(-x), -drive.process(x)) << d << " " << x;
            EXPECT_LE(std::abs(drive.process(x)), bound) << d << " " << x;
        }
    }
}

TEST(AtanDrive, SmallSignalGainAndIdentityLimit)
{
    AtanDrive drive;
    for (const float d : {0.5f, 2.0f, 30.0f})
    {
        drive.setDrive(d);
        const float x = 1e-4f;
        EXPECT_NEAR(drive.process(x) / x, drive.getSmallSignalGain(), 1e-3f * drive.getSmallSignalGain()) << d;
        EXPECT_NEAR(drive.getSmallSignalGain(), d / std::atan(d), 1e-5f) << d;
    }
    drive.setDrive(0.0f);
    EXPECT_FLOAT_EQ(drive.getDrive(), 1e-3f);
    for (const float x : {-0.9f, -0.2f, 0.3f, 0.7f})
    {
        EXPECT_NEAR(drive.process(x), x, 1e-6f) << x;
    }
}

TEST(AtanDrive, HarmonicsAreOddAndGrowWithDrive)
{
    constexpr double fs{48000.0};
    constexpr double f0{750.0}; // 64 periods in 4096 samples: every harmonic on a bin
    double previousThird = -400.0;
    for (const float d : {1.0f, 4.0f, 16.0f})
    {
        AtanDrive drive;
        drive.setDrive(d);
        std::vector<float> out(4096);
        for (size_t i = 0; i < out.size(); ++i)
        {
            out[i] = drive.process(0.5f * static_cast<float>(std::sin(2.0 * c_pi * f0 * static_cast<double>(i) / fs)));
        }
        const double h1 = std::abs(toneAmplitude(out, f0, fs));
        const double h2 = std::abs(toneAmplitude(out, 2.0 * f0, fs));
        const double h3 = std::abs(toneAmplitude(out, 3.0 * f0, fs));
        std::cout << "[ atan     ] drive " << d << ": 3rd " << toDb(h3 / h1) << " dB, 2nd " << toDb(h2 / h1)
                  << " dB re fundamental\n";
        EXPECT_LT(toDb(h2 / h1), -100.0) << d;
        EXPECT_GT(toDb(h3 / h1), previousThird) << d;
        previousThird = toDb(h3 / h1);
    }
}

}
