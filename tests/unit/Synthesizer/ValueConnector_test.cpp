#include <gtest/gtest.h>
#include <tuple>

#include "dsp/Synthesizer/ValueConnector.h"

namespace jp::test
{

namespace
{
// enumerators with gaps and with more members than any connector below lists
enum class Source
{
    FixValue = 1,
    Key = 2,
    Velocity = 4,
    DimensionX = 7,
    DimensionY = 8,
    Lfo = 11
};
}

TEST(ValueConnector, TheFormulaSeesTheValuesInTheOrderOfTheIdsNotOfTheEnumerators)
{
    using Sut = ValueConnector<128, Source, Source::DimensionX, Source::FixValue>;
    Sut sut{[](const Sut::Values& v) { return 10.0f * v[0] + v[1]; }};
    sut.setForced(Source::FixValue, 2.0f);
    sut.setForced(Source::DimensionX, 0.25f);
    EXPECT_FLOAT_EQ(sut.get(), 4.5f);
}

TEST(ValueConnector, ASourceThatIsNotListedIsIgnoredEvenBetweenListedOnes)
{
    // Key (2) lies between the listed FixValue (1) and DimensionX (7): it must not land in a listed slot
    using Sut = ValueConnector<128, Source, Source::FixValue, Source::DimensionX>;
    Sut sut{[](const Sut::Values& v) { return v[0] + 100.0f * v[1]; }};
    sut.setForced(Source::FixValue, 1.0f);
    sut.setForced(Source::DimensionX, 2.0f);
    sut.set(Source::Key, 999.0f);
    sut.setForced(Source::Lfo, 999.0f);
    sut.setForced(Source::Velocity, 999.0f);
    EXPECT_FLOAT_EQ(sut.get(), 201.0f);
    EXPECT_FALSE(sut.hasChangedValue()) << "an ignored source does not even start a ramp";
}

TEST(ValueConnector, SetRampsAndSetForcedJumps)
{
    using Sut = ValueConnector<8, Source, Source::FixValue>;
    Sut sut{[](const Sut::Values& v) { return v[0]; }};
    sut.setForced(Source::FixValue, 0.0f);
    sut.set(Source::FixValue, 1.0f);
    EXPECT_TRUE(sut.hasChangedValue());
    EXPECT_FLOAT_EQ(sut.get(), 0.125f);
    for (int i = 1; i < 8; ++i)
    {
        std::ignore = sut.get();
    }
    EXPECT_FALSE(sut.hasChangedValue());
    EXPECT_EQ(sut.get(), 1.0f);
    sut.setForced(Source::FixValue, -2.0f);
    EXPECT_EQ(sut.get(), -2.0f);
}

TEST(ValueConnector, TheFormulaIsEvaluatedWithAllStoredValues)
{
    using Sut = ValueConnector<4, Source, Source::FixValue, Source::DimensionX, Source::Lfo>;
    Sut sut{[](const Sut::Values& v) { return v[0] * 2.0f + v[1] + v[2]; }};
    sut.setForced(Source::FixValue, 1.0f);
    sut.setForced(Source::DimensionX, 0.5f);
    sut.setForced(Source::Lfo, 0.25f);
    EXPECT_FLOAT_EQ(sut.get(), 2.75f);
    sut.setForced(Source::DimensionX, 0.0f);
    EXPECT_FLOAT_EQ(sut.get(), 2.25f);
}

TEST(ValueConnectorWithPreset, PresetFastEvaluatesBothPartsAndJumps)
{
    using Sut = ValueConnectorWithPreset<128, Source, Source::FixValue, Source::Key, Source::DimensionX,
                                         Source::Velocity, Source::DimensionY>;
    Sut sut{[](const Sut::Values& v) { return v[0] * v[1]; },
            [](const float fixedPart, const Sut::Values& v) { return fixedPart + v[2] + v[3] * v[4]; }};
    sut.set(Source::FixValue, 0.5f);
    sut.set(Source::Key, 0.25f);
    sut.set(Source::DimensionX, 0.125f);
    sut.set(Source::Velocity, 1.0f);
    sut.set(Source::DimensionY, 0.75f);
    sut.presetFast();
    EXPECT_FALSE(sut.hasChangedValue());
    EXPECT_FLOAT_EQ(sut.get(), 1.0f);
}

TEST(ValueConnectorWithPreset, SetAfterPresetRampsTowardTheNewVariablePart)
{
    using Sut = ValueConnectorWithPreset<128, Source, Source::FixValue, Source::Key, Source::Lfo>;
    Sut sut{[](const Sut::Values& v) { return v[0] * v[1]; },
            [](const float fixedPart, const Sut::Values& v) { return fixedPart + v[2]; }};
    sut.set(Source::FixValue, 2.0f);
    sut.set(Source::Key, 0.25f);
    sut.set(Source::Lfo, 0.5f);
    sut.presetFast();
    EXPECT_FLOAT_EQ(sut.get(), 1.0f);

    float previous = sut.get();
    sut.set(Source::Lfo, 0.2f);
    for (int i = 0; i < 128; ++i)
    {
        const float current = sut.get();
        EXPECT_LT(current, previous);
        previous = current;
    }
    EXPECT_FLOAT_EQ(sut.get(), 0.7f);
}

TEST(ValueConnectorWithPreset, TheFixedPartChangesOnlyAtPresetFast)
{
    using Sut = ValueConnectorWithPreset<2, Source, Source::FixValue, Source::Lfo>;
    Sut sut{[](const Sut::Values& v) { return v[0]; },
            [](const float fixedPart, const Sut::Values& v) { return fixedPart + v[1]; }};
    sut.set(Source::FixValue, 3.0f);
    sut.presetFast();
    sut.set(Source::FixValue, 100.0f);
    std::ignore = sut.get();
    EXPECT_FLOAT_EQ(sut.get(), 3.0f) << "the variable part ran again with the old fixed part";
    sut.presetFast();
    EXPECT_FLOAT_EQ(sut.get(), 100.0f);
}

TEST(ValueConnectorWithPreset, ASourceThatIsNotListedIsIgnored)
{
    using Sut = ValueConnectorWithPreset<4, Source, Source::FixValue, Source::DimensionX>;
    Sut sut{[](const Sut::Values& v) { return v[0]; },
            [](const float fixedPart, const Sut::Values& v) { return fixedPart + v[1]; }};
    sut.set(Source::FixValue, 1.0f);
    sut.set(Source::DimensionX, 2.0f);
    sut.presetFast();
    sut.set(Source::Key, 50.0f);
    sut.set(Source::Lfo, 50.0f);
    EXPECT_FALSE(sut.hasChangedValue());
    EXPECT_FLOAT_EQ(sut.get(), 3.0f);
}

}
