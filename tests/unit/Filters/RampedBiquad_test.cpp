#include <gtest/gtest.h>

#include "dsp/Filters/RampedBiquad.h"

namespace jp::test
{

namespace
{
const BiquadSection c_a{0.2f, 0.1f, -0.05f, -0.9f, 0.3f};
const BiquadSection c_b{0.6f, -0.2f, 0.1f, 0.4f, -0.2f};
}

TEST(RampedBiquad, ReachesTheTargetExactlyAfterTheRamp)
{
    RampedBiquad filter;
    filter.setTarget(c_a);
    EXPECT_EQ(filter.getCurrent().b0, c_a.b0); // ramp 0: at once
    filter.setRampSamples(16);
    filter.setTarget(c_b);
    for (int i = 0; i < 15; ++i)
    {
        EXPECT_TRUE(filter.isRamping());
        static_cast<void>(filter.process(0.0f));
    }
    static_cast<void>(filter.process(0.0f));
    const BiquadSection& c = filter.getCurrent();
    EXPECT_FALSE(filter.isRamping());
    EXPECT_EQ(c.b0, c_b.b0);
    EXPECT_EQ(c.b1, c_b.b1);
    EXPECT_EQ(c.b2, c_b.b2);
    EXPECT_EQ(c.a1, c_b.a1);
    EXPECT_EQ(c.a2, c_b.a2);
}

TEST(RampedBiquad, MovesLinearlyAndRetargetsFromWhereItIs)
{
    RampedBiquad filter;
    filter.setTarget(c_a);
    filter.setRampSamples(10);
    filter.setTarget(c_b);
    for (int i = 0; i < 5; ++i)
    {
        static_cast<void>(filter.process(0.0f));
    }
    EXPECT_NEAR(filter.getCurrent().a1, 0.5f * (c_a.a1 + c_b.a1), 1e-6f);
    EXPECT_NEAR(filter.getCurrent().b0, 0.5f * (c_a.b0 + c_b.b0), 1e-6f);
    const float from = filter.getCurrent().a1;
    filter.setTarget(c_a);
    static_cast<void>(filter.process(0.0f));
    EXPECT_NEAR(filter.getCurrent().a1, from + (c_a.a1 - from) / 10.0f, 1e-6f);
    filter.snapToTarget();
    EXPECT_EQ(filter.getCurrent().a1, c_a.a1);
}

}
