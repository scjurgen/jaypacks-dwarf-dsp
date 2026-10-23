#include <cmath>
#include <gtest/gtest.h>

#include "dsp/Numbers/PanLaw.h"

namespace jp::test
{

TEST(PanLaw, EndsAreDryOnlyAndWetOnly)
{
    const auto dryOnly = constantPowerMix(-1.0f);
    EXPECT_NEAR(dryOnly.dry, 1.0f, 1e-6f);
    EXPECT_NEAR(dryOnly.wet, 0.0f, 1e-6f);
    const auto wetOnly = constantPowerMix(1.0f);
    EXPECT_NEAR(wetOnly.dry, 0.0f, 1e-6f);
    EXPECT_NEAR(wetOnly.wet, 1.0f, 1e-6f);
}

TEST(PanLaw, CenterIsMinusThreeDbEach)
{
    const auto center = constantPowerMix(0.0f);
    EXPECT_NEAR(center.dry, std::sqrt(0.5f), 1e-6f);
    EXPECT_NEAR(center.wet, std::sqrt(0.5f), 1e-6f);
}

TEST(PanLaw, PowerIsConstantAndGainsAreMonotonic)
{
    float previousWet = -1.0f;
    float previousDry = 2.0f;
    for (float mix = -1.0f; mix <= 1.0f; mix += 0.01f)
    {
        const auto g = constantPowerMix(mix);
        EXPECT_NEAR(g.dry * g.dry + g.wet * g.wet, 1.0f, 1e-5f) << mix;
        EXPECT_GT(g.wet, previousWet) << mix;
        EXPECT_LT(g.dry, previousDry) << mix;
        previousWet = g.wet;
        previousDry = g.dry;
    }
}

TEST(PanLaw, ClampsOutOfRangeValues)
{
    EXPECT_NEAR(constantPowerMix(-3.0f).dry, 1.0f, 1e-6f);
    EXPECT_NEAR(constantPowerMix(7.0f).wet, 1.0f, 1e-6f);
}

TEST(PanLaw, MatchesTheFormerSumAndDifferenceForm)
{
    // the old getPanFactorNormalized(): dry = (cos a - sin a) / sqrt(2), wet = (cos a + sin a) / sqrt(2), a = m pi / 4
    for (float mix = -1.0f; mix <= 1.0f; mix += 0.125f)
    {
        const float a = mix * 0.78539816f;
        const auto g = constantPowerMix(mix);
        EXPECT_NEAR(g.dry, (std::cos(a) - std::sin(a)) / std::sqrt(2.0f), 1e-6f) << mix;
        EXPECT_NEAR(g.wet, (std::cos(a) + std::sin(a)) / std::sqrt(2.0f), 1e-6f) << mix;
    }
}

}
