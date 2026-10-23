#include <cmath>
#include <gtest/gtest.h>
#include <tuple>
#include <vector>

#include "dsp/Generators/AdsEnvelope.h"

namespace jp::test
{

namespace
{
constexpr float c_rate{48000.0f};

Envelope<4> adsr(const float attackMs, const float decayMs, const float sustain, const float releaseMs,
                 const float curve = 0.0f)
{
    Envelope<4> env(c_rate);
    env.setSegment(0, attackMs, 1.0f, curve);
    env.setSegment(1, decayMs, sustain, curve);
    env.setSegmentHoldPreviousValue(2, 1000.0f);
    env.markSustainAt(2);
    env.setSegment(3, releaseMs, 0.0f, curve);
    return env;
}

template <typename Env>
std::vector<float> run(Env& env, const size_t steps)
{
    std::vector<float> out(steps);
    for (float& x : out)
    {
        x = env.step();
    }
    return out;
}
}

TEST(CurvedValue, RunsFromZeroToOneForEveryFactor)
{
    for (const float factor : {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f})
    {
        CurvedValue curve;
        curve.setFactor(factor);
        EXPECT_NEAR(curve.get(0.0f), 0.0f, 1e-6f) << factor;
        EXPECT_NEAR(curve.get(1.0f), 1.0f, 1e-5f) << factor;
    }
}

TEST(CurvedValue, ZeroIsStraightAndTheSignBowsTheCurve)
{
    CurvedValue straight;
    EXPECT_FLOAT_EQ(straight.get(0.3f), 0.3f);
    CurvedValue slowStart;
    slowStart.setFactor(1.0f);
    CurvedValue fastStart;
    fastStart.setFactor(-1.0f);
    EXPECT_NEAR(slowStart.get(0.5f), 0.707f, 1e-3f);
    EXPECT_NEAR(fastStart.get(0.5f), 0.382f, 1e-3f);
    EXPECT_GT(slowStart.get(0.5f), 0.5f) << "a positive factor bows up";
    EXPECT_LT(fastStart.get(0.5f), 0.5f) << "a negative factor bows down";
}

TEST(CurvedValue, TheFactorsWithinOneAreMonotonic)
{
    for (const float factor : {-1.0f, -0.5f, 0.5f, 1.0f})
    {
        CurvedValue curve;
        curve.setFactor(factor);
        float previous = -1.0f;
        for (int i = 0; i <= 1000; ++i)
        {
            const float v = curve.get(static_cast<float>(i) / 1000.0f);
            ASSERT_GE(v, previous) << factor;
            previous = v;
        }
    }
}

TEST(EnvelopeShaper, RampsInExactlyTheGivenStepsAndHoldsTheTarget)
{
    EnvelopeShaper shaper;
    shaper.reset(0.0f);
    shaper.setNewFramesAndTarget(10, 1.0f, 0.0f);
    for (int i = 1; i <= 10; ++i)
    {
        EXPECT_NEAR(shaper.step(), static_cast<float>(i) / 10.0f, 1e-6f);
    }
    EXPECT_TRUE(shaper.isDone());
    for (int i = 0; i < 5; ++i)
    {
        EXPECT_NEAR(shaper.step(), 1.0f, 1e-6f);
    }
}

TEST(EnvelopeShaper, AZeroStepSegmentJumpsToItsTarget)
{
    EnvelopeShaper shaper;
    shaper.reset(0.2f);
    shaper.setNewFramesAndTarget(0, 0.9f, 0.0f);
    EXPECT_TRUE(shaper.isDone());
    EXPECT_FLOAT_EQ(shaper.step(), 0.9f);
}

TEST(EnvelopeShaper, ASegmentStartsFromTheLastGain)
{
    EnvelopeShaper shaper;
    shaper.reset(0.0f);
    shaper.setNewFramesAndTarget(4, 1.0f, 0.0f);
    std::ignore = shaper.step();
    std::ignore = shaper.step();
    EXPECT_NEAR(shaper.currentGain(), 0.5f, 1e-6f);
    shaper.setNewFramesAndTarget(2, 0.0f, 0.0f);
    EXPECT_NEAR(shaper.step(), 0.25f, 1e-6f);
    EXPECT_NEAR(shaper.step(), 0.0f, 1e-6f);
}

TEST(EnvelopeShaper, ModifyTargetKeepsTheRemainingTime)
{
    EnvelopeShaper shaper;
    shaper.reset(0.0f);
    shaper.setNewFramesAndTarget(10, 1.0f, 0.0f);
    for (int i = 0; i < 4; ++i)
    {
        std::ignore = shaper.step();
    }
    shaper.modifyTarget(0.0f);
    for (int i = 0; i < 5; ++i)
    {
        std::ignore = shaper.step();
    }
    EXPECT_FALSE(shaper.isDone()) << "one step of the original ten is left";
    EXPECT_NEAR(shaper.step(), 0.0f, 1e-5f);
    EXPECT_TRUE(shaper.isDone());
}

TEST(AdsEnvelope, RisesThroughTheAttackThenDecaysToTheSustainLevelAndHolds)
{
    AdsEnvelope env(c_rate);
    env.setAttackDecaySustain(10.0f, 200.0f, 0.4f);
    env.trigger();
    const auto attackSteps = static_cast<int>(10.0f * c_rate / 1000.0f);
    for (int i = 1; i < attackSteps; ++i)
    {
        std::ignore = env.step();
    }
    EXPECT_NEAR(env.step(), 1.0f, 1e-3f) << "the peak comes on the last step of the attack";
    EXPECT_EQ(env.currentSegmentIndex(), 1u);
    for (int i = 0; i < 20000; ++i)
    {
        std::ignore = env.step();
    }
    EXPECT_NEAR(env.step(), 0.4f, 1e-4f);
    EXPECT_EQ(env.currentSegmentIndex(), 2u);
    EXPECT_FALSE(env.isDone());
    for (int i = 0; i < 5000; ++i)
    {
        ASSERT_NEAR(env.step(), 0.4f, 1e-4f);
    }
}

TEST(AdsEnvelope, ARetriggerContinuesFromTheCurrentGain)
{
    AdsEnvelope env(c_rate);
    env.setAttackDecaySustain(10.0f, 200.0f, 0.4f);
    env.trigger();
    std::ignore = run(env, 20000);
    EXPECT_NEAR(env.step(), 0.4f, 1e-3f);
    env.trigger();
    EXPECT_NEAR(env.step(), 0.4f, 0.01f) << "no jump to 0";
    EXPECT_EQ(env.currentSegmentIndex(), 0u);
}

TEST(AdsEnvelope, ZeroTimeSegmentsJumpAtOnce)
{
    AdsEnvelope env(c_rate);
    env.setAttackDecaySustain(0.0f, 0.0f, 0.6f);
    env.trigger();
    std::ignore = env.step();
    EXPECT_NEAR(env.step(), 0.6f, 1e-4f);
    EXPECT_NEAR(env.step(), 0.6f, 1e-4f);
}

TEST(AdsEnvelope, EveryCurveStaysFiniteAndReachesTheSustainLevel)
{
    for (const float curve : {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f})
    {
        AdsEnvelope env(c_rate);
        env.setAttackDecaySustain(20.0f, 20.0f, 0.3f, curve);
        env.trigger();
        for (const float v : run(env, 5000))
        {
            ASSERT_TRUE(std::isfinite(v)) << curve;
            ASSERT_LE(v, 1.01f) << curve;
        }
        EXPECT_NEAR(env.step(), 0.3f, 1e-3f) << curve;
    }
}

TEST(Envelope, ReleaseRunsFromTheSustainToZeroInTheReleaseTime)
{
    auto env = adsr(10.0f, 100.0f, 0.5f, 50.0f);
    env.trigger();
    std::ignore = run(env, 20000);
    EXPECT_EQ(env.currentSegmentIndex(), 2u);
    env.release();
    EXPECT_EQ(env.currentSegmentIndex(), 3u);
    const auto releaseSteps = static_cast<size_t>(50.0f * c_rate / 1000.0f);
    const auto out = run(env, releaseSteps);
    EXPECT_NEAR(out.front(), 0.5f - 0.5f / static_cast<float>(releaseSteps), 1e-4f);
    EXPECT_NEAR(out.back(), 0.0f, 1e-4f);
    for (size_t i = 1; i < out.size(); ++i)
    {
        ASSERT_LE(out[i], out[i - 1] + 1e-6f);
    }
    EXPECT_TRUE(env.isDone());
    EXPECT_NEAR(env.step(), 0.0f, 1e-4f);
}

TEST(Envelope, ReleaseDuringTheAttackStartsFromTheGainReached)
{
    auto env = adsr(100.0f, 100.0f, 0.5f, 50.0f);
    env.trigger();
    std::ignore = run(env, 2400); // half of the attack
    const float reached = env.step();
    EXPECT_NEAR(reached, 0.5f, 0.01f);
    EXPECT_EQ(env.currentSegmentIndex(), 0u);
    env.release();
    EXPECT_EQ(env.currentSegmentIndex(), 3u)
        << "release is the segment after the sustain point, wherever the envelope is";
    EXPECT_LE(env.step(), reached);
}

TEST(Envelope, AnEnvelopeWithoutSustainRunsThroughAndStaysAtTheLastTarget)
{
    Envelope<2> env(c_rate);
    env.setSegment(0, 10.0f, 1.0f);
    env.setSegment(1, 10.0f, 0.25f);
    env.noSustain();
    env.trigger();
    std::ignore = run(env, 2000);
    EXPECT_TRUE(env.isDone());
    EXPECT_NEAR(env.step(), 0.25f, 1e-5f);
    env.release();
    EXPECT_TRUE(env.isDone()) << "nothing to release";
}

TEST(Envelope, OneShotIgnoresTheSustainPointAndTheRelease)
{
    auto env = adsr(10.0f, 10.0f, 0.5f, 10.0f);
    env.setOneshot(true);
    env.trigger();
    std::ignore = run(env, 480 + 480 + 48000 + 10); // attack, decay, the 1000 ms hold segment
    EXPECT_EQ(env.currentSegmentIndex(), 3u) << "through the sustain segment without holding";
    env.release();
    EXPECT_EQ(env.currentSegmentIndex(), 3u);
    std::ignore = run(env, 1000);
    EXPECT_TRUE(env.isDone());
    EXPECT_NEAR(env.step(), 0.0f, 1e-4f);
}

TEST(Envelope, TriggerFromStartsFromAGivenValue)
{
    auto env = adsr(10.0f, 100.0f, 0.5f, 50.0f);
    env.triggerFrom(0.8f);
    EXPECT_GT(env.step(), 0.8f);
    EXPECT_LT(env.step(), 1.0f);
}

TEST(Envelope, EmergencyReleaseJumpsToTheLastSegmentFromAnyState)
{
    auto env = adsr(10.0f, 200.0f, 0.4f, 100.0f);
    env.trigger();
    std::ignore = run(env, 100);
    ASSERT_EQ(env.currentSegmentIndex(), 0u);
    env.emergencyRelease(5.0f);
    EXPECT_EQ(env.currentSegmentIndex(), 3u);
    const auto steps = static_cast<size_t>(5.0f * c_rate / 1000.0f);
    for (float v : run(env, steps))
    {
        ASSERT_TRUE(std::isfinite(v));
    }
    EXPECT_NEAR(env.step(), 0.0f, 1e-3f);
}

TEST(Envelope, QuickModifyReachesOnlyTheSegmentThatIsPlaying)
{
    auto env = adsr(10.0f, 200.0f, 0.4f, 100.0f);
    env.trigger();
    std::ignore = run(env, 500); // past the 480-step attack: the decay plays
    ASSERT_EQ(env.currentSegmentIndex(), 1u);
    env.setSegment(1, 200.0f, 0.1f);
    env.setSegmentHoldPreviousValue(2, 1000.0f);
    env.markSustainAt(2);
    const float before = env.currentSegmentIndex() == 1u ? env.step() : 0.0f;
    env.quickModifyIfSegmentActive(0, 100);
    EXPECT_LT(env.step(), before) << "the attack is not playing: the decay goes on";
    env.quickModifyIfSegmentActive(1, 100);
    std::ignore = run(env, 100);
    EXPECT_NEAR(env.step(), 0.1f, 1e-3f) << "the decay now lands in 100 steps on the new target";
}

TEST(Envelope, ModifyTargetIfActiveRetargetsTheSegmentInFlight)
{
    auto env = adsr(10.0f, 200.0f, 0.4f, 100.0f);
    env.trigger();
    std::ignore = run(env, 1000);
    ASSERT_EQ(env.currentSegmentIndex(), 1u);
    env.setSegment(1, 200.0f, 0.2f);
    env.setSegmentHoldPreviousValue(2, 1000.0f);
    env.markSustainAt(2);
    env.modifyTargetIfActive(1);
    std::ignore = run(env, 20000);
    EXPECT_NEAR(env.step(), 0.2f, 1e-3f);
}

}
