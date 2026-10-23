#include <array>
#include <bitset>
#include <gtest/gtest.h>

#include "dsp/Numbers/Random.h"
#include "dsp/Reverbs/WalshHadamard.h"

namespace jp::test
{

template <size_t N>
void expectSylvesterMatrix()
{
    // column j of the transform is the transform of the unit vector e_j
    for (size_t j = 0; j < N; ++j)
    {
        std::array<float, N> x{};
        x[j] = 1.0f;
        walshHadamardInPlace<N>(x.data());
        for (size_t i = 0; i < N; ++i)
        {
            const float expected = std::bitset<32>(i & j).count() % 2 == 0 ? 1.0f : -1.0f;
            ASSERT_EQ(x[i], expected) << "N " << N << " row " << i << " column " << j;
        }
    }
}

TEST(WalshHadamard, MatchesSylvesterMatrix)
{
    expectSylvesterMatrix<2>();
    expectSylvesterMatrix<4>();
    expectSylvesterMatrix<8>();
    expectSylvesterMatrix<16>();
    expectSylvesterMatrix<32>();
}

template <size_t N>
void expectInvolutionTimesN()
{
    Random random(42);
    for (int run = 0; run < 100; ++run)
    {
        std::array<float, N> x{};
        for (auto& v : x)
        {
            v = 2.0f * random.uniform() - 1.0f;
        }
        std::array<float, N> y{};
        walshHadamard<N>(x.data(), y.data());
        walshHadamardInPlace<N>(y.data());
        for (size_t i = 0; i < N; ++i)
        {
            ASSERT_NEAR(y[i], static_cast<float>(N) * x[i], 1e-5f * static_cast<float>(N)) << "N " << N;
        }
    }
}

TEST(WalshHadamard, AppliedTwiceGivesNTimesInput)
{
    expectInvolutionTimesN<4>();
    expectInvolutionTimesN<8>();
    expectInvolutionTimesN<16>();
    expectInvolutionTimesN<32>();
}

TEST(WalshHadamard, AllOnesAndSingleValue)
{
    // as AbacDsp's checkSumAndSingle and singleValue for the Walsh orders
    std::array<float, 16> ones{};
    ones.fill(1.0f);
    walshHadamardInPlace<16>(ones.data());
    EXPECT_EQ(ones[0], 16.0f);
    for (size_t i = 1; i < 16; ++i)
    {
        EXPECT_EQ(ones[i], 0.0f) << i;
    }
    std::array<float, 16> single{};
    single[0] = 1.0f;
    walshHadamardInPlace<16>(single.data());
    for (const float v : single)
    {
        EXPECT_EQ(v, 1.0f);
    }
}

TEST(WalshHadamard, RowFormsEqualTheVectorFormBitForBit)
{
    // the row forms transform every column with the same additions in the same order
    Random random(17);
    std::array<std::array<float, 16>, 16> rows{};
    for (auto& row : rows)
    {
        for (auto& v : row)
        {
            v = 2.0f * random.uniform() - 1.0f;
        }
    }
    std::array<std::array<float, 16>, 16> inPlace = rows;
    walshHadamardRowsInPlace<16, 16>(inPlace);
    std::array<std::array<float, 16>, 16> copied{};
    walshHadamardRows<16, 16>(rows, copied);
    for (size_t column = 0; column < 16; ++column)
    {
        std::array<float, 16> vector{};
        for (size_t k = 0; k < 16; ++k)
        {
            vector[k] = rows[k][column];
        }
        walshHadamardInPlace<16>(vector.data());
        for (size_t k = 0; k < 16; ++k)
        {
            ASSERT_EQ(inPlace[k][column], vector[k]) << k << " " << column;
            ASSERT_EQ(copied[k][column], vector[k]) << k << " " << column;
        }
    }
}
}
