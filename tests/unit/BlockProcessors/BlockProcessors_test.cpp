#include <array>
#include <cmath>
#include <gtest/gtest.h>
#include <iostream>
#include <vector>

#include "tests/unit/TestHelpers.h"
#include "dsp/BlockProcessors/BlockProcOnePole.h"
#include "dsp/BlockProcessors/BlockProcPitch.h"

namespace jp::test
{

namespace
{
constexpr size_t c_block{16};
constexpr float c_sampleRate{48000.0f};
constexpr float c_probe{237.0f};

[[nodiscard]] std::vector<float> sine(const float frequency, const size_t numSamples)
{
    std::vector<float> signal(numSamples);
    for (size_t i = 0; i < numSamples; ++i)
    {
        signal[i] = static_cast<float>(std::sin(2.0 * c_pi * frequency * static_cast<double>(i) / c_sampleRate));
    }
    return signal;
}

template <typename Processor>
[[nodiscard]] std::vector<float> runBlocks(Processor& sut, const std::vector<float>& input)
{
    std::vector<float> output;
    output.reserve(input.size());
    std::array<float, c_block> block{};
    for (size_t i = 0; i + c_block <= input.size(); i += c_block)
    {
        std::copy_n(input.begin() + static_cast<long>(i), c_block, block.begin());
        sut.process(block);
        output.insert(output.end(), block.begin(), block.end());
    }
    return output;
}

// spectral centroid within +/-10 % of the expected pitch (see PitchFadeWindowDelay_test)
[[nodiscard]] double settledFrequency(const std::vector<float>& signal, const double expected)
{
    constexpr size_t fftSize = 131072;
    const std::vector<float> settled(signal.end() - static_cast<long>(fftSize), signal.end());
    return spectralCentroid(settled, c_sampleRate, expected * 0.9, expected * 1.1);
}
}

TEST(BlockPitch, ZeroMixIsBitExactBypass)
{
    BlockPitch<c_block> sut(c_sampleRate);
    sut.setPitch(12.0f);
    sut.setPitchMix(0.0f);
    const auto input = sine(220.0f, 4000);
    const auto output = runBlocks(sut, input);
    for (size_t i = 0; i < output.size(); ++i)
    {
        ASSERT_EQ(output[i], input[i]) << "at sample " << i;
    }
}

TEST(BlockPitch, OctavesAndUnity)
{
    for (const float semitones : {12.0f, -12.0f, 0.0f})
    {
        BlockPitch<c_block> sut(c_sampleRate);
        sut.setPitch(semitones);
        sut.setPitchMix(1.0f);
        const auto output = runBlocks(sut, sine(c_probe, 48000 * 4));
        const double expected = c_probe * std::pow(2.0, semitones / 12.0);
        const double cents = 1200.0 * std::log2(settledFrequency(output, expected) / expected);
        std::cout << "[ blkpitch ] " << semitones << " st: " << cents << " cents\n";
        EXPECT_LT(std::fabs(cents), 8.0) << semitones;
    }
}

TEST(BlockPitch, ReverseStaysFiniteAndBounded)
{
    BlockPitch<c_block> sut(c_sampleRate);
    sut.setPitch(7.0f);
    sut.setPitchMix(1.0f);
    sut.setReverse(true);
    for (const float y : runBlocks(sut, sine(220.0f, 20000)))
    {
        ASSERT_TRUE(std::isfinite(y));
        ASSERT_LE(std::fabs(y), 1.5f);
    }
}

TEST(BlockPitch, ResetSilencesTheShifter)
{
    BlockPitch<c_block> sut(c_sampleRate);
    sut.setPitch(3.0f);
    sut.setPitchMix(1.0f);
    std::ignore = runBlocks(sut, sine(220.0f, 8000));
    sut.reset();
    for (const float y : runBlocks(sut, std::vector<float>(8000, 0.0f)))
    {
        ASSERT_EQ(y, 0.0f);
    }
}

TEST(BlockOnePole, MatchesTheSampleFilters)
{
    const auto input = sine(1000.0f, 4096);
    BlockLowpass<c_block> lowpassBlock(c_sampleRate, 3000.0f);
    BlockHighpass<c_block> highpassBlock(c_sampleRate, 200.0f);
    OnePoleLowpass lowpass;
    lowpass.setPole(onePoleLowpassPole(3000.0f, c_sampleRate));
    OnePoleHighpass highpass;
    highpass.setPole(onePoleLowpassPole(200.0f, c_sampleRate));
    const auto lowOut = runBlocks(lowpassBlock, input);
    const auto highOut = runBlocks(highpassBlock, input);
    for (size_t i = 0; i < input.size(); ++i)
    {
        ASSERT_EQ(lowOut[i], lowpass.process(input[i])) << i;
        ASSERT_EQ(highOut[i], highpass.process(input[i])) << i;
    }
    lowpassBlock.reset();
    highpassBlock.reset();
    std::array<float, c_block> silence{};
    lowpassBlock.process(silence);
    highpassBlock.process(silence);
    for (const float y : silence)
    {
        EXPECT_EQ(y, 0.0f);
    }
}

}
