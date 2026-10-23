#include <algorithm>
#include <cmath>
#include <cstdint>
#include <gtest/gtest.h>
#include <vector>

#include "dsp/Generators/CutoffTrajectory.h"

namespace jp::test
{

namespace
{
std::vector<float> run(CutoffTrajectory& trajectory, const size_t samples)
{
    std::vector<float> values;
    values.reserve(samples);
    for (size_t i = 0; i < samples; ++i)
    {
        values.push_back(trajectory.next());
    }
    return values;
}
}

TEST(CutoffTrajectory, HoldsItsValueWithoutATarget)
{
    CutoffTrajectory trajectory;
    trajectory.setValue(60.0f);
    for (const float v : run(trajectory, 100))
    {
        EXPECT_EQ(v, 60.0f);
    }
    EXPECT_FALSE(trajectory.isGliding());
}

TEST(CutoffTrajectory, ZeroDurationJumpsAtOnce)
{
    CutoffTrajectory trajectory;
    trajectory.setValue(60.0f);
    trajectory.retarget(72.0f, 0);
    EXPECT_EQ(trajectory.value(), 72.0f);
    EXPECT_FALSE(trajectory.isGliding());
    trajectory.retarget(48.0f, -5);
    EXPECT_EQ(trajectory.value(), 48.0f);
}

TEST(CutoffTrajectory, OneSampleGlideArrivesOnTheFirstSample)
{
    CutoffTrajectory trajectory;
    trajectory.setValue(60.0f);
    trajectory.retarget(72.0f, 1);
    EXPECT_EQ(trajectory.value(), 60.0f);
    EXPECT_EQ(trajectory.next(), 72.0f);
    EXPECT_FALSE(trajectory.isGliding());
}

TEST(CutoffTrajectory, GlideIsLinearAndLandsExactlyOnTheTargetAtTheLastSample)
{
    for (const int64_t n : {2, 7, 480, 4800, 48000})
    {
        CutoffTrajectory trajectory;
        trajectory.setValue(33.3f);
        trajectory.retarget(97.7f, n);
        const std::vector<float> values = run(trajectory, static_cast<size_t>(n) + 10);
        for (int64_t i = 0; i < n; ++i)
        {
            const double expected = 33.3 + (97.7 - 33.3) * static_cast<double>(i + 1) / static_cast<double>(n);
            EXPECT_NEAR(values[static_cast<size_t>(i)], expected, 2e-3) << "n " << n << " i " << i;
        }
        EXPECT_EQ(values[static_cast<size_t>(n) - 1], 97.7f) << "n " << n;
        EXPECT_EQ(values.back(), 97.7f);
        EXPECT_FALSE(trajectory.isGliding());
    }
}

TEST(CutoffTrajectory, DownwardGlideIsMonotonic)
{
    CutoffTrajectory trajectory;
    trajectory.setValue(80.0f);
    trajectory.retarget(40.0f, 1000);
    float previous = trajectory.value();
    for (const float v : run(trajectory, 1000))
    {
        EXPECT_LE(v, previous);
        previous = v;
    }
    EXPECT_EQ(previous, 40.0f);
}

TEST(CutoffTrajectory, RetargetMidGlideContinuesFromTheCurrentValueWithoutAJump)
{
    CutoffTrajectory trajectory;
    trajectory.setValue(60.0f);
    trajectory.retarget(72.0f, 1000);
    const std::vector<float> first = run(trajectory, 400);
    const float at = first.back();
    trajectory.retarget(55.0f, 600);
    EXPECT_EQ(trajectory.value(), at);
    const float step = (55.0f - at) / 600.0f;
    EXPECT_NEAR(trajectory.next(), at + step, 1e-4f);
    const std::vector<float> second = run(trajectory, 599);
    EXPECT_EQ(second.back(), 55.0f);
}

TEST(CutoffTrajectory, RetargetToTheSamePlaceStaysPut)
{
    CutoffTrajectory trajectory;
    trajectory.setValue(64.0f);
    trajectory.retarget(64.0f, 100);
    for (const float v : run(trajectory, 100))
    {
        EXPECT_EQ(v, 64.0f);
    }
}

TEST(CutoffTrajectory, SplittingTheRunIntoBlocksChangesNothing)
{
    CutoffTrajectory whole;
    whole.setValue(50.0f);
    whole.retarget(90.0f, 5000);
    const std::vector<float> reference = run(whole, 5000);

    for (const size_t block : {1u, 7u, 128u, 4096u})
    {
        CutoffTrajectory trajectory;
        trajectory.setValue(50.0f);
        trajectory.retarget(90.0f, 5000);
        std::vector<float> values;
        while (values.size() < 5000)
        {
            const size_t count = std::min<size_t>(block, 5000 - values.size());
            const std::vector<float> part = run(trajectory, count);
            values.insert(values.end(), part.begin(), part.end());
        }
        EXPECT_EQ(values, reference) << "block " << block;
    }
}

TEST(CutoffTrajectory, SetValueEndsAGlide)
{
    CutoffTrajectory trajectory;
    trajectory.setValue(60.0f);
    trajectory.retarget(72.0f, 1000);
    run(trajectory, 10);
    trajectory.setValue(30.0f);
    EXPECT_FALSE(trajectory.isGliding());
    EXPECT_EQ(trajectory.next(), 30.0f);
    EXPECT_EQ(trajectory.target(), 30.0f);
}

}
