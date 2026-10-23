#include <cmath>
#include <cstdint>
#include <gtest/gtest.h>
#include <tuple>
#include <vector>

#include "dsp/Numbers/Random.h"

namespace jp::test
{

TEST(Random, MatchesXoshiro256StarStarReferenceVector)
{
    // first outputs of the reference implementation for the state {1, 2, 3, 4}
    Random random;
    random.setState({1, 2, 3, 4});
    EXPECT_EQ(random.next(), 11520ULL);
    EXPECT_EQ(random.next(), 0ULL);
    EXPECT_EQ(random.next(), 1509978240ULL);
    EXPECT_EQ(random.next(), 1215971899390074240ULL);
    EXPECT_EQ(random.next(), 1216172134540287360ULL);
}

TEST(Random, SameSeedGivesSameSequence)
{
    Random a(1234);
    Random b(1234);
    for (int i = 0; i < 1000; ++i)
    {
        ASSERT_EQ(a.next(), b.next());
    }
}

TEST(Random, DifferentSeedsGiveDifferentSequences)
{
    Random a(1);
    Random b(2);
    int equal = 0;
    for (int i = 0; i < 1000; ++i)
    {
        equal += a.next() == b.next() ? 1 : 0;
    }
    EXPECT_EQ(equal, 0);
}

TEST(Random, InstancesAreIndependent)
{
    // stepping one instance must not influence another (the old generator shared one global state)
    Random reference(77);
    Random a(77);
    Random b(77);
    for (int i = 0; i < 100; ++i)
    {
        std::ignore = b.next();
        std::ignore = b.next();
        ASSERT_EQ(a.next(), reference.next());
    }
}

TEST(Random, SeedZeroGivesValidState)
{
    Random random(0);
    uint64_t combined = 0;
    for (int i = 0; i < 4; ++i)
    {
        combined |= random.next();
    }
    EXPECT_NE(combined, 0ULL);
}

TEST(Random, UniformIsInUnitIntervalWithCorrectMoments)
{
    Random random(42);
    constexpr int n = 1000000;
    double sum = 0.0;
    double sumSquares = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const float u = random.uniform();
        ASSERT_GE(u, 0.0f);
        ASSERT_LT(u, 1.0f);
        sum += u;
        sumSquares += static_cast<double>(u) * u;
    }
    const double mean = sum / n;
    const double variance = sumSquares / n - mean * mean;
    EXPECT_NEAR(mean, 0.5, 0.002);
    EXPECT_NEAR(variance, 1.0 / 12.0, 0.001);
}

TEST(Random, UniformHas24BitResolution)
{
    Random random(9);
    for (int i = 0; i < 1000; ++i)
    {
        const double scaled = static_cast<double>(random.uniform()) * 16777216.0;
        ASSERT_EQ(scaled, std::floor(scaled));
    }
}

TEST(Random, UniformDoubleIsInUnitInterval)
{
    Random random(5);
    double sum = 0.0;
    constexpr int n = 100000;
    for (int i = 0; i < n; ++i)
    {
        const double u = random.uniformDouble();
        ASSERT_GE(u, 0.0);
        ASSERT_LT(u, 1.0);
        sum += u;
    }
    // the old getRandDouble() never exceeded 2^-32
    EXPECT_NEAR(sum / n, 0.5, 0.01);
}

TEST(Random, GaussianHasStandardNormalStatistics)
{
    Random random(2024);
    constexpr int n = 1000000;
    double sum = 0.0;
    double sumSquares = 0.0;
    int withinOneSigma = 0;
    int beyondThreeSigma = 0;
    for (int i = 0; i < n; ++i)
    {
        const double g = random.gaussian();
        ASSERT_TRUE(std::isfinite(g));
        sum += g;
        sumSquares += g * g;
        withinOneSigma += std::abs(g) < 1.0 ? 1 : 0;
        beyondThreeSigma += std::abs(g) > 3.0 ? 1 : 0;
    }
    const double mean = sum / n;
    EXPECT_NEAR(mean, 0.0, 0.005);
    EXPECT_NEAR(std::sqrt(sumSquares / n - mean * mean), 1.0, 0.005);
    EXPECT_NEAR(static_cast<double>(withinOneSigma) / n, 0.6827, 0.003);
    EXPECT_NEAR(static_cast<double>(beyondThreeSigma) / n, 0.0027, 0.0005);
}

TEST(Random, GaussianIsDeterministicPerSeed)
{
    Random a(31);
    Random b(31);
    for (int i = 0; i < 1001; ++i)
    {
        ASSERT_EQ(a.gaussian(), b.gaussian());
    }
    // reseeding drops the cached second value of the Box-Muller pair
    a.seed(31);
    Random c(31);
    EXPECT_EQ(a.gaussian(), c.gaussian());
}

}
