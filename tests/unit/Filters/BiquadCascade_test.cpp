#include <cmath>
#include <gtest/gtest.h>

#include "dsp/Filters/BiquadCascade.h"
#include "tests/unit/TestHelpers.h"

namespace jp::test
{

TEST(BiquadCascade, ResponseIsTheProductOfItsSections)
{
    constexpr float fs{48000.0f};
    const std::array<BiquadDesign, 3> designs{{{BiquadFilterType::HighPass, 100.0f, 0.9f, 0.0f},
                                               {BiquadFilterType::Peak, 2400.0f, 2.0f, 5.0f},
                                               {BiquadFilterType::LowPass, 5000.0f, 0.8f, 0.0f}}};
    BiquadCascade<4> cascade;
    for (size_t i = 0; i < designs.size(); ++i)
    {
        cascade.setSection(i, designBiquad(designs[i], fs));
    }
    cascade.setSection(3, firstOrderHighpassSection(80.0, fs));
    for (const double f : {50.0, 100.0, 1000.0, 2400.0, 6000.0})
    {
        double sumDb = toDb(std::abs(biquadSectionResponse(firstOrderHighpassSection(80.0, fs), f, fs)));
        for (const auto& d : designs)
        {
            sumDb += toDb(std::abs(biquadSectionResponse(designBiquad(d, fs), f, fs)));
        }
        EXPECT_NEAR(toDb(std::abs(cascade.response(f, fs))), sumDb, 1e-9) << f;
        cascade.reset();
        const double measured =
            measureSineGain([&cascade](const float x) { return cascade.process(x); }, f, fs, 0.1, 0.3, 0.5);
        EXPECT_NEAR(toDb(measured), sumDb, 0.01) << f;
    }
}

TEST(BiquadCascade, DefaultSectionsPassThrough)
{
    BiquadCascade<3> cascade;
    for (int i = 0; i < 100; ++i)
    {
        const float x = 0.01f * static_cast<float>(i) - 0.3f;
        EXPECT_EQ(cascade.process(x), x);
    }
}

}
