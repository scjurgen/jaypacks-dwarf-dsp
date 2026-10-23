#include <chrono>
#include <cmath>
#include <complex>
#include <gtest/gtest.h>
#include <iostream>
#include <vector>

#include "dsp/Wavetables/WaveTableBank.h"

namespace jp::test
{

namespace
{
constexpr size_t c_harmonics[]{1024, 683, 455, 303, 202, 135, 90, 60, 40, 27, 18, 12, 8, 5, 3, 2};
constexpr BasicWave c_tonal[]{BasicWave::Sine,     BasicWave::Triangle,      BasicWave::Saw,
                              BasicWave::SharkFin, BasicWave::Square,        BasicWave::Pulse,
                              BasicWave::Pulse1,   BasicWave::RectifiedSine, BasicWave::RectifiedTriangle};

// all eleven waveforms are built once for the whole suite
const WaveTableBank& allWaves()
{
    static const WaveTableBank bank;
    return bank;
}

double rms(const WaveTable& table)
{
    double sum = 0.0;
    for (size_t n = 0; n < c_waveTableSize; ++n)
    {
        sum += static_cast<double>(table.data[n]) * static_cast<double>(table.data[n]);
    }
    return std::sqrt(sum / static_cast<double>(c_waveTableSize));
}

double mean(const WaveTable& table)
{
    double sum = 0.0;
    for (size_t n = 0; n < c_waveTableSize; ++n)
    {
        sum += static_cast<double>(table.data[n]);
    }
    return sum / static_cast<double>(c_waveTableSize);
}

/// |X_k| / N * 2 of one cycle: the amplitude of harmonic k.
double harmonicAmplitude(const WaveTable& table, const size_t k)
{
    constexpr double twoPi{6.283185307179586};
    std::complex<double> sum{};
    for (size_t n = 0; n < c_waveTableSize; ++n)
    {
        const double angle =
            -twoPi * static_cast<double>((k * n) % c_waveTableSize) / static_cast<double>(c_waveTableSize);
        sum += static_cast<double>(table.data[n]) * std::complex<double>(std::cos(angle), std::sin(angle));
    }
    return 2.0 * std::abs(sum) / static_cast<double>(c_waveTableSize);
}
}

TEST(WaveTableBank, EveryWaveformHasSixteenLevelsWithTheHarmonicCountsOfTheDesign)
{
    for (int wave = 0; wave < static_cast<int>(BasicWave::Last); ++wave)
    {
        const WaveTableSet* set = allWaves().find(static_cast<BasicWave>(wave));
        ASSERT_NE(set, nullptr) << wave;
        ASSERT_EQ(set->tables.size(), std::size(c_harmonics)) << wave;
        for (size_t level = 0; level < set->tables.size(); ++level)
        {
            EXPECT_FLOAT_EQ(set->tables[level].topFreq, 0.6f / static_cast<float>(c_harmonics[level])) << level;
        }
    }
}

TEST(WaveTableBank, TopFrequenciesIncreaseAndTheWrapSampleCopiesTheFirst)
{
    for (int wave = 0; wave < static_cast<int>(BasicWave::Last); ++wave)
    {
        const WaveTableSet* set = allWaves().find(static_cast<BasicWave>(wave));
        for (size_t level = 0; level < set->tables.size(); ++level)
        {
            EXPECT_EQ(set->tables[level].data[c_waveTableSize], set->tables[level].data[0]);
            if (level > 0)
            {
                EXPECT_GT(set->tables[level].topFreq, set->tables[level - 1].topFreq);
            }
        }
    }
}

TEST(WaveTableBank, LevelForPicksTheFirstLevelAboveTheIncrementElseTheLast)
{
    const WaveTableSet& set = *allWaves().find(BasicWave::Saw);
    EXPECT_EQ(set.levelFor(0.0f), 0u);
    EXPECT_EQ(set.levelFor(0.0005f), 0u) << "below 0.6 / 1024";
    EXPECT_EQ(set.levelFor(0.6f / 1024.0f), 1u) << "the topFreq itself is the first increment of the next level";
    EXPECT_EQ(set.levelFor(0.0416f), 11u) << "twice a 1 kHz increment at 48 kHz: 12 harmonics";
    EXPECT_EQ(set.levelFor(0.3f), 15u) << "above 0.6 / 3 and 0.6 / 2: the last level";
    EXPECT_EQ(set.levelFor(0.45f), 15u);
    EXPECT_EQ(WaveTableSet{}.levelFor(0.1f), 0u) << "an empty set does not index out of range";
}

TEST(WaveTableBank, TonalFirstLevelsHaveRmsHalfAndNoDc)
{
    for (const BasicWave wave : c_tonal)
    {
        const WaveTableSet& set = *allWaves().find(wave);
        EXPECT_NEAR(rms(set.tables[0]), 0.5, 1e-6) << static_cast<int>(wave);
        for (const WaveTable& table : set.tables)
        {
            EXPECT_NEAR(mean(table), 0.0, 1e-6) << static_cast<int>(wave);
        }
    }
}

TEST(WaveTableBank, EveryLevelKeepsExactlyItsHarmonicsAndTheGainOfTheFirst)
{
    for (const BasicWave wave : {BasicWave::Saw, BasicWave::Square, BasicWave::SharkFin, BasicWave::Pulse1})
    {
        const WaveTableSet& set = *allWaves().find(wave);
        const WaveTable& first = set.tables[0];
        for (size_t level = 1; level < set.tables.size(); ++level)
        {
            const size_t kept = c_harmonics[level];
            for (const size_t k : {size_t{1}, size_t{2}, size_t{3}, kept - 1, kept})
            {
                if (k >= 1 && k <= kept)
                {
                    EXPECT_NEAR(harmonicAmplitude(set.tables[level], k), harmonicAmplitude(first, k), 2e-6)
                        << static_cast<int>(wave) << " level " << level << " harmonic " << k;
                }
            }
            for (const size_t k : {kept + 1, kept + 2, kept + 40})
            {
                EXPECT_LT(harmonicAmplitude(set.tables[level], k), 1e-6)
                    << static_cast<int>(wave) << " level " << level << " harmonic " << k;
            }
        }
    }
}

TEST(WaveTableBank, TheShapesKeepTheirDirection)
{
    const WaveTable& saw = allWaves().find(BasicWave::Saw)->tables[0];
    EXPECT_LT(saw.data[100], saw.data[1000]) << "the ramp rises";
    EXPECT_GT(saw.data[1900], saw.data[1000]);
    const WaveTable& square = allWaves().find(BasicWave::Square)->tables[0];
    EXPECT_GT(square.data[512], 0.45f);
    EXPECT_LT(square.data[1536], -0.45f);
    const WaveTable& triangle = allWaves().find(BasicWave::Triangle)->tables[0];
    EXPECT_GT(triangle.data[100], triangle.data[1024]) << "peak at the start, trough in the middle";
    const WaveTable& pulse = allWaves().find(BasicWave::Pulse)->tables[0];
    EXPECT_GT(pulse.data[200], 0.0f) << "high for the first 20 %";
    EXPECT_LT(pulse.data[1000], 0.0f);
    const WaveTable& fin = allWaves().find(BasicWave::SharkFin)->tables[0];
    EXPECT_GT(fin.data[80], fin.data[10]) << "the fast rise comes first";
    EXPECT_GT(fin.data[80], fin.data[1000]);
}

TEST(WaveTableBank, ASineIsTheSameInEveryLevel)
{
    const WaveTableSet& set = *allWaves().find(BasicWave::Sine);
    for (size_t level = 1; level < set.tables.size(); ++level)
    {
        for (size_t n = 0; n < c_waveTableSize; n += 17)
        {
            EXPECT_NEAR(set.tables[level].data[n], set.tables[0].data[n], 2e-6f);
        }
    }
    EXPECT_NEAR(harmonicAmplitude(set.tables[0], 1), 0.5 * std::sqrt(2.0), 1e-6);
}

TEST(WaveTableBank, NoiseTablesAreQuietDeterministicAndNotTheSame)
{
    const WaveTableBank other({BasicWave::White, BasicWave::NoiseFloor});
    for (const BasicWave wave : {BasicWave::White, BasicWave::NoiseFloor})
    {
        float peak = 0.0f;
        for (size_t n = 0; n < c_waveTableSize; ++n)
        {
            peak = std::max(peak, std::abs(allWaves().find(wave)->tables[0].data[n]));
            EXPECT_EQ(allWaves().find(wave)->tables[0].data[n], other.find(wave)->tables[0].data[n]);
        }
        EXPECT_NEAR(peak, 1e-5f, 1e-9f);
    }
    EXPECT_NE(allWaves().find(BasicWave::White)->tables[0].data[5],
              allWaves().find(BasicWave::NoiseFloor)->tables[0].data[5]);
}

TEST(WaveTableBank, OnlyTheListedWaveformsAreBuilt)
{
    const WaveTableBank bank({BasicWave::Saw, BasicWave::Square, BasicWave::Saw, BasicWave::Last});
    EXPECT_TRUE(bank.contains(BasicWave::Saw));
    EXPECT_TRUE(bank.contains(BasicWave::Square));
    EXPECT_FALSE(bank.contains(BasicWave::Sine));
    EXPECT_EQ(bank.find(BasicWave::Triangle), nullptr);
    EXPECT_EQ(bank.find(BasicWave::Last), nullptr);
    EXPECT_EQ(bank.memoryBytes(), 2u * std::size(c_harmonics) * sizeof(WaveTable));
    const auto start = std::chrono::steady_clock::now();
    const WaveTableBank everything;
    const double milliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    EXPECT_EQ(everything.memoryBytes(), 11u * std::size(c_harmonics) * sizeof(WaveTable));
    std::cout << "[ memory   ] " << bank.memoryBytes() / 2 << " bytes per waveform, " << everything.memoryBytes()
              << " bytes for all eleven, built in " << milliseconds << " ms\n";
}

}
