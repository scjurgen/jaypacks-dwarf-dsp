// Unit tests of the meter test plugin's pedal: the window and measure protocol, the
// level, frequency and tone results, the bands against a reference, and the counts.

#include <cmath>
#include <gtest/gtest.h>
#include <limits>
#include <vector>

#include "tests/unit/TestHelpers.h"
#include "dsp/Filters/OnePoleLowpass.h"
#include "dsp/Numbers/Random.h"
#include "plugins/meter/MeterPedal.h"

namespace jp::test
{

namespace
{
constexpr double c_fs{48000.0};

std::vector<float> sine(const double frequency, const double amplitude, const size_t length)
{
    std::vector<float> out(length);
    for (size_t i = 0; i < length; ++i)
    {
        out[i] = static_cast<float>(amplitude * std::sin(2.0 * c_pi * frequency * static_cast<double>(i) / c_fs));
    }
    return out;
}

std::vector<float> noise(const size_t length, const float amplitude, const uint64_t seed = 11u)
{
    Random random(seed);
    std::vector<float> out(length);
    for (auto& x : out)
    {
        x = amplitude * (2.0f * random.uniform() - 1.0f);
    }
    return out;
}

MeterPedal make(const float windowSeconds)
{
    MeterPedal meter;
    meter.setSampleRate(static_cast<float>(c_fs));
    meter.setWindow(windowSeconds);
    meter.reset();
    return meter;
}

/// Runs signal and reference through the meter in blocks; returns the output.
std::vector<float> run(MeterPedal& meter, const std::vector<float>& signal, const std::vector<float>& reference,
                       const size_t blockSize = 128)
{
    std::vector<float> out(signal.size());
    for (size_t pos = 0; pos < signal.size(); pos += blockSize)
    {
        const size_t n = std::min(blockSize, signal.size() - pos);
        const float* in[2]{signal.data() + pos, reference.data() + pos};
        float* outs[1]{out.data() + pos};
        meter.process(in, outs, n);
    }
    return out;
}

double db(const double x)
{
    return 20.0 * std::log10(x);
}
}

TEST(MeterPedal, StartsWithoutResults)
{
    auto meter = make(0.5f);
    EXPECT_EQ(meter.getResultId(), 0.0f);
    EXPECT_EQ(meter.getRms(), MeterPedal::c_floorDb);
    EXPECT_EQ(meter.getPeak(), MeterPedal::c_floorDb);
    EXPECT_EQ(meter.getFrequency(), 0.0f);
    EXPECT_EQ(meter.getBand1Coherence(), 0.0f);
    EXPECT_EQ(meter.getBand1Gain(), MeterPedal::c_floorDb);
}

TEST(MeterPedal, PassesTheSignalThroughAlsoInPlace)
{
    auto meter = make(0.5f);
    const auto x = noise(10000, 0.5f);
    const auto r = noise(10000, 0.5f, 2u);
    EXPECT_EQ(run(meter, x, r), x);
    std::vector<float> shared = x;
    const float* in[2]{shared.data(), r.data()};
    float* out[1]{shared.data()};
    meter.process(in, out, shared.size());
    EXPECT_EQ(shared, x);
}

TEST(MeterPedal, SineLevelsFrequencyAndTone)
{
    auto meter = make(0.5f);
    const auto x = sine(1000.0, 0.5, 24000);
    run(meter, x, x);
    EXPECT_NEAR(meter.getPeak(), db(0.5), 1e-3);
    EXPECT_NEAR(meter.getRms(), db(0.5 / std::sqrt(2.0)), 1e-4);
    EXPECT_NEAR(meter.getCrest(), db(std::sqrt(2.0)), 1e-3);
    EXPECT_NEAR(meter.getDc(), 0.0, 1e-6);
    EXPECT_NEAR(meter.getFrequency(), 1000.0, 1e-2);
    EXPECT_NEAR(meter.getToneLevel(), db(0.5), 1e-4);
    EXPECT_LT(meter.getToneResidual(), -120.0f);
    // 48 samples per period with a sample on every zero crossing: the largest step is A sin(w)
    EXPECT_NEAR(meter.getMaxStep(), db(0.5 * std::sin(2.0 * c_pi * 1000.0 / c_fs)), 1e-3);
    EXPECT_NEAR(meter.getRefRms(), meter.getRms(), 1e-6);
    EXPECT_EQ(meter.getDiffPeak(), MeterPedal::c_floorDb) << "signal equals reference";
    EXPECT_EQ(meter.getNonFinite(), 0.0f);
}

TEST(MeterPedal, DiffPeakIsThePeakOfSignalMinusReference)
{
    auto meter = make(0.5f);
    const auto r = sine(440.0, 0.5, 24000);
    auto x = r;
    x[1234] += 0.01f;
    run(meter, x, r);
    EXPECT_NEAR(meter.getDiffPeak(), db(0.01), 1e-3);
}

TEST(MeterPedal, NewMeasureValueStartsAWindowAtTheNextBlock)
{
    auto meter = make(0.1f); // 4800 samples
    const auto x = sine(1000.0, 0.25, 4800 * 4);
    run(meter, std::vector<float>(x.begin(), x.begin() + 1000), std::vector<float>(1000, 0.0f));
    meter.setMeasure(5.0f);
    // the window of 5 starts at the next process() call and ends 4800 samples later
    const std::vector<float> part(x.begin(), x.begin() + 4799);
    run(meter, part, part);
    EXPECT_EQ(meter.getResultId(), 0.0f);
    const std::vector<float> last(1, x[4799]);
    run(meter, last, last);
    EXPECT_EQ(meter.getResultId(), 5.0f);
    EXPECT_NEAR(meter.getToneLevel(), db(0.25), 1e-4);
    // the same value again is no new request
    meter.setMeasure(5.0f);
    run(meter, std::vector<float>(100, 0.0f), std::vector<float>(100, 0.0f));
    EXPECT_NEAR(meter.getToneLevel(), db(0.25), 1e-4);
}

TEST(MeterPedal, SettingsApplyAtTheNextWindowAndMeasureUsesTheLatest)
{
    auto meter = make(0.1f);
    const auto x = sine(2000.0, 0.25, 4800 * 3);
    meter.setToneHz(2000.0f);
    meter.setMeasure(1.0f); // same block as the tone change: the new window uses 2 kHz
    run(meter, std::vector<float>(x.begin(), x.begin() + 4800), std::vector<float>(4800, 0.0f));
    EXPECT_EQ(meter.getResultId(), 1.0f);
    EXPECT_NEAR(meter.getToneLevel(), db(0.25), 1e-4);
    // no new measure value: the window running now (started at sample 4800) keeps 2 kHz, the
    // one after it uses 500 Hz
    meter.setToneHz(500.0f);
    run(meter, std::vector<float>(x.begin() + 4800, x.begin() + 9600), std::vector<float>(4800, 0.0f));
    EXPECT_NEAR(meter.getToneLevel(), db(0.25), 1e-4);
    run(meter, std::vector<float>(x.begin() + 9600, x.end()), std::vector<float>(4800, 0.0f));
    EXPECT_LT(meter.getToneLevel(), -60.0f);
    EXPECT_EQ(meter.getResultId(), 1.0f);
}

TEST(MeterPedal, BandsMeasureGainAndCoherenceAgainstTheReference)
{
    auto meter = make(4.0f);
    const float pole = onePoleLowpassPole(1000.0f, static_cast<float>(c_fs));
    OnePoleLowpass lowpass;
    lowpass.setPole(pole);
    const auto r = noise(48000 * 4, 0.5f);
    std::vector<float> x(r.size());
    for (size_t i = 0; i < r.size(); ++i)
    {
        x[i] = lowpass.process(r[i]);
    }
    run(meter, x, r);
    const std::array<double, 8> hz{63.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0};
    const std::array<float, 8> gains{meter.getBand1Gain(), meter.getBand2Gain(), meter.getBand3Gain(),
                                     meter.getBand4Gain(), meter.getBand5Gain(), meter.getBand6Gain(),
                                     meter.getBand7Gain(), meter.getBand8Gain()};
    const std::array<float, 8> coherences{
        meter.getBand1Coherence(), meter.getBand2Coherence(), meter.getBand3Coherence(), meter.getBand4Coherence(),
        meter.getBand5Coherence(), meter.getBand6Coherence(), meter.getBand7Coherence(), meter.getBand8Coherence()};
    for (size_t k = 0; k < hz.size(); ++k)
    {
        const double theory = toDb(std::abs(onePoleLowpassResponse(pole, hz[k], c_fs)));
        EXPECT_NEAR(gains[k], theory, 0.05) << hz[k];
        EXPECT_GT(coherences[k], 0.999f) << hz[k];
    }
    EXPECT_LT(meter.getBand8Level(), meter.getBand1Level());
}

TEST(MeterPedal, BandLevelOfASineOnABinCentre)
{
    auto meter = make(1.0f);
    meter.setBand1Hz(static_cast<float>(c_fs * 341.0 / MeterPedal::c_bandBlockSize)); // bin 341, 999.0 Hz
    meter.setMeasure(1.0f);
    const auto x = sine(c_fs * 341.0 / MeterPedal::c_bandBlockSize, 0.1, 48000);
    run(meter, x, x);
    EXPECT_NEAR(meter.getBand1Level(), -20.0, 0.01);
    EXPECT_NEAR(meter.getBand1Gain(), 0.0, 1e-4);
}

TEST(MeterPedal, AWindowShorterThanABandBlockGivesNoBandResult)
{
    auto meter = make(0.2f); // 9600 samples < 16384
    const auto x = noise(9600, 0.5f);
    run(meter, x, x);
    EXPECT_EQ(meter.getBand1Gain(), MeterPedal::c_floorDb);
    EXPECT_EQ(meter.getBand1Coherence(), 0.0f);
    EXPECT_GT(meter.getRms(), -20.0f) << "the other results are there";
}

TEST(MeterPedal, CountsNonFiniteAndDenormalSamples)
{
    auto meter = make(0.1f);
    auto x = sine(1000.0, 0.5, 4800);
    x[100] = std::numeric_limits<float>::quiet_NaN();
    x[200] = std::numeric_limits<float>::infinity();
    x[300] = -std::numeric_limits<float>::infinity();
    x[400] = std::numeric_limits<float>::denorm_min();
    run(meter, x, std::vector<float>(x.size(), 0.0f));
    EXPECT_EQ(meter.getNonFinite(), 3.0f);
    EXPECT_EQ(meter.getDenormals(), 1.0f);
    EXPECT_NEAR(meter.getPeak(), db(0.5), 1e-3);
    EXPECT_TRUE(std::isfinite(meter.getToneLevel()));
    EXPECT_TRUE(std::isfinite(meter.getDiffPeak()));
    EXPECT_TRUE(std::isfinite(meter.getBand1Gain()));
}

TEST(MeterPedal, ResultsDoNotDependOnTheBlockSize)
{
    const auto x = noise(48000, 0.5f);
    const auto r = noise(48000, 0.5f, 3u);
    auto a = make(0.4f);
    auto b = make(0.4f);
    run(a, x, r, 1);
    run(b, x, r, 4096);
    EXPECT_EQ(a.getRms(), b.getRms());
    EXPECT_EQ(a.getBand3Gain(), b.getBand3Gain());
    EXPECT_EQ(a.getToneResidual(), b.getToneResidual());
}

}
