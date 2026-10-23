#include <array>
#include <cmath>
#include <gtest/gtest.h>
#include <tuple>
#include <vector>

#include "dsp/Numbers/BulgeControl.h"

namespace jp::test
{

class BulgeNormalizedTable : public ::testing::TestWithParam<std::tuple<float, std::vector<float>>>
{
};

TEST_P(BulgeNormalizedTable, ProducesExpectedValues)
{
    const auto& [bulge, expected] = GetParam();
    std::array<float, 5> result{};
    Bulge::fillNormalizedTable(result.data(), result.size(), bulge);
    for (size_t i = 0; i < result.size(); ++i)
    {
        EXPECT_NEAR(result[i], expected[i], 0.0001f) << "bulge " << bulge << " index " << i;
    }
}

INSTANTIATE_TEST_SUITE_P(
    BulgeValues, BulgeNormalizedTable,
    ::testing::Values(std::make_tuple(-1.0f, std::vector<float>{0.0f, 0.6836f, 0.9375f, 0.9961f, 1.0f}),
                      std::make_tuple(-0.5f, std::vector<float>{0.0f, 0.4375f, 0.75f, 0.9375f, 1.0f}),
                      std::make_tuple(0.0f, std::vector<float>{0.0f, 0.25f, 0.5f, 0.75f, 1.0f}),
                      std::make_tuple(0.5f, std::vector<float>{0.0f, 0.0625f, 0.25f, 0.5625f, 1.0f}),
                      std::make_tuple(1.0f, std::vector<float>{0.0f, 0.0039f, 0.0625f, 0.3164f, 1.0f})));

class BulgeGetValue : public ::testing::TestWithParam<std::tuple<float, float, float>>
{
};

TEST_P(BulgeGetValue, ProducesExpectedValue)
{
    const auto& [x, bulge, expected] = GetParam();
    EXPECT_NEAR(Bulge::getValue(x, bulge), expected, 0.0001f);
}

INSTANTIATE_TEST_SUITE_P(InputOutputPairs, BulgeGetValue,
                         ::testing::Values(std::make_tuple(0.0f, -0.5f, 0.0f), std::make_tuple(0.25f, -0.5f, 0.4375f),
                                           std::make_tuple(0.5f, -0.5f, 0.75f), std::make_tuple(0.75f, -0.5f, 0.9375f),
                                           std::make_tuple(1.0f, -0.5f, 1.0f), std::make_tuple(0.0f, 0.0f, 0.0f),
                                           std::make_tuple(0.25f, 0.0f, 0.25f), std::make_tuple(0.5f, 0.0f, 0.5f),
                                           std::make_tuple(0.75f, 0.0f, 0.75f), std::make_tuple(1.0f, 0.0f, 1.0f),
                                           std::make_tuple(0.0f, 0.5f, 0.0f), std::make_tuple(0.25f, 0.5f, 0.0625f),
                                           std::make_tuple(0.5f, 0.5f, 0.25f), std::make_tuple(0.75f, 0.5f, 0.5625f),
                                           std::make_tuple(1.0f, 0.5f, 1.0f)));

TEST(Bulge, TableEdgeCases)
{
    std::array<float, 1> one{0.5f};
    Bulge::fillNormalizedTable(one.data(), one.size(), 0.3f);
    EXPECT_EQ(one[0], 0.0f);
    std::array<float, 1> untouched{0.5f};
    Bulge::fillNormalizedTable(untouched.data(), 0, 0.3f);
    EXPECT_EQ(untouched[0], 0.5f);
}

TEST(Bulge, MonotonicForAnyBulge)
{
    for (const float bulge : {-1.0f, -0.4f, 0.0f, 0.6f, 1.0f})
    {
        std::array<float, 16> table{};
        Bulge::fillNormalizedTable(table.data(), table.size(), bulge);
        for (size_t i = 1; i < table.size(); ++i)
        {
            EXPECT_GT(table[i], table[i - 1]) << "bulge " << bulge << " index " << i;
        }
        EXPECT_EQ(table.front(), 0.0f);
        EXPECT_FLOAT_EQ(table.back(), 1.0f);
    }
}

}
