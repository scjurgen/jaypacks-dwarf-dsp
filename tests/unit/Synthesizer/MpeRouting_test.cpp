#include <gtest/gtest.h>

#include "dsp/Synthesizer/MpeRouting.h"

namespace jp::test
{

TEST(MpeRouting, ADefaultSlotDrivesNothing)
{
    const MpeCtrl slot;
    EXPECT_EQ(slot.target, CtrlTarget::None);
    EXPECT_EQ(slot.depth, 0.0f);
    EXPECT_EQ(mpeCtrlValue(slot, 12345), 0.0f);
}

TEST(MpeRouting, AbsSlotIsTheCurveTimesDepth)
{
    MpeCtrl slot;
    slot.curve = CtrlCurve::Square;
    slot.depth = 3.0f;
    EXPECT_FLOAT_EQ(mpeCtrlValue(slot, c_mpeMaxValue), 3.0f);
    EXPECT_FLOAT_EQ(mpeCtrlValue(slot, 0), 0.0f);
    EXPECT_FLOAT_EQ(mpeCtrlValue(slot, 8192), 3.0f * mpeCurve(CtrlCurve::Square, 8192));
}

TEST(MpeRouting, BipolarSlotIsSignedAroundTheCentre)
{
    MpeCtrl slot;
    slot.type = CtrlValueType::BiPolar;
    slot.curve = CtrlCurve::Linear;
    slot.depth = 2.0f / 12.0f;
    EXPECT_FLOAT_EQ(mpeCtrlValue(slot, c_mpeMaxValue), 2.0f / 12.0f);
    EXPECT_FLOAT_EQ(mpeCtrlValue(slot, 0), -2.0f / 12.0f);
    EXPECT_EQ(mpeCtrlValue(slot, 8192), 0.0f);
}

TEST(MpeRouting, NegativeDepthInvertsTheSlot)
{
    MpeCtrl slot;
    slot.curve = CtrlCurve::Linear;
    slot.depth = -1.0f;
    EXPECT_FLOAT_EQ(mpeCtrlValue(slot, c_mpeMaxValue), -1.0f);
}

TEST(MpeRouting, TheEnumeratorsKeepTheirNumbers)
{
    EXPECT_EQ(static_cast<int>(CtrlDimension::X), 0);
    EXPECT_EQ(static_cast<int>(CtrlDimension::EnvelopeAmplitude), 6);
    EXPECT_EQ(static_cast<int>(CtrlTarget::None), 0);
    EXPECT_EQ(static_cast<int>(CtrlTarget::FilterCutoff), 3);
    EXPECT_EQ(static_cast<int>(CtrlTarget::Distortion), 16);
    EXPECT_EQ(static_cast<int>(CtrlCurve::CubeRoot), 0);
    EXPECT_EQ(static_cast<int>(CtrlCurve::Cube), 4);
    EXPECT_EQ(static_cast<int>(CtrlValueType::BiPolar), 1);
    EXPECT_EQ(c_mpeSlotCount, 10u);
}

}
