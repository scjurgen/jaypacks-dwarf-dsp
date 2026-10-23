#include <cmath>
#include <gtest/gtest.h>

#include "dsp/Filters/PoleMixingCorrections.h"

namespace jp::test
{

TEST(PoleMixingCorrections, WarpValuesOfTheFit)
{
    EXPECT_NEAR(correctedRawCutoffForTargetFrequency(20.0f), 20.2192f, 1e-3f);
    EXPECT_NEAR(correctedRawCutoffForTargetFrequency(100.0f), 101.7925f, 1e-2f);
    EXPECT_NEAR(correctedRawCutoffForTargetFrequency(1000.0f), 1020.4797f, 1e-1f);
    EXPECT_NEAR(correctedRawCutoffForTargetFrequency(8564.1f), 11020.09f, 1.0f);
}

TEST(PoleMixingCorrections, WarpIsClampedToTheFitDomain)
{
    EXPECT_FLOAT_EQ(correctedRawCutoffForTargetFrequency(1.0f), correctedRawCutoffForTargetFrequency(20.0f));
    EXPECT_FLOAT_EQ(correctedRawCutoffForTargetFrequency(20000.0f), correctedRawCutoffForTargetFrequency(8564.1f));
}

TEST(PoleMixingCorrections, WarpRisesWithTheTarget)
{
    float previous = 0.0f;
    for (float hz = 20.0f; hz < 8564.0f; hz *= 1.05f)
    {
        const float raw = correctedRawCutoffForTargetFrequency(hz);
        EXPECT_GT(raw, previous) << hz;
        previous = raw;
    }
}

TEST(PoleMixingCorrections, CriticalResonanceValuesOfTheFit)
{
    EXPECT_NEAR(criticalResonanceForRawCutoff(20.0f), 3.899318f, 1e-4f);
    EXPECT_NEAR(criticalResonanceForRawCutoff(1000.0f), 4.549509f, 1e-4f);
    EXPECT_NEAR(criticalResonanceForRawCutoff(19706.4f), 115.162f, 1e-2f);
}

TEST(PoleMixingCorrections, CriticalResonanceIsClampedToTheFitDomain)
{
    EXPECT_FLOAT_EQ(criticalResonanceForRawCutoff(10.0f), criticalResonanceForRawCutoff(20.0f));
    EXPECT_FLOAT_EQ(criticalResonanceForRawCutoff(30000.0f), criticalResonanceForRawCutoff(19706.4f));
}

}
