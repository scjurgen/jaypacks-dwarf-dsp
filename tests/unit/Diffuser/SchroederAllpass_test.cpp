#include <array>
#include <cmath>
#include <gtest/gtest.h>
#include <iostream>
#include <vector>

#include "tests/unit/TestHelpers.h"
#include "dsp/Diffuser/SchroederAllpass.h"

namespace jp::test
{

class SchroederAllPassSoftTransitionTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        m_sut.setGain(0.5f);
        m_sut.setSize(23);
        expectZeroOutputs(10000);
    }

    void expectZeroOutputs(const size_t count)
    {
        for (size_t i = 0; i < count; ++i)
        {
            ASSERT_EQ(m_sut.step(0.0f), 0.0f) << "at " << i;
        }
    }

    SchroederAllPassSoftTransition<1000, 16> m_sut{48000.0f};
};

TEST_F(SchroederAllPassSoftTransitionTest, DecayHalf)
{
    const std::vector<float> expected{-0.5f, 0.75f, 0.375f, 0.1875f, 0.09375f};
    EXPECT_FLOAT_EQ(m_sut.step(1.0f), expected[0]);
    for (size_t i = 1; i < expected.size(); ++i)
    {
        expectZeroOutputs(22);
        EXPECT_FLOAT_EQ(m_sut.step(0.0f), expected[i]);
    }
}

TEST_F(SchroederAllPassSoftTransitionTest, DecaySlow)
{
    const std::vector<float> expected{-0.75f,    0.4375f,    0.328125f,  0.246094f,  0.18457f,  0.138428f,
                                      0.103821f, 0.0778656f, 0.0583992f, 0.0437994f, 0.0328496f};
    m_sut.setGain(0.75f);
    EXPECT_FLOAT_EQ(m_sut.step(1.0f), expected[0]);
    for (size_t i = 1; i < expected.size(); ++i)
    {
        expectZeroOutputs(22);
        EXPECT_NEAR(m_sut.step(0.0f), expected[i], 1e-6f);
    }
}

TEST_F(SchroederAllPassSoftTransitionTest, NoDecayIsAPureDelay)
{
    m_sut.setGain(0.0f);
    m_sut.setSize(1);
    expectZeroOutputs(256); // the fade from 23 takes 2 x 23 samples, at least 256
    EXPECT_EQ(m_sut.step(1.0f), 0.0f);
    EXPECT_EQ(m_sut.step(0.0f), 1.0f);
    expectZeroOutputs(10);
}

TEST(SchroederAllPassSoftTransition, FastSizeIsExactAtOnce)
{
    SchroederAllPassSoftTransition<1000, 16> sut{48000.0f};
    sut.setGain(0.0f);
    sut.setSize(37, true);
    EXPECT_EQ(sut.size(), 37u);
    EXPECT_EQ(sut.step(1.0f), 0.0f);
    for (int i = 1; i < 37; ++i)
    {
        ASSERT_EQ(sut.step(0.0f), 0.0f) << i;
    }
    EXPECT_EQ(sut.step(0.0f), 1.0f);
}

TEST(SchroederAllPassSoftTransition, BufferScalesWithSampleRate)
{
    SchroederAllPassSoftTransition<1000, 16> sut{96000.0f};
    sut.setSize(1900, true);
    EXPECT_EQ(sut.size(), 1900u);
    sut.setSize(5000, true);
    EXPECT_EQ(sut.size(), 1999u);
}

TEST(SchroederAllPassSoftTransition, MagnitudeIsFlat)
{
    for (const float gain : {0.3f, 0.6f, 0.75f})
    {
        for (const double frequency : {100.0, 1234.0, 9000.0})
        {
            SchroederAllPassSoftTransition<1000, 16> sut{48000.0f};
            sut.setGain(gain);
            sut.setSize(113, true);
            const double measured =
                measureSineGain([&sut](const float x) { return sut.step(x); }, frequency, 48000.0, 0.1, 1.0, 1.0);
            EXPECT_NEAR(toDb(measured), 0.0, 0.01) << "gain " << gain << " at " << frequency << " Hz";
        }
    }
}

TEST(SchroederAllPassSoftTransition, SizeChangeHasNoStep)
{
    SchroederAllPassSoftTransition<4800, 16> sut{48000.0f};
    sut.setGain(0.5f);
    sut.setSize(1201, true);
    const double omega = 2.0 * c_pi * 237.0 / 48000.0;
    float previous = 0.0f;
    float worstSteady = 0.0f;
    float worstFade = 0.0f;
    for (size_t n = 0; n < 48000; ++n)
    {
        if (n == 24000)
        {
            sut.setSize(1601);
        }
        const float y = sut.step(static_cast<float>(std::sin(omega * static_cast<double>(n))));
        const float stepSize = std::fabs(y - previous);
        if (n > 12000 && n < 24000)
        {
            worstSteady = std::max(worstSteady, stepSize);
        }
        if (n >= 24000)
        {
            worstFade = std::max(worstFade, stepSize);
        }
        previous = y;
    }
    std::cout << "[ allpass  ] largest step steady " << worstSteady << ", during and after the fade " << worstFade
              << "\n";
    EXPECT_LT(worstFade, 1.1f * worstSteady);
}

TEST(SchroederAllPassSoftTransition, BlockEqualsStepForEveryPath)
{
    // the block loop (delay of at least a block, no wrap, no fade) and the per-sample path
    // (short delays, wraps, fades) give the same samples as step(); the buffer of 1000
    // samples wraps every 62.5 blocks
    for (const size_t size : {5u, 15u, 16u, 17u, 113u, 997u})
    {
        SchroederAllPassSoftTransition<1000, 16> block{48000.0f};
        SchroederAllPassSoftTransition<1000, 16> sample{48000.0f};
        for (auto* sut : {&block, &sample})
        {
            sut->setGain(0.6f);
            sut->setSize(size, true);
        }
        float x = 0.3f;
        for (size_t b = 0; b < 400; ++b)
        {
            if (b == 200)
            {
                block.setSize(size + 40);
                sample.setSize(size + 40);
            }
            std::array<float, 16> in{};
            for (auto& v : in)
            {
                x = 3.9f * x * (1.0f - x);
                v = x - 0.5f;
            }
            std::array<float, 16> out{};
            block.processBlock(in.data(), out.data());
            for (size_t i = 0; i < 16; ++i)
            {
                ASSERT_EQ(out[i], sample.step(in[i])) << "size " << size << " block " << b << " sample " << i;
            }
        }
    }
}
}
