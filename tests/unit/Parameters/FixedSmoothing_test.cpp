#include <gtest/gtest.h>
#include <tuple>

#include "dsp/Parameters/FixedSmoothing.h"

namespace jp::test
{

TEST(FixedSmoothing, ForceTargetJumpsAtOnceAndCancelsARamp)
{
    FixedSmoothing<128> sut;
    sut.forceTarget(0.0f);
    sut.setTarget(1.0f);
    EXPECT_GT(sut.getSmoothed(), 0.0f);
    EXPECT_LE(sut.getSmoothed(), 1.0f);
    sut.forceTarget(2.0f);
    EXPECT_FALSE(sut.isActive());
    EXPECT_FLOAT_EQ(sut.getSmoothed(), 2.0f);
}

TEST(FixedSmoothing, RampReachesTheTargetInExactlyStepsCalls)
{
    FixedSmoothing<8> sut;
    sut.forceTarget(0.0f);
    sut.setTarget(1.0f);
    for (int i = 1; i < 8; ++i)
    {
        EXPECT_FLOAT_EQ(sut.getSmoothed(), static_cast<float>(i) / 8.0f);
    }
    EXPECT_EQ(sut.getSmoothed(), 1.0f) << "set exactly on the last step, not accumulated";
    EXPECT_FALSE(sut.isActive());
}

TEST(FixedSmoothing, RampHandlesANegativeDelta)
{
    FixedSmoothing<8> sut;
    sut.forceTarget(1.0f);
    sut.setTarget(-1.0f);
    for (int i = 1; i < 8; ++i)
    {
        EXPECT_FLOAT_EQ(sut.getSmoothed(), 1.0f - static_cast<float>(i) / 4.0f);
    }
    EXPECT_EQ(sut.getSmoothed(), -1.0f);
}

TEST(FixedSmoothing, ANewTargetRestartsTheRampFromTheCurrentValue)
{
    FixedSmoothing<4> sut;
    sut.forceTarget(0.0f);
    sut.setTarget(1.0f);
    std::ignore = sut.getSmoothed();
    const float middle = sut.getSmoothed();
    EXPECT_FLOAT_EQ(middle, 0.5f);
    sut.setTarget(0.0f);
    EXPECT_FLOAT_EQ(sut.getSmoothed(), 0.375f);
    std::ignore = sut.getSmoothed();
    std::ignore = sut.getSmoothed();
    EXPECT_EQ(sut.getSmoothed(), 0.0f);
}

TEST(FixedSmoothing, IsActiveWhileARampRuns)
{
    FixedSmoothing<8> sut;
    sut.forceTarget(0.0f);
    EXPECT_FALSE(sut.isActive());
    sut.setTarget(1.0f);
    EXPECT_TRUE(sut.isActive());
    for (int i = 0; i < 8; ++i)
    {
        std::ignore = sut.getSmoothed();
    }
    EXPECT_FALSE(sut.isActive());
}

TEST(FixedSmoothing, OneStepIsAJumpOnTheNextCall)
{
    FixedSmoothing<1> sut;
    sut.setTarget(3.0f);
    EXPECT_TRUE(sut.isActive());
    EXPECT_EQ(sut.getSmoothed(), 3.0f);
    EXPECT_FALSE(sut.isActive());
}

}
