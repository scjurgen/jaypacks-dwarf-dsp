#include <array>
#include <gtest/gtest.h>

#include "dsp/Numbers/PrimeDispatcher.h"

namespace jp::test
{

TEST(PrimeDispatcher, IsPrimeNumber)
{
    EXPECT_FALSE(isPrimeNumber(0));
    EXPECT_FALSE(isPrimeNumber(1));
    EXPECT_TRUE(isPrimeNumber(2));
    EXPECT_TRUE(isPrimeNumber(3));
    EXPECT_FALSE(isPrimeNumber(4));
    EXPECT_TRUE(isPrimeNumber(5));
    EXPECT_FALSE(isPrimeNumber(9));
    EXPECT_TRUE(isPrimeNumber(13));
    EXPECT_FALSE(isPrimeNumber(15));
    EXPECT_TRUE(isPrimeNumber(17));
    EXPECT_TRUE(isPrimeNumber(97));
    EXPECT_FALSE(isPrimeNumber(100));
}

TEST(PrimeDispatcher, IsPrimeNumberAgreesWithSieve)
{
    constexpr size_t n = 30000;
    std::array<bool, n> composite{};
    for (size_t i = 2; i * i < n; ++i)
    {
        if (!composite[i])
        {
            for (size_t j = i * i; j < n; j += i)
            {
                composite[j] = true;
            }
        }
    }
    for (size_t i = 2; i < n; ++i)
    {
        ASSERT_EQ(isPrimeNumber(i), !composite[i]) << i;
    }
}

TEST(PrimeDispatcher, GetUsefulPrime)
{
    EXPECT_EQ(getUsefulPrime<3>(0), 3u);
    EXPECT_EQ(getUsefulPrime<3>(1), 3u);
    EXPECT_EQ(getUsefulPrime<3>(2), 3u);
    EXPECT_EQ(getUsefulPrime<3>(3), 3u);
    EXPECT_EQ(getUsefulPrime<3>(11), 11u);
    EXPECT_EQ(getUsefulPrime<3>(12), 13u);
    EXPECT_EQ(getUsefulPrime<3>(97), 97u);
    EXPECT_EQ(getUsefulPrime<7>(12), 13u);
    EXPECT_EQ(getUsefulPrime<5>(100), 101u);
    EXPECT_EQ(getUsefulPrime<11>(1), 11u);
}

TEST(PrimeDispatcher, GenerateUniquePrimeSet)
{
    const std::array<size_t, 4> values{5, 5, 7, 7};
    std::array<size_t, 4> out{};
    generateUniquePrimeSet<3>(values.data(), out.data(), values.size());
    EXPECT_EQ(out, (std::array<size_t, 4>{5, 7, 11, 13}));
}

TEST(PrimeDispatcher, GenerateUniquePrimeSetSortsByValue)
{
    const std::array<size_t, 4> values{12, 8, 9, 11};
    std::array<size_t, 4> out{};
    generateUniquePrimeSet<3>(values.data(), out.data(), values.size());
    EXPECT_EQ(out, (std::array<size_t, 4>{11, 13, 17, 19}));
}

TEST(PrimeDispatcher, EnsureUniqueDiscreteSize)
{
    std::array<size_t, 4> values{11, 11, 13, 13};
    ensureUniqueDiscreteSize(values.data(), values.size());
    EXPECT_EQ(values, (std::array<size_t, 4>{11, 13, 17, 19}));
}

TEST(PrimeDispatcher, EnsureUniqueKeepsDistinctValues)
{
    std::array<size_t, 3> values{20, 10, 30};
    ensureUniqueDiscreteSize(values.data(), values.size());
    EXPECT_EQ(values, (std::array<size_t, 3>{10, 20, 30}));
}

TEST(PrimeDispatcher, LargeInputHandling)
{
    constexpr size_t largeInput = 1500000000;
    EXPECT_EQ(getUsefulPrime<3>(largeInput), largeInput);
}

}
