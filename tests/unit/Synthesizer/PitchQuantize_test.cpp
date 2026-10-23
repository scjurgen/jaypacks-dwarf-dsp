#include <gtest/gtest.h>

#include "dsp/Synthesizer/PitchQuantize.h"

namespace jp::test
{

TEST(PitchQuantize, ZeroLevelLeavesTheInputUnchanged)
{
    EXPECT_FLOAT_EQ(pitchQuantize(0.0f, 0.0f), 0.0f);
    EXPECT_FLOAT_EQ(pitchQuantize(0.45f, 0.0f), 0.45f);
    EXPECT_FLOAT_EQ(pitchQuantize(0.5f, 0.0f), 0.5f);
    EXPECT_FLOAT_EQ(pitchQuantize(-0.5f, 0.0f), -0.5f);
}

TEST(PitchQuantize, FullLevelSnapsToTheNearestInteger)
{
    EXPECT_FLOAT_EQ(pitchQuantize(0.0f, 1.0f), 0.0f);
    EXPECT_FLOAT_EQ(pitchQuantize(0.45f, 1.0f), 0.0f);
    EXPECT_FLOAT_EQ(pitchQuantize(0.5f, 1.0f), 1.0f);
    EXPECT_FLOAT_EQ(pitchQuantize(-0.5f, 1.0f), -1.0f);
    EXPECT_FLOAT_EQ(pitchQuantize(0.75f, 1.0f), 1.0f);
    EXPECT_FLOAT_EQ(pitchQuantize(-0.75f, 1.0f), -1.0f);
    EXPECT_FLOAT_EQ(pitchQuantize(8.75f, 1.0f), 9.0f);
    EXPECT_FLOAT_EQ(pitchQuantize(-11.75f, 1.0f), -12.0f);
}

TEST(PitchQuantize, PartialLevelRemovesThatFractionOfTheDistance)
{
    EXPECT_FLOAT_EQ(pitchQuantize(0.45f, 0.5f), 0.225f);
    EXPECT_FLOAT_EQ(pitchQuantize(8.75f, 0.5f), 8.875f);
    EXPECT_FLOAT_EQ(pitchQuantize(8.75f, 0.25f), 8.8125f);
}

TEST(PitchQuantize, IntegersAreFixedPointsAtAnyLevel)
{
    for (const float level : {0.0f, 0.3f, 1.0f})
    {
        EXPECT_FLOAT_EQ(pitchQuantize(7.0f, level), 7.0f);
        EXPECT_FLOAT_EQ(pitchQuantize(-12.0f, level), -12.0f);
    }
}

TEST(PitchQuantize, WorksForDoubleAsWellAsFloat)
{
    EXPECT_DOUBLE_EQ(pitchQuantize(8.75, 1.0), 9.0);
    EXPECT_DOUBLE_EQ(pitchQuantize(8.75, 0.0), 8.75);
}

}
