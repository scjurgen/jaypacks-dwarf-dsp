#include <cmath>
#include <gtest/gtest.h>
#include <iostream>

#include "dsp/Filters/BilinearTransform.h"
#include "dsp/Filters/Biquad.h"
#include "tests/unit/TestHelpers.h"

namespace jp::test
{

namespace
{
constexpr float c_sampleRate{48000.0f};
constexpr float c_butterworthQ{0.70710678f};

struct TypeCase
{
    BiquadFilterType type;
    float gainDb;
};

/// Measured gain in dB of the running filter of a type minus its designed magnitude, worst over
/// the grid of Q, cutoff and probe frequencies of the AbacDsp test.
template <BiquadFilterType type>
double worstRunningVersusDesignDb(const float gainDb)
{
    double worst = 0.0;
    for (const float q : {c_butterworthQ, 10.0f, 0.2f})
    {
        for (const float cutoff : {125.0f, 2000.0f})
        {
            for (const double probe : {25.0, 200.0, 800.0, 6400.0})
            {
                Biquad<type> filter;
                filter.computeCoefficients(c_sampleRate, cutoff, q, gainDb);
                const double measured =
                    toDb(measureSineGain([&filter](const float x) { return filter.process(x); }, probe, c_sampleRate));
                const double designed =
                    toDb(static_cast<double>(filter.magnitudeLinear(static_cast<float>(probe) / c_sampleRate)));
                worst = std::max(worst, std::abs(measured - designed));
            }
        }
    }
    return worst;
}

double worstRunningVersusDesignDb(const TypeCase& c)
{
    switch (c.type)
    {
        case BiquadFilterType::LowPass:
            return worstRunningVersusDesignDb<BiquadFilterType::LowPass>(c.gainDb);
        case BiquadFilterType::HighPass:
            return worstRunningVersusDesignDb<BiquadFilterType::HighPass>(c.gainDb);
        case BiquadFilterType::BandPass:
            return worstRunningVersusDesignDb<BiquadFilterType::BandPass>(c.gainDb);
        case BiquadFilterType::Notch:
            return worstRunningVersusDesignDb<BiquadFilterType::Notch>(c.gainDb);
        case BiquadFilterType::Peak:
            return worstRunningVersusDesignDb<BiquadFilterType::Peak>(c.gainDb);
        case BiquadFilterType::LoShelf:
            return worstRunningVersusDesignDb<BiquadFilterType::LoShelf>(c.gainDb);
        case BiquadFilterType::HiShelf:
            return worstRunningVersusDesignDb<BiquadFilterType::HiShelf>(c.gainDb);
        case BiquadFilterType::AllPass:
            return worstRunningVersusDesignDb<BiquadFilterType::AllPass>(c.gainDb);
        case BiquadFilterType::FreeCoefficients:
            break;
    }
    return 1e9;
}

double designedDb(const BiquadFilterType type, const float cutoff, const float q, const float gainDb,
                  const double probe)
{
    BiquadCoefficients c;
    c.coefficients(type, c_sampleRate, cutoff, q, gainDb);
    return toDb(std::abs(biquadSectionResponse(c.getCoefficients(), probe, c_sampleRate)));
}
}

class BiquadTypes : public ::testing::TestWithParam<TypeCase>
{
};

TEST_P(BiquadTypes, RunningFilterMatchesDesignedMagnitude)
{
    // each type's specialised step must realise the designed coefficients (port of AbacDsp
    // allMagnitudesMono, measured by a windowed single-bin DFT instead of the peak)
    const TypeCase c = GetParam();
    const double worst = worstRunningVersusDesignDb(c);
    std::cout << "[ biquad   ] type " << static_cast<int>(c.type) << " gain " << c.gainDb << ": worst " << worst
              << " dB\n";
    EXPECT_LT(worst, 0.01);
}

INSTANTIATE_TEST_SUITE_P(
    AllTypes, BiquadTypes,
    ::testing::Values(TypeCase{BiquadFilterType::LowPass, 0.0f}, TypeCase{BiquadFilterType::HighPass, 0.0f},
                      TypeCase{BiquadFilterType::BandPass, 0.0f}, TypeCase{BiquadFilterType::Notch, 0.0f},
                      TypeCase{BiquadFilterType::Peak, 6.0f}, TypeCase{BiquadFilterType::Peak, -6.0f},
                      TypeCase{BiquadFilterType::LoShelf, 6.0f}, TypeCase{BiquadFilterType::LoShelf, -6.0f},
                      TypeCase{BiquadFilterType::HiShelf, 6.0f}, TypeCase{BiquadFilterType::HiShelf, -6.0f},
                      TypeCase{BiquadFilterType::AllPass, 0.0f}));

TEST(Biquad, DesignedResponsesAtTheirKeyFrequencies)
{
    for (const float fc : {100.0f, 1000.0f, 8000.0f})
    {
        EXPECT_NEAR(designedDb(BiquadFilterType::LowPass, fc, c_butterworthQ, 0.0f, fc), -3.0103, 1e-3) << fc;
        EXPECT_NEAR(designedDb(BiquadFilterType::HighPass, fc, c_butterworthQ, 0.0f, fc), -3.0103, 1e-3) << fc;
        EXPECT_NEAR(designedDb(BiquadFilterType::BandPass, fc, 2.0f, 0.0f, fc), 0.0, 1e-3) << fc;
        EXPECT_LT(designedDb(BiquadFilterType::Notch, fc, 2.0f, 0.0f, fc), -60.0) << fc;
        EXPECT_NEAR(designedDb(BiquadFilterType::Peak, fc, 1.5f, 7.0f, fc), 7.0, 1e-3) << fc;
        EXPECT_NEAR(designedDb(BiquadFilterType::Peak, fc, 1.5f, -7.0f, fc), -7.0, 1e-3) << fc;
        for (const double probe : {30.0, 500.0, 3000.0, 15000.0})
        {
            EXPECT_NEAR(designedDb(BiquadFilterType::AllPass, fc, 0.7f, 0.0f, probe), 0.0, 1e-4) << fc << " " << probe;
        }
    }
    // shelves: full gain far on their side, none on the other
    EXPECT_NEAR(designedDb(BiquadFilterType::LoShelf, 500.0f, c_butterworthQ, 9.0f, 10.0), 9.0, 0.01);
    EXPECT_NEAR(designedDb(BiquadFilterType::LoShelf, 500.0f, c_butterworthQ, 9.0f, 20000.0), 0.0, 0.05);
    EXPECT_NEAR(designedDb(BiquadFilterType::HiShelf, 500.0f, c_butterworthQ, 9.0f, 23900.0), 9.0, 0.01);
    EXPECT_NEAR(designedDb(BiquadFilterType::HiShelf, 500.0f, c_butterworthQ, 9.0f, 10.0), 0.0, 0.01);
}

TEST(Biquad, PeakCutInvertsBoost)
{
    for (const double probe : {50.0, 300.0, 1000.0, 3000.0, 12000.0})
    {
        const double sum = designedDb(BiquadFilterType::Peak, 1000.0f, 0.9f, 8.0f, probe) +
                           designedDb(BiquadFilterType::Peak, 1000.0f, 0.9f, -8.0f, probe);
        EXPECT_NEAR(sum, 0.0, 1e-4) << probe;
    }
}

TEST(Biquad, FrequencyAtNyquistGivesFiniteCoefficients)
{
    // AbacDsp clamps to fs / 2, where tan(pi / 2) is infinite and the lowpass gets NaN
    BiquadCoefficients c;
    c.coefficients(BiquadFilterType::LowPass, c_sampleRate, c_sampleRate, c_butterworthQ, 0.0f);
    const BiquadSection& s = c.getCoefficients();
    for (const float v : {s.b0, s.b1, s.b2, s.a1, s.a2})
    {
        EXPECT_TRUE(std::isfinite(v));
    }
    EXPECT_NEAR(toDb(std::abs(biquadSectionResponse(s, 100.0, c_sampleRate))), 0.0, 0.01);
}

TEST(Biquad, MagnitudeHelpersAgree)
{
    // flat gain-of-2 section: magnitude exactly 2 everywhere (AbacDsp test)
    for (float cf = 0.01f; cf <= 0.49f; cf += 0.05f)
    {
        EXPECT_NEAR(biquadMagnitude(cf, 2.0f, 0.0f, 0.0f, 0.0f, 0.0f), 2.0f, 1e-5f) << cf;
    }
    Biquad<BiquadFilterType::Peak> peak;
    peak.computeCoefficients(c_sampleRate, 1000.0f, c_butterworthQ, 6.0f);
    for (float hz = 50.0f; hz <= 16000.0f; hz *= 1.3f)
    {
        const float cf = hz / c_sampleRate;
        EXPECT_NEAR(peak.magnitude(cf), peak.magnitudeLinear(cf), 1e-4f) << hz;
        EXPECT_NEAR(static_cast<double>(peak.magnitudeLinear(cf)),
                    std::abs(biquadSectionResponse(peak.getCoefficients(), static_cast<double>(hz), c_sampleRate)),
                    1e-5)
            << hz;
    }
}

TEST(Biquad, InPlaceBlockEqualsPerSample)
{
    Biquad<BiquadFilterType::HiShelf> a;
    Biquad<BiquadFilterType::HiShelf> b;
    a.computeCoefficients(c_sampleRate, 2000.0f, 0.8f, -5.0f);
    b.computeCoefficients(c_sampleRate, 2000.0f, 0.8f, -5.0f);
    std::vector<float> block(256);
    for (size_t i = 0; i < block.size(); ++i)
    {
        block[i] = std::sin(0.05f * static_cast<float>(i)) + ((i % 7) == 0 ? 0.3f : 0.0f);
    }
    std::vector<float> expected(block.size());
    for (size_t i = 0; i < block.size(); ++i)
    {
        expected[i] = b.process(block[i]);
    }
    a.processBlock(block.data(), block.data(), block.size());
    EXPECT_EQ(block, expected);
}

TEST(BilinearTransform, FirstOrderHighpassMinusThreeDbAtCutoff)
{
    for (const double fs : {44100.0, 48000.0, 96000.0})
    {
        for (const double fc : {20.0, 90.0, 1000.0, 10000.0})
        {
            const BiquadSection s = firstOrderHighpassSection(fc, fs);
            EXPECT_EQ(s.b2, 0.0f);
            EXPECT_EQ(s.a2, 0.0f);
            EXPECT_NEAR(toDb(std::abs(biquadSectionResponse(s, fc, fs))), -3.0103, 1e-3) << fs << " " << fc;
            EXPECT_NEAR(toDb(std::abs(biquadSectionResponse(s, 0.4999 * fs, fs))), 0.0, 1e-3) << fs << " " << fc;
        }
    }
}

TEST(BilinearTransform, DigitalAtFEqualsAnalogAtTheWarpedFrequency)
{
    // the plain transform maps f to the analog frequency fs / pi tan(pi f / fs), exactly; what
    // remains is the rounding of the float coefficients (measured 1.2e-4 dB)
    constexpr double fs{48000.0};
    constexpr double twoPi{6.283185307179586476925286766559};
    const double w = twoPi * 200.0;
    const AnalogSection lowpass{w * w, 0.0, 0.0, w * w, w / 0.9, 1.0};
    const AnalogSection shelf{1.0, 3e-4, 0.0, 1.0, 1e-4, 0.0};
    for (const AnalogSection& h : {lowpass, shelf})
    {
        const BiquadSection s = bilinearSection(h, 2.0 * fs);
        for (const double f : {10.0, 100.0, 200.0, 1000.0, 5000.0, 15000.0})
        {
            const double warped = fs / c_pi * std::tan(c_pi * f / fs);
            EXPECT_NEAR(toDb(std::abs(biquadSectionResponse(s, f, fs))),
                        toDb(std::abs(analogSectionResponse(h, warped))), 1e-3)
                << f;
        }
    }
}

}
