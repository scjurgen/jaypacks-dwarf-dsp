// Unit tests of the analysis blocks in dsp/Analysis/ (used by the host integration tests and,
// from step 9, by the meter plugin on the device).

#include <cmath>
#include <gtest/gtest.h>
#include <limits>
#include <vector>

#include "tests/unit/TestHelpers.h"
#include "dsp/Analysis/GoertzelBand.h"
#include "dsp/Analysis/SignalStats.h"
#include "dsp/Analysis/ToneFit.h"
#include "dsp/Analysis/ZeroCrossingFrequency.h"
#include "dsp/Filters/OnePoleLowpass.h"
#include "dsp/Numbers/Random.h"

namespace jp::test
{

namespace
{
constexpr double c_fs{48000.0};

std::vector<float> sine(const double frequency, const double amplitude, const size_t length, const double phase = 0.0,
                        const double dc = 0.0)
{
    std::vector<float> out(length);
    for (size_t i = 0; i < length; ++i)
    {
        out[i] = static_cast<float>(dc + amplitude *
                                             std::sin(2.0 * c_pi * frequency * static_cast<double>(i) / c_fs + phase));
    }
    return out;
}

std::vector<float> noise(const size_t length, const float amplitude, const uint64_t seed = 7u)
{
    Random random(seed);
    std::vector<float> out(length);
    for (auto& x : out)
    {
        x = amplitude * (2.0f * random.uniform() - 1.0f);
    }
    return out;
}
}

// SignalStats

TEST(SignalStats, PeakRmsDcAndCrestOfASine)
{
    SignalStats stats;
    const auto x = sine(1000.0, 0.5, 48000, 0.0, 0.1);
    stats.add(x.data(), x.size());
    EXPECT_EQ(stats.count(), 48000u);
    EXPECT_NEAR(stats.peak(), 0.6, 1e-6);
    EXPECT_NEAR(stats.dc(), 0.1, 1e-6);
    EXPECT_NEAR(stats.rms(), std::sqrt(0.01 + 0.125), 1e-6);
    EXPECT_NEAR(stats.crest(), 0.6 / std::sqrt(0.135), 1e-5);
    // largest step of a sampled sine: 2 A sin(w / 2), approached where the samples straddle a zero
    EXPECT_NEAR(stats.maxDelta(), 2.0 * 0.5 * std::sin(c_pi * 1000.0 / c_fs), 3e-4);
}

TEST(SignalStats, StepShowsInMaxDelta)
{
    SignalStats stats;
    for (const float x : {0.0f, 0.01f, 0.02f, 0.52f, 0.53f})
    {
        stats.add(x);
    }
    EXPECT_NEAR(stats.maxDelta(), 0.5, 1e-6);
}

TEST(SignalStats, CountsNonFiniteAndDenormalSamplesAndSkipsNonFinite)
{
    SignalStats stats;
    stats.add(1.0f);
    stats.add(std::numeric_limits<float>::quiet_NaN());
    stats.add(std::numeric_limits<float>::infinity());
    stats.add(std::numeric_limits<float>::denorm_min());
    stats.add(1e-39f);
    stats.add(std::numeric_limits<float>::min()); // smallest normal
    EXPECT_EQ(stats.nonFinite(), 2u);
    EXPECT_EQ(stats.denormals(), 2u);
    EXPECT_EQ(stats.count(), 4u);
    EXPECT_EQ(stats.peak(), 1.0);
}

TEST(SignalStats, SilenceAndReset)
{
    SignalStats stats;
    EXPECT_EQ(stats.rms(), 0.0);
    EXPECT_EQ(stats.crest(), 0.0);
    stats.add(0.7f);
    stats.reset();
    EXPECT_EQ(stats.count(), 0u);
    EXPECT_EQ(stats.peak(), 0.0);
}

// ToneFit

TEST(ToneFit, RecoversAmplitudePhaseAndDcForAnyWindowLength)
{
    for (const size_t length : {37u, 480u, 4801u, 100000u})
    {
        const auto x = sine(1234.5, 0.3, length, 0.7, -0.05);
        ToneFit fit(1234.5, c_fs);
        fit.add(x.data(), x.size());
        const ToneFit::Result r = fit.result();
        EXPECT_NEAR(r.amplitude, 0.3, 1e-6) << length;
        EXPECT_NEAR(r.phase, 0.7, 1e-5) << length;
        EXPECT_NEAR(r.dc, -0.05, 1e-6) << length;
        EXPECT_LT(r.residualRms, 1e-7) << length;
    }
}

TEST(ToneFit, ResidualIsTheAddedNoise)
{
    auto x = sine(440.0, 0.5, 96000);
    const auto n = noise(x.size(), 0.01f);
    for (size_t i = 0; i < x.size(); ++i)
    {
        x[i] += n[i];
    }
    ToneFit fit(440.0, c_fs);
    fit.add(x.data(), x.size());
    const double noiseRms = 0.01 / std::sqrt(3.0);
    EXPECT_NEAR(fit.result().residualRms, noiseRms, 0.02 * noiseRms);
    EXPECT_NEAR(fit.result().amplitude, 0.5, 2e-4);
}

TEST(ToneFit, AmplitudeStepLeavesAResidual)
{
    auto x = sine(1000.0, 0.5, 48000);
    for (size_t i = 24000; i < x.size(); ++i)
    {
        x[i] *= 0.9f;
    }
    ToneFit fit(1000.0, c_fs);
    fit.add(x.data(), x.size());
    // the fit settles at 0.475: half the window a sine of 0.025 too much, half 0.025 too little
    EXPECT_NEAR(fit.result().amplitude, 0.475, 1e-4);
    EXPECT_NEAR(fit.result().residualRms, 0.025 / std::sqrt(2.0), 1e-4);
}

TEST(ToneFit, OscillatorStaysAccurateOverLongRuns)
{
    ToneFit fit(997.0, c_fs);
    const size_t length = 48000u * 60u;
    for (size_t i = 0; i < length; ++i)
    {
        fit.add(static_cast<float>(0.25 * std::sin(2.0 * c_pi * 997.0 * static_cast<double>(i) / c_fs)));
    }
    EXPECT_NEAR(fit.result().amplitude, 0.25, 1e-6);
    EXPECT_LT(fit.result().residualRms, 1e-7);
}

TEST(ToneFit, TooFewSamplesOrDcOnlyGiveZero)
{
    ToneFit fit(1000.0, c_fs);
    fit.add(1.0f);
    fit.add(0.5f);
    EXPECT_EQ(fit.result().amplitude, 0.0);
    ToneFit dcOnly(0.0, c_fs);
    for (int i = 0; i < 100; ++i)
    {
        dcOnly.add(0.3f);
    }
    EXPECT_EQ(dcOnly.result().amplitude, 0.0);
}

// GoertzelBand and TransferBand

TEST(GoertzelBand, ReadsTheAmplitudeOfASineOnABinCentre)
{
    const uint32_t blockSize = 4800; // bins of 10 Hz
    GoertzelBand band(1000.0, c_fs, blockSize);
    const auto x = sine(1000.0, 0.25, blockSize * 5, 0.3);
    for (const float v : x)
    {
        band.add(v);
    }
    EXPECT_EQ(band.blocks(), 5u);
    EXPECT_NEAR(band.amplitude(), 0.25, 1e-5);
}

TEST(GoertzelBand, HalfABinOffReadsAtMostThe1Point42DbScallopLow)
{
    const uint32_t blockSize = 4800;
    GoertzelBand band(1005.0, c_fs, blockSize);
    for (const float v : sine(1000.0, 0.25, blockSize * 4))
    {
        band.add(v);
    }
    EXPECT_NEAR(toDb(band.amplitude() / 0.25), -1.42, 0.02);
}

TEST(GoertzelBand, ToneFiveBinsAwayLeaksLessThanMinus60Db)
{
    const uint32_t blockSize = 4800;
    GoertzelBand band(1050.0, c_fs, blockSize);
    for (const float v : sine(1000.0, 0.25, blockSize * 4))
    {
        band.add(v);
    }
    EXPECT_LT(toDb(band.amplitude() / 0.25), -60.0);
}

TEST(GoertzelBand, LastBlockPhaseRefersToTheBlockStart)
{
    const uint32_t blockSize = 4800;
    GoertzelBand band(1000.0, c_fs, blockSize);
    for (const float v : sine(1000.0, 0.25, blockSize, 0.0))
    {
        band.add(v);
    }
    // sin(w n) = Re(-j e^{j w n}): the DFT of a sine starting at phase 0 points along -j
    EXPECT_NEAR(std::arg(band.lastBlock()), -c_pi / 2.0, 1e-6);
}

TEST(TransferBand, MeasuresAOnePoleLowpassFromNoise)
{
    const float pole = onePoleLowpassPole(1000.0f, static_cast<float>(c_fs));
    OnePoleLowpass lowpass;
    lowpass.setPole(pole);
    const auto x = noise(48000 * 10, 0.5f);
    std::vector<TransferBand> bands;
    const std::vector<double> frequencies{100.0, 777.0, 1000.0, 3333.0, 12000.0};
    for (const double f : frequencies)
    {
        bands.emplace_back(f, c_fs, 4096);
    }
    for (const float v : x)
    {
        const float y = lowpass.process(v);
        for (auto& band : bands)
        {
            band.add(v, y);
        }
    }
    for (size_t k = 0; k < frequencies.size(); ++k)
    {
        const std::complex<double> theory = onePoleLowpassResponse(pole, frequencies[k], c_fs);
        EXPECT_NEAR(toDb(bands[k].gain()), toDb(std::abs(theory)), 0.05) << frequencies[k];
        EXPECT_NEAR(bands[k].phase(), std::arg(theory), 0.01) << frequencies[k];
        EXPECT_GT(bands[k].coherence(), 0.999) << frequencies[k];
    }
}

TEST(TransferBand, ReportsBothAmplitudes)
{
    const uint32_t blockSize = 4800;
    TransferBand band(1000.0, c_fs, blockSize);
    const auto x = sine(1000.0, 0.5, blockSize * 3);
    for (const float v : x)
    {
        band.add(v, 0.25f * v);
    }
    EXPECT_NEAR(band.inputAmplitude(), 0.5, 1e-5);
    EXPECT_NEAR(band.outputAmplitude(), 0.125, 1e-5);
    EXPECT_NEAR(band.gain(), 0.25, 1e-9);
}

TEST(TransferBand, CoherenceDropsWithUnrelatedOutput)
{
    TransferBand band(1000.0, c_fs, 4096);
    const auto x = noise(48000 * 4, 0.5f, 1u);
    const auto y = noise(48000 * 4, 0.5f, 2u);
    for (size_t i = 0; i < x.size(); ++i)
    {
        band.add(x[i], y[i]);
    }
    EXPECT_LT(band.coherence(), 0.2);
}

// ZeroCrossingFrequency

TEST(ZeroCrossingFrequency, SineFrequencyToAFractionOfAPpm)
{
    for (const double f : {50.0, 440.0, 1234.567, 9000.0})
    {
        ZeroCrossingFrequency counter(c_fs, 0.01f);
        counter.add(sine(f, 0.5, 48000, 0.3).data(), 48000);
        EXPECT_NEAR(counter.frequency() / f, 1.0, 1e-5) << f;
    }
}

TEST(ZeroCrossingFrequency, HysteresisIgnoresNoiseAroundZero)
{
    auto x = sine(440.0, 0.5, 48000);
    const auto n = noise(x.size(), 0.02f);
    for (size_t i = 0; i < x.size(); ++i)
    {
        x[i] += n[i];
    }
    ZeroCrossingFrequency counter(c_fs, 0.05f);
    counter.add(x.data(), x.size());
    EXPECT_EQ(counter.crossings(), 439u); // the start at t = 0 is not armed
    EXPECT_NEAR(counter.frequency(), 440.0, 0.1);
    ZeroCrossingFrequency noHysteresis(c_fs, 0.0f);
    noHysteresis.add(x.data(), x.size());
    EXPECT_GT(noHysteresis.crossings(), 439u);
}

TEST(ZeroCrossingFrequency, SilenceGivesZero)
{
    ZeroCrossingFrequency counter(c_fs, 0.01f);
    counter.add(std::vector<float>(4800, 0.0f).data(), 4800);
    EXPECT_EQ(counter.frequency(), 0.0);
}

}
