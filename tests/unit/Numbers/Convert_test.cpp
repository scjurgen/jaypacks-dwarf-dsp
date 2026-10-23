#include <gtest/gtest.h>

#include "dsp/Numbers/Convert.h"

namespace jp::test
{

TEST(Convert, NotesAreEqualTemperedAroundA440)
{
    EXPECT_FLOAT_EQ(noteToFrequency(69.0f), 440.0f);
    EXPECT_FLOAT_EQ(noteToFrequency(81.0f), 880.0f);
    EXPECT_FLOAT_EQ(noteToFrequency(57.0f), 220.0f);
    EXPECT_NEAR(noteToFrequency(60.0f), 261.6256f, 1e-3f);
    EXPECT_NEAR(noteToFrequency(69.5f), 440.0f * 1.0293022f, 1e-2f) << "a half note is 50 cent";
    EXPECT_NEAR(noteToFrequency(69.0f, 432.0f), 432.0f, 1e-3f);
}

TEST(Convert, FrequenciesAreTheInverseOfNotes)
{
    EXPECT_FLOAT_EQ(frequencyToNote(440.0f), 69.0f);
    EXPECT_FLOAT_EQ(frequencyToNote(880.0f), 81.0f);
    EXPECT_NEAR(frequencyToNote(261.6256f), 60.0f, 1e-4f);
    EXPECT_NEAR(frequencyToNote(432.0f, 432.0f), 69.0f, 1e-6f);
    for (float note = 20.0f; note < 120.0f; note += 3.7f)
    {
        EXPECT_NEAR(frequencyToNote(noteToFrequency(note)), note, 1e-4f);
    }
}

TEST(Convert, IntervalsAreTwoToTheTwelfth)
{
    EXPECT_FLOAT_EQ(noteIntervalToRatio(0.0f), 1.0f);
    EXPECT_FLOAT_EQ(noteIntervalToRatio(12.0f), 2.0f);
    EXPECT_FLOAT_EQ(noteIntervalToRatio(-12.0f), 0.5f);
    EXPECT_NEAR(noteIntervalToRatio(7.0f), 1.4983071f, 1e-6f);
    EXPECT_NEAR(noteIntervalToRatio(1.0f) * noteIntervalToRatio(-1.0f), 1.0f, 1e-6f);
}

TEST(Convert, DecibelsAreTwentyLogTen)
{
    EXPECT_FLOAT_EQ(dbToGain(0.0f), 1.0f);
    EXPECT_NEAR(dbToGain(20.0f), 10.0f, 1e-5f);
    EXPECT_NEAR(dbToGain(-6.0206f), 0.5f, 1e-5f);
    EXPECT_NEAR(dbToGain(-100.0f), 1e-5f, 1e-9f);
}

}
