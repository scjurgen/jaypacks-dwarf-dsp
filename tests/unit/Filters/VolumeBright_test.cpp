#include <cmath>
#include <gtest/gtest.h>
#include <iostream>

#include "dsp/Filters/VolumeBright.h"
#include "tests/unit/TestHelpers.h"

namespace jp::test
{

namespace
{
/// The divider of the volume pot with the bright cap, normalised to DC, straight from the circuit.
double circuitLiftDb(const BrightNetwork& n, const double fraction, const double frequencyHz)
{
    const std::complex<double> s{0.0, 2.0 * c_pi * frequencyHz};
    const double ru = (1.0 - fraction) * n.potOhms;
    const double rl = fraction * n.potOhms;
    const std::complex<double> upper = ru / (1.0 + s * ru * n.capFarad);
    const std::complex<double> h = rl / (n.sourceOhms + upper + rl);
    const double dc = rl / (n.sourceOhms + ru + rl);
    return toDb(std::abs(h) / dc);
}
}

TEST(VolumeBright, LiftEqualsTheCircuit)
{
    VolumeBright bright;
    bright.setSampleRate(48000.0f);
    bright.setAmount(1.0f);
    const BrightNetwork network{};
    double worst = 0.0;
    for (const double fraction : {0.01, 0.034, 0.1, 0.3, 0.7, 0.95})
    {
        bright.setVolumeFraction(fraction);
        for (const double f : {100.0, 500.0, 1000.0, 2000.0, 4000.0})
        {
            const double analog = toDb(std::abs(analogSectionResponse(bright.getAnalog(), f)));
            EXPECT_NEAR(analog, circuitLiftDb(network, fraction, f), 1e-9) << fraction << " " << f;
            worst = std::max(worst, std::abs(toDb(std::abs(bright.response(f))) - analog));
        }
        EXPECT_NEAR(toDb(bright.getHighFrequencyLift()), circuitLiftDb(network, fraction, 1e9), 1e-6) << fraction;
    }
    std::cout << "[ bright   ] digital vs circuit up to 4 kHz: worst " << worst << " dB\n";
    EXPECT_LT(worst, 0.2);
}

TEST(VolumeBright, StrongAtLowVolumeAndGoneAtFull)
{
    VolumeBright bright;
    bright.setSampleRate(48000.0f);
    bright.setAmount(1.0f);
    for (const double knob : {0.0, 0.3, 0.5, 0.7, 1.0})
    {
        bright.setVolume(knob);
        std::cout << "[ bright   ] volume " << knob * 10.0 << ": lift at 3 kHz "
                  << toDb(std::abs(bright.response(3000.0))) << " dB, high-frequency limit "
                  << toDb(bright.getHighFrequencyLift()) << " dB\n";
    }
    bright.setVolume(0.3);
    EXPECT_GT(toDb(std::abs(bright.response(3000.0))), 6.0);
    EXPECT_NEAR(toDb(std::abs(bright.response(50.0))), 0.0, 0.05);
    bright.setVolume(1.0);
    EXPECT_LT(toDb(std::abs(bright.response(3000.0))), 0.1);
}

TEST(VolumeBright, AmountZeroIsExactPassThrough)
{
    VolumeBright bright;
    bright.setSampleRate(48000.0f);
    bright.setVolume(0.2);
    bright.setAmount(0.0f);
    for (int i = 0; i < 2000; ++i)
    {
        const float x = std::sin(0.37f * static_cast<float>(i)) * 0.8f;
        EXPECT_EQ(bright.process(x), x);
    }
}

TEST(VolumeBright, RunningFilterMatchesDesign)
{
    VolumeBright bright;
    bright.setSampleRate(48000.0f);
    bright.setVolume(0.3);
    bright.setAmount(0.7f);
    for (const double f : {200.0, 2000.0, 8000.0})
    {
        bright.reset();
        const double measured =
            measureSineGain([&bright](const float x) { return bright.process(x); }, f, 48000.0, 0.1, 0.2, 0.5);
        EXPECT_NEAR(toDb(measured), toDb(std::abs(bright.response(f))), 0.01) << f;
    }
}

}
