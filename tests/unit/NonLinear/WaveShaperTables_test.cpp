#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <set>
#include <string>
#include <vector>

#include "dsp/NonLinear/WaveShaperTables.h"

namespace jp::test
{

namespace
{
const WaveShaperTableStore& store()
{
    static const WaveShaperTableStore sharedStore;
    return sharedStore;
}

size_t indexOf(const std::string& name)
{
    const auto& presets = distortionPresets();
    for (size_t i = 0; i < presets.size(); ++i)
    {
        if (name == presets[i].name)
        {
            return i;
        }
    }
    return presets.size();
}
}

TEST(InterpolateWithEquidistantControlPoints, ControlPointsLieAroundZeroAtTheirDistance)
{
    InterpolateWithEquidistantControlPoints odd;
    odd.setInterpolationFunction(linearPt2, 0);
    odd.setControlVector(false, {1.0f, 0.5f, 0.0f, 0.25f, 3.0f}, 0.5f);
    for (const auto& [x, y] : {std::pair{-1.0f, 1.0f}, std::pair{-0.5f, 0.5f}, std::pair{0.0f, 0.0f},
                               std::pair{0.5f, 0.25f}, std::pair{1.0f, 3.0f}})
    {
        EXPECT_NEAR(odd.getValue(x), y, 1e-5f) << x;
    }
    EXPECT_NEAR(odd.getValue(-0.75f), 0.75f, 1e-5f) << "linear between the points";
    InterpolateWithEquidistantControlPoints even;
    even.setInterpolationFunction(linearPt2, 0);
    even.setControlVector(false, {-1.0f, 1.0f}, 2.0f);
    EXPECT_NEAR(even.getValue(-1.0f), -1.0f, 1e-5f);
    EXPECT_NEAR(even.getValue(0.0f), 0.0f, 1e-5f);
    EXPECT_NEAR(even.getValue(1.0f), 1.0f, 1e-5f);
}

TEST(InterpolateWithEquidistantControlPoints, BeyondThePointsTheEndValuesStay)
{
    InterpolateWithEquidistantControlPoints curve;
    curve.setInterpolationFunction(hermite43x, 1);
    curve.setControlVector(false, {-0.8f, 0.0f, 0.7f}, 0.25f);
    for (const float x : {-4.0f, -2.0f, -1.0f})
    {
        EXPECT_NEAR(curve.getValue(x), -0.8f, 1e-5f) << x;
    }
    for (const float x : {1.0f, 2.0f, 4.0f})
    {
        EXPECT_NEAR(curve.getValue(x), 0.7f, 1e-5f) << x;
    }
}

TEST(WaveShaperInterpolated, AnIdentityTableReproducesTheInputAndClamps)
{
    WaveShaperInterpolated<513, true> shaper;
    shaper.assignTable(-2.0f, 2.0f, [](const float x) { return x; });
    for (float x = -1.95f; x < 1.95f; x += 0.0371f)
    {
        EXPECT_NEAR(shaper.step(x), x, 1e-5f) << x;
    }
    EXPECT_NEAR(shaper.step(10.0f), 2.0f, 1e-5f);
    EXPECT_NEAR(shaper.step(-10.0f), -2.0f, 1e-5f);
    std::vector<float> block{-3.0f, 0.5f, 3.0f};
    shaper.processBlock(block.data(), block.size());
    EXPECT_NEAR(block[0], -2.0f, 1e-5f);
    EXPECT_NEAR(block[1], 0.5f, 1e-5f);
    EXPECT_NEAR(block[2], 2.0f, 1e-5f);
}

TEST(WaveShaperTables, ThereAreThirtyOnePresetsWithUniqueNames)
{
    const auto& presets = distortionPresets();
    ASSERT_EQ(presets.size(), 31u);
    EXPECT_EQ(store().presetCount(), 31u);
    std::set<std::string> names;
    for (const auto& preset : presets)
    {
        names.insert(preset.name);
        EXPECT_GE(preset.controlPoints.size(), 2u) << preset.name;
        EXPECT_GT(preset.delta, 0.0f) << preset.name;
    }
    EXPECT_EQ(names.size(), 31u);
    EXPECT_STREQ(presets.front().name, "classic silicon");
    EXPECT_STREQ(presets.back().name, "broken amp 5");
}

TEST(WaveShaperTables, EveryTableIsFiniteBoundedAndFlatBeyondItsEnds)
{
    const auto& presets = distortionPresets();
    for (size_t i = 0; i < presets.size(); ++i)
    {
        const auto& points = presets[i].controlPoints;
        float largest = 0.0f;
        for (const float p : points)
        {
            largest = std::max(largest, std::abs(p));
        }
        for (float x = -4.0f; x <= 4.0f; x += 0.0173f)
        {
            const float y = store().getValue(i, x);
            ASSERT_TRUE(std::isfinite(y)) << presets[i].name;
            ASSERT_LE(std::abs(y), 1.35f * largest + 1e-3f) << presets[i].name << " at " << x;
        }
        EXPECT_NEAR(store().getValue(i, 4.0f), points.back(), 1e-3f) << presets[i].name;
        EXPECT_NEAR(store().getValue(i, -4.0f), points.front(), 1e-3f) << presets[i].name;
        EXPECT_EQ(store().getValue(i, 40.0f), store().getValue(i, 4.0f)) << "clamped to the table";
        EXPECT_EQ(store().getValue(i, -40.0f), store().getValue(i, -4.0f)) << presets[i].name;
    }
}

TEST(WaveShaperTables, ClassicSiliconIsAHardClipAtOne)
{
    const size_t i = indexOf("classic silicon");
    EXPECT_NEAR(store().getValue(i, 0.5f), 0.5f, 1e-3f);
    EXPECT_NEAR(store().getValue(i, -0.25f), -0.25f, 1e-3f);
    EXPECT_NEAR(store().getValue(i, 2.0f), 1.0f, 1e-3f);
    EXPECT_NEAR(store().getValue(i, -3.0f), -1.0f, 1e-3f);
}

TEST(WaveShaperTables, ClassicSoftTanhFollowsTanhOfTwoX)
{
    const size_t i = indexOf("classic soft tanh");
    double worst = 0.0;
    for (float x = -1.4f; x <= 1.4f; x += 0.01f)
    {
        worst = std::max(worst, static_cast<double>(std::abs(store().getValue(i, x) - std::tanh(2.0f * x))));
    }
    EXPECT_LT(worst, 3e-3);
}

TEST(WaveShaperTables, TheClassicPresetsAreOddAndRectifyIsEven)
{
    for (const char* name : {"classic silicon", "classic germanium", "classic soft", "classic soft tanh",
                             "classic medium", "classic hard", "classic fuzz"})
    {
        const size_t i = indexOf(name);
        for (float x = 0.013f; x < 3.0f; x += 0.047f)
        {
            ASSERT_NEAR(store().getValue(i, -x), -store().getValue(i, x), 3e-3f) << name << " at " << x;
        }
    }
    const size_t rectify = indexOf("classic rectify");
    for (float x = 0.013f; x < 3.0f; x += 0.047f)
    {
        ASSERT_NEAR(store().getValue(rectify, -x), store().getValue(rectify, x), 3e-3f) << x;
    }
    EXPECT_NEAR(store().getValue(rectify, 0.0f), 0.0f, 5e-3f)
        << "the kink is rounded off by the B-spline, by a third of a table step";
    EXPECT_NEAR(store().getValue(rectify, 1.0f), 1.0f, 2e-3f);
}

TEST(WaveShaperTables, TheClassicCurvesAreMonotonic)
{
    for (const char* name : {"classic silicon", "classic germanium", "classic soft", "classic soft tanh",
                             "classic medium", "classic hard", "classic fuzz"})
    {
        const size_t i = indexOf(name);
        float previous = -2.0f;
        for (float x = -4.0f; x <= 4.0f; x += 0.01f)
        {
            const float y = store().getValue(i, x);
            ASSERT_GE(y, previous - 1e-4f) << name << " at " << x;
            previous = y;
        }
    }
}

TEST(WaveShaperTables, BitCrunchPresetsAreStaircasesOnTheirLevels)
{
    const size_t i = indexOf("bit crunch 1"); // levels -1, -0.5, 0, 0.5, 1
    int onALevel = 0;
    int total = 0;
    for (float x = -1.2f; x <= 1.2f; x += 0.005f)
    {
        const float y = store().getValue(i, x);
        float nearest = 1e9f;
        for (const float level : {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f})
        {
            nearest = std::min(nearest, std::abs(y - level));
        }
        onALevel += nearest < 0.02f ? 1 : 0;
        ++total;
    }
    EXPECT_GT(onALevel, total * 9 / 10) << "all but the thin risers";
    EXPECT_NEAR(store().getValue(i, 0.05f), 0.0f, 0.02f);
    EXPECT_NEAR(store().getValue(i, 0.4f), 0.5f, 0.02f)
        << "the flats are centred on the points: 0.5 holds 0.25 to 0.75";
}

TEST(WaveShaperTables, ABlockEqualsTheSamplesAndAnIndexBeyondTheEndReadsTheLastPreset)
{
    std::vector<float> block{-2.0f, -0.3f, 0.0f, 0.4f, 1.7f};
    std::vector<float> expected(block.size());
    for (size_t k = 0; k < block.size(); ++k)
    {
        expected[k] = store().getValue(5, block[k]);
    }
    store().processBlock(5, block.data(), block.size());
    EXPECT_EQ(block, expected);
    EXPECT_EQ(store().getValue(99, 0.7f), store().getValue(30, 0.7f));
    std::vector<float> beyond{0.7f};
    store().processBlock(99, beyond.data(), 1);
    EXPECT_EQ(beyond[0], store().getValue(30, 0.7f));
}

}
