#include <gtest/gtest.h>

#include "dsp/Synthesizer/MpeZone.h"

namespace jp::test
{

TEST(MpeZone, TheDefaultIsTheLowerZoneWithAllSixteenChannelsInside)
{
    const MpeZone zone;
    EXPECT_EQ(zone.masterChannel(), 1);
    EXPECT_EQ(zone.lowerMemberChannel(), 2);
    EXPECT_EQ(zone.upperMemberChannel(), 16);
    for (int channel = 0; channel < 16; ++channel)
    {
        EXPECT_TRUE(zone.accepts(channel)) << channel;
    }
}

TEST(MpeZone, ChannelsOutsideTheMemberRangeAndTheMasterAreRejected)
{
    MpeZone zone;
    zone.setMasterChannel(1);
    zone.setLowerMemberChannel(2);
    zone.setUpperMemberChannel(8);
    EXPECT_TRUE(zone.accepts(0));
    for (int channel = 1; channel <= 7; ++channel)
    {
        EXPECT_TRUE(zone.accepts(channel)) << channel;
    }
    for (int channel = 8; channel < 16; ++channel)
    {
        EXPECT_FALSE(zone.accepts(channel)) << channel;
    }
}

TEST(MpeZone, SettersTakeChannelsOneToSixteenAndClamp)
{
    MpeZone zone;
    zone.setMasterChannel(16);
    zone.setLowerMemberChannel(1);
    zone.setUpperMemberChannel(4);
    EXPECT_EQ(zone.masterChannel(), 16);
    EXPECT_TRUE(zone.accepts(15));
    EXPECT_TRUE(zone.accepts(0));
    EXPECT_TRUE(zone.accepts(3));
    EXPECT_FALSE(zone.accepts(4));
    zone.setMasterChannel(0);
    zone.setUpperMemberChannel(99);
    EXPECT_EQ(zone.masterChannel(), 1);
    EXPECT_EQ(zone.upperMemberChannel(), 16);
}

TEST(MpeZone, AnEmptyMemberRangeLeavesOnlyTheMaster)
{
    MpeZone zone;
    zone.setMasterChannel(3);
    zone.setLowerMemberChannel(10);
    zone.setUpperMemberChannel(5);
    for (int channel = 0; channel < 16; ++channel)
    {
        EXPECT_EQ(zone.accepts(channel), channel == 2) << channel;
    }
}

TEST(MpeZone, ChannelsThatAreNotOnTheWireAreRejected)
{
    const MpeZone zone;
    EXPECT_FALSE(zone.accepts(-1));
    EXPECT_FALSE(zone.accepts(16));
}

}
