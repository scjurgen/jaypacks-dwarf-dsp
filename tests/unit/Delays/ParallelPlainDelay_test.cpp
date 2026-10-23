#include <array>
#include <cmath>
#include <gtest/gtest.h>
#include <iostream>
#include <vector>

#include "dsp/Delays/ParallelPlainDelay.h"

namespace jp::test
{

namespace
{
constexpr size_t c_block{4};
using Delay2 = ParallelPlainDelay<c_block, 2>;
using Block2 = Delay2::Block;

/// Feeds an impulse on both channels at sample `at` and returns the sample index at which
/// each channel's output carries it.
template <typename DelayType>
std::array<long, 2> impulseArrival(DelayType& sut, const size_t at, const size_t blocks)
{
    std::array<long, 2> arrival{-1, -1};
    typename DelayType::Block src{};
    typename DelayType::Block target{};
    for (size_t b = 0; b < blocks; ++b)
    {
        for (size_t c = 0; c < 2; ++c)
        {
            src[c].fill(0.0f);
            if (at >= b * c_block && at < (b + 1) * c_block)
            {
                src[c][at - b * c_block] = 1.0f;
            }
        }
        sut.processBlock(src, target);
        for (size_t c = 0; c < 2; ++c)
        {
            for (size_t i = 0; i < c_block; ++i)
            {
                if (target[c][i] == 1.0f)
                {
                    arrival[c] = static_cast<long>(b * c_block + i);
                }
            }
        }
    }
    return arrival;
}
}

TEST(ParallelPlainDelay, CheckSumAndSingle)
{
    Delay2 sut(16);
    sut.setSize(0, 10);
    sut.setSize(1, 7);
    const auto arrival = impulseArrival(sut, 12, 8);
    EXPECT_EQ(arrival[0], 22);
    EXPECT_EQ(arrival[1], 19);
}

TEST(ParallelPlainDelay, CheckShortDelay)
{
    Delay2 sut(16);
    sut.setSize(0, 1);
    sut.setSize(1, 2);
    const auto arrival = impulseArrival(sut, 12, 5);
    EXPECT_EQ(arrival[0], 13);
    EXPECT_EQ(arrival[1], 14);
}

TEST(ParallelPlainDelay, ZeroDelayReturnsTheBlockJustWritten)
{
    Delay2 sut(16);
    sut.setSize(0, 0);
    sut.setSize(1, 0);
    const auto arrival = impulseArrival(sut, 5, 3);
    EXPECT_EQ(arrival[0], 5);
    EXPECT_EQ(arrival[1], 5);
}

TEST(ParallelPlainDelay, OverflowProtected)
{
    // after 4 blocks the write position wraps (16 / 4)
    Delay2 sut(16);
    sut.setSize(0, 1);
    sut.setSize(1, 2);
    const auto arrival = impulseArrival(sut, 16, 6);
    EXPECT_EQ(arrival[0], 17);
    EXPECT_EQ(arrival[1], 18);
}

TEST(ParallelPlainDelay, SizeSetWhileRunningIsExact)
{
    // AbacDsp wrapped a new read position by the buffer length instead of the ring length,
    // so a size set with the write position past the start came out two blocks longer
    for (size_t warmBlocks = 0; warmBlocks < 12; ++warmBlocks)
    {
        Delay2 sut(32);
        Block2 src{};
        Block2 target{};
        for (size_t b = 0; b < warmBlocks; ++b)
        {
            sut.processBlock(src, target);
        }
        sut.setSize(0, 3);
        sut.setSize(1, 21);
        const size_t start = warmBlocks * c_block;
        std::array<long, 2> arrival{-1, -1};
        for (size_t b = 0; b < 10; ++b)
        {
            src[0].fill(0.0f);
            src[1].fill(0.0f);
            if (b == 0)
            {
                src[0][0] = 1.0f;
                src[1][0] = 1.0f;
            }
            sut.processBlock(src, target);
            for (size_t c = 0; c < 2; ++c)
            {
                for (size_t i = 0; i < c_block; ++i)
                {
                    if (target[c][i] == 1.0f)
                    {
                        arrival[c] = static_cast<long>(start + b * c_block + i);
                    }
                }
            }
        }
        EXPECT_EQ(arrival[0], static_cast<long>(start + 3)) << "after " << warmBlocks << " blocks";
        EXPECT_EQ(arrival[1], static_cast<long>(start + 21)) << "after " << warmBlocks << " blocks";
    }
}

TEST(ParallelPlainDelay, MaxSizeIsRoundedToWholeBlocks)
{
    Delay2 sut(18);
    EXPECT_EQ(sut.getMaxSize(), 20u);
    sut.setSize(0, 1000);
    EXPECT_EQ(sut.getSize(0), 19u);
}

TEST(ParallelPlainDelay, AuxiliaryHeadsReadEarlier)
{
    ParallelPlainDelay<c_block, 2, 2> sut(64);
    sut.setSize(0, 20);
    sut.setSize(1, 20);
    sut.setRelativeHead(1, 0, 0);
    sut.setRelativeHead(1, 1, 7);
    using Block = ParallelPlainDelay<c_block, 2, 2>::Block;
    Block src{};
    Block mainOut{};
    Block auxOut{};
    std::array<long, 2> auxArrival{-1, -1};
    for (size_t b = 0; b < 10; ++b)
    {
        src[0].fill(0.0f);
        src[1].fill(0.0f);
        if (b == 0)
        {
            src[0][0] = 1.0f;
            src[1][0] = 1.0f;
        }
        sut.processBlock(src, mainOut);
        sut.processHead(1, auxOut);
        for (size_t c = 0; c < 2; ++c)
        {
            for (size_t i = 0; i < c_block; ++i)
            {
                if (auxOut[c][i] == 1.0f)
                {
                    auxArrival[c] = static_cast<long>(b * c_block + i);
                }
            }
        }
    }
    EXPECT_EQ(auxArrival[0], 20);
    EXPECT_EQ(auxArrival[1], 13);
}

TEST(ParallelPlainDelay, FadeRampsFromOldToNewLength)
{
    // a constant per channel written once makes the two heads distinguishable: before the
    // fade the line reads value A (old position), afterwards value B (new position)
    constexpr size_t fade = 16;
    ParallelPlainDelay<c_block, 2> sut(64);
    sut.setSize(0, 40);
    sut.setSize(1, 40);
    using Block = ParallelPlainDelay<c_block, 2>::Block;
    Block src{};
    Block target{};
    // write a ramp so every position holds its own time stamp
    float stamp = 1.0f;
    auto feed = [&]()
    {
        for (size_t i = 0; i < c_block; ++i)
        {
            src[0][i] = stamp;
            src[1][i] = stamp;
            stamp += 1.0f;
        }
        sut.processBlock(src, target);
    };
    for (int b = 0; b < 20; ++b)
    {
        feed();
    }
    ASSERT_TRUE(sut.startFade({20, 40}, fade));
    EXPECT_TRUE(sut.isFading());
    std::vector<float> faded;
    std::vector<float> untouched;
    for (size_t b = 0; b < fade / c_block; ++b)
    {
        const float first = stamp;
        feed();
        for (size_t i = 0; i < c_block; ++i)
        {
            const float now = first + static_cast<float>(i);
            faded.push_back(target[0][i] - (now - 40.0f)); // 0 at the old head, 20 at the new head
            untouched.push_back(target[1][i] - (now - 40.0f));
        }
    }
    EXPECT_FALSE(sut.isFading());
    for (size_t n = 0; n < faded.size(); ++n)
    {
        // linear ramp: new-head weight (n + 1) / fade, the stamp difference between heads is 20
        EXPECT_NEAR(faded[n], 20.0f * static_cast<float>(n + 1) / static_cast<float>(fade), 1e-3f) << n;
        EXPECT_EQ(untouched[n], 0.0f) << n;
    }
    const float first = stamp;
    feed();
    EXPECT_EQ(target[0][0], first - 20.0f);
    EXPECT_EQ(sut.getSize(0), 20u);
}

TEST(ParallelPlainDelay, FadeRefusedWhileRunningAndWithoutChange)
{
    ParallelPlainDelay<c_block, 2> sut(64);
    sut.setSize(0, 30);
    sut.setSize(1, 30);
    EXPECT_FALSE(sut.startFade({30, 30}, 8));
    ASSERT_TRUE(sut.startFade({20, 30}, 8));
    EXPECT_FALSE(sut.startFade({10, 30}, 8));
    EXPECT_EQ(sut.getSize(0), 20u);
    ParallelPlainDelay<c_block, 2>::Block src{};
    ParallelPlainDelay<c_block, 2>::Block target{};
    sut.processBlock(src, target);
    EXPECT_TRUE(sut.isFading());
    sut.processBlock(src, target);
    EXPECT_FALSE(sut.isFading());
    EXPECT_TRUE(sut.startFade({10, 30}, 8));
}

TEST(ParallelPlainDelay, FadeHasNoStepOnASine)
{
    // a sine through a line whose length changes by 1 ms over 50 ms: the largest step stays
    // within a sine's own largest step plus the fade's share
    constexpr size_t block = 16;
    ParallelPlainDelay<block, 2, 2> sut(24000);
    sut.setSize(0, 9000);
    sut.setSize(1, 9000);
    sut.setRelativeHead(1, 0, 10);
    using Block = ParallelPlainDelay<block, 2, 2>::Block;
    Block src{};
    Block mainOut{};
    Block auxOut{};
    constexpr double fs = 48000.0;
    constexpr double frequency = 237.0;
    const double omega = 2.0 * 3.14159265358979323846 * frequency / fs;
    size_t n = 0;
    float previousMain = 0.0f;
    float previousAux = 0.0f;
    float worstMain = 0.0f;
    float worstAux = 0.0f;
    for (size_t b = 0; b < 2000; ++b)
    {
        if (b == 1000)
        {
            ASSERT_TRUE(sut.startFade({9048, 9000}, 2400));
        }
        for (size_t i = 0; i < block; ++i, ++n)
        {
            src[0][i] = static_cast<float>(std::sin(omega * static_cast<double>(n)));
            src[1][i] = src[0][i];
        }
        sut.processBlock(src, mainOut);
        sut.processHead(1, auxOut);
        for (size_t i = 0; i < block; ++i)
        {
            if (b > 700)
            {
                worstMain = std::max(worstMain, std::fabs(mainOut[0][i] - previousMain));
                worstAux = std::max(worstAux, std::fabs(auxOut[0][i] - previousAux));
            }
            previousMain = mainOut[0][i];
            previousAux = auxOut[0][i];
        }
    }
    const auto sineStep = static_cast<float>(omega);
    std::cout << "[ fade     ] largest step " << worstMain << " (main), " << worstAux << " (aux), sine step "
              << sineStep << "\n";
    EXPECT_LT(worstMain, 1.1f * sineStep);
    EXPECT_LT(worstAux, 1.1f * sineStep);
}

TEST(ParallelPlainDelay, ClearSilencesTheLines)
{
    Delay2 sut(16);
    sut.setSize(0, 5);
    sut.setSize(1, 5);
    Block2 src{};
    Block2 target{};
    src[0].fill(1.0f);
    src[1].fill(1.0f);
    sut.processBlock(src, target);
    sut.clear();
    src[0].fill(0.0f);
    src[1].fill(0.0f);
    for (int b = 0; b < 8; ++b)
    {
        sut.processBlock(src, target);
        for (size_t c = 0; c < 2; ++c)
        {
            for (const float v : target[c])
            {
                EXPECT_EQ(v, 0.0f);
            }
        }
    }
}

}

namespace jp::test
{

TEST(ParallelPlainDelay, OneChannelDelaysAndFades)
{
    // one channel (Eigenghost's pre-delay): an impulse arrives at the set delay, and a fade to
    // a new delay ends at it
    using Delay1 = ParallelPlainDelay<c_block, 1>;
    Delay1 delay(64);
    delay.setSize(0, 10);
    Delay1::Block src{};
    Delay1::Block target{};
    long arrival = -1;
    for (size_t b = 0; b < 8; ++b)
    {
        src[0].fill(0.0f);
        if (b == 0)
        {
            src[0][1] = 1.0f;
        }
        delay.processBlock(src, target);
        for (size_t i = 0; i < c_block; ++i)
        {
            if (target[0][i] == 1.0f)
            {
                arrival = static_cast<long>(b * c_block + i);
            }
        }
    }
    EXPECT_EQ(arrival, 11);
    EXPECT_TRUE(delay.startFade({20}, 2 * c_block));
    src[0].fill(0.0f);
    for (size_t b = 0; b < 4; ++b)
    {
        delay.processBlock(src, target);
    }
    EXPECT_FALSE(delay.isFading());
    EXPECT_EQ(delay.getSize(0), 20u);
}

}
