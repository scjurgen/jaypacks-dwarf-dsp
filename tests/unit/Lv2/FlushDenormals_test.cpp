#include <cfloat>
#include <gtest/gtest.h>

#include "lv2/FlushDenormals.h"

namespace jp::test
{

namespace
{
// volatile keeps the compiler from folding the products at compile time
float halve(const float x)
{
    volatile float value = x;
    return value * 0.5f;
}
}

#if defined(__aarch64__) || defined(__x86_64__) || defined(__i386__)
TEST(FlushDenormals, DenormalResultsAreZeroOnlyInsideTheScope)
{
    const float smallestNormal = FLT_MIN;
    EXPECT_GT(halve(smallestNormal), 0.0f) << "denormal result outside the scope";
    {
        ScopedFlushDenormals guard;
        EXPECT_EQ(halve(smallestNormal), 0.0f);
        EXPECT_EQ(halve(1.0f), 0.5f) << "normal arithmetic is unchanged";
    }
    EXPECT_GT(halve(smallestNormal), 0.0f) << "mode restored";
}

TEST(FlushDenormals, NestedScopesRestoreTheOuterMode)
{
    ScopedFlushDenormals outer;
    {
        ScopedFlushDenormals inner;
    }
    EXPECT_EQ(halve(FLT_MIN), 0.0f);
}
#endif

}
