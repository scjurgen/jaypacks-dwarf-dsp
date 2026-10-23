#include <gtest/gtest.h>

// The device toolchain (moddwarf-new, GCC 9.4) builds the plugins as C++17. Code that
// compiles on the Mac must not rely on a newer standard, so the test build is pinned too.
static_assert(__cplusplus == 201703L, "the code base is C++17 (device toolchain GCC 9.4)");

TEST(Toolchain, CompilesAsCpp17)
{
    EXPECT_EQ(__cplusplus, 201703L);
}
