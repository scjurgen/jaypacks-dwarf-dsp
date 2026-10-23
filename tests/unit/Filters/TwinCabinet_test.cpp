#include <cmath>
#include <fstream>
#include <gtest/gtest.h>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "dsp/Filters/TwinCabinet.h"
#include "tests/unit/TestHelpers.h"

namespace jp::test
{

namespace
{
std::vector<std::pair<double, double>> readCurve()
{
    std::ifstream file(std::string(JP_SOURCE_DIR) + "/documentation/JaysTwin/c12k_8ohm.csv");
    std::vector<std::pair<double, double>> curve;
    std::string line;
    while (std::getline(file, line))
    {
        if (line.empty() || line[0] == '#')
        {
            continue;
        }
        std::istringstream fields(line);
        double f = 0.0;
        double db = 0.0;
        fields >> f >> db;
        curve.emplace_back(f, db);
    }
    return curve;
}

/// The speaker sections only (without the open back).
double speakerDb(const double frequencyHz, const float sampleRate)
{
    std::complex<double> h{1.0, 0.0};
    for (const auto& d : c_jensenC12KSections)
    {
        h *= biquadSectionResponse(designBiquad(d, sampleRate), frequencyHz, sampleRate);
    }
    return toDb(std::abs(h));
}
}

TEST(TwinCabinet, SpeakerSectionsFitTheDatasheetCurve)
{
    // the same measure as fit_cabinet.py: weighted least squares 30 Hz to 8 kHz, best level offset
    const auto curve = readCurve();
    ASSERT_EQ(curve.size(), 105u);
    double sumW = 0.0;
    double sumWE = 0.0;
    std::vector<std::pair<double, double>> errors;
    for (const auto& [f, db] : curve)
    {
        if (f < 30.0 || f > 8000.0)
        {
            continue;
        }
        const double w = f > 6000.0 ? 0.3 : 1.0;
        const double e = speakerDb(f, 48000.0f) - db;
        errors.emplace_back(f, e);
        sumW += w;
        sumWE += w * e;
    }
    const double offset = -sumWE / sumW;
    double sumWE2 = 0.0;
    double worstBelow6k = 0.0;
    for (const auto& [f, e] : errors)
    {
        const double w = f > 6000.0 ? 0.3 : 1.0;
        sumWE2 += w * (e + offset) * (e + offset);
        if (f <= 6000.0)
        {
            worstBelow6k = std::max(worstBelow6k, std::abs(e + offset));
        }
    }
    const double rms = std::sqrt(sumWE2 / sumW);
    std::cout << "[ cabinet  ] C12K fit: rms " << rms << " dB, worst up to 6 kHz " << worstBelow6k << " dB, level "
              << offset << " dB SPL\n";
    EXPECT_LT(rms, 0.7);
    EXPECT_LT(worstBelow6k, 2.5);
    EXPECT_NEAR(offset, 99.9, 0.5);
}

TEST(TwinCabinet, FallsAboveTheDataAndPassesTheMiddle)
{
    BiquadCascade<c_twinCabinetSections> cabinet;
    designTwinCabinet(cabinet, 48000.0f);
    for (const double f : {10000.0, 12000.0, 15000.0, 19000.0, 23000.0})
    {
        EXPECT_LT(toDb(std::abs(cabinet.response(f, 48000.0))), -25.0) << f;
    }
    const double at1k = toDb(std::abs(cabinet.response(1000.0, 48000.0)));
    std::cout << "[ cabinet  ] at 1 kHz " << at1k << " dB, 100 Hz " << toDb(std::abs(cabinet.response(100.0, 48000.0)))
              << " dB, 2.3 kHz " << toDb(std::abs(cabinet.response(2300.0, 48000.0))) << " dB, 6 kHz "
              << toDb(std::abs(cabinet.response(6000.0, 48000.0))) << " dB\n";
    EXPECT_NEAR(at1k, 0.0, 1.0);
    // the open back takes the low end further down than the baffle curve
    EXPECT_NEAR(toDb(std::abs(cabinet.response(80.0, 48000.0))) - speakerDb(80.0, 48000.0f), -3.01, 0.01);
}

TEST(TwinCabinet, RunningCascadeMatchesItsResponse)
{
    BiquadCascade<c_twinCabinetSections> cabinet;
    designTwinCabinet(cabinet, 48000.0f);
    for (const double f : {90.0, 410.0, 2300.0, 5000.0})
    {
        cabinet.reset();
        const double measured =
            measureSineGain([&cabinet](const float x) { return cabinet.process(x); }, f, 48000.0, 0.1, 0.5, 0.5);
        EXPECT_NEAR(toDb(measured), toDb(std::abs(cabinet.response(f, 48000.0))), 0.01) << f;
    }
}

TEST(TwinCabinet, OtherRatesKeepTheShapeBelowSixKilohertz)
{
    for (const float fs : {44100.0f, 96000.0f})
    {
        double worst = 0.0;
        for (double f = 40.0; f <= 6000.0; f *= 1.05)
        {
            worst = std::max(worst, std::abs(speakerDb(f, fs) - speakerDb(f, 48000.0f)));
        }
        std::cout << "[ cabinet  ] fs " << fs << " vs 48 kHz up to 6 kHz: worst " << worst << " dB\n";
        EXPECT_LT(worst, 1.0);
    }
}

}
