#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "dsp/SynthHandling/SustainPedalHandler.h"

namespace jp::test
{

namespace
{
/// Records the events as "on c/n/v" and "off c/n/v" strings.
class Recorder : public NoteListener
{
  public:
    void noteOn(const int channel, const int note, const int velocity) override
    {
        events.push_back("on " + text(channel, note, velocity));
    }

    void noteOff(const int channel, const int note, const int velocity) override
    {
        events.push_back("off " + text(channel, note, velocity));
    }

    std::vector<std::string> events;

  private:
    static std::string text(const int channel, const int note, const int velocity)
    {
        return std::to_string(channel) + "/" + std::to_string(note) + "/" + std::to_string(velocity);
    }
};

using Events = std::vector<std::string>;
}

TEST(SustainPedalHandler, NoteOnAndOffWithoutThePedalAreForwardedAtOnce)
{
    Recorder recorder;
    SustainPedalHandler<4> handler;
    handler.setListener(&recorder);
    handler.noteOn(0, 60, 100);
    EXPECT_EQ(recorder.events, (Events{"on 0/60/100"}));
    handler.noteOff(0, 60, 40);
    EXPECT_EQ(recorder.events, (Events{"on 0/60/100", "off 0/60/40"}));
    EXPECT_EQ(handler.activeCount(), 0u);
}

TEST(SustainPedalHandler, RetriggeringAnActiveNoteClosesTheOldInstanceFirst)
{
    Recorder recorder;
    SustainPedalHandler<4> handler;
    handler.setListener(&recorder);
    handler.noteOn(0, 60, 100);
    handler.noteOn(0, 60, 110);
    EXPECT_EQ(recorder.events, (Events{"on 0/60/100", "off 0/60/100", "on 0/60/110"}))
        << "the old instance ends before the new one starts; its note-off velocity was never given";
    EXPECT_EQ(handler.activeCount(), 1u);
    handler.noteOff(0, 60, 0);
    EXPECT_EQ(recorder.events.back(), "off 0/60/0") << "the one physical note-off reaches the new instance";
    EXPECT_EQ(handler.activeCount(), 0u);
}

TEST(SustainPedalHandler, UnderThePedalANoteOffIsDeferredUntilThePedalLifts)
{
    Recorder recorder;
    SustainPedalHandler<4> handler;
    handler.setListener(&recorder);
    handler.setSustain(true);
    handler.noteOn(0, 60, 100);
    handler.noteOff(0, 60, 33);
    EXPECT_EQ(recorder.events, (Events{"on 0/60/100"}));
    EXPECT_EQ(handler.activeCount(), 1u);
    handler.setSustain(false);
    EXPECT_EQ(recorder.events, (Events{"on 0/60/100", "off 0/60/33"})) << "the recorded velocity is replayed";
    EXPECT_EQ(handler.activeCount(), 0u);
}

TEST(SustainPedalHandler, LiftingThePedalLeavesNotesThatAreStillHeld)
{
    Recorder recorder;
    SustainPedalHandler<4> handler;
    handler.setListener(&recorder);
    handler.noteOn(0, 60, 100);
    handler.setSustain(true);
    handler.noteOn(0, 64, 100);
    handler.noteOff(0, 64, 0);
    handler.setSustain(false);
    EXPECT_EQ(recorder.events, (Events{"on 0/60/100", "on 0/64/100", "off 0/64/0"}));
    EXPECT_EQ(handler.activeCount(), 1u);
    handler.noteOff(0, 60, 0);
    EXPECT_EQ(recorder.events.back(), "off 0/60/0");
}

TEST(SustainPedalHandler, ANoteStruckAgainAfterItsReleaseUnderThePedalSurvivesTheLift)
{
    Recorder recorder;
    SustainPedalHandler<4> handler;
    handler.setListener(&recorder);
    handler.setSustain(true);
    handler.noteOn(0, 60, 100);
    handler.noteOff(0, 60, 20);
    handler.noteOn(0, 60, 90); // struck again while the first still rings under the pedal
    handler.setSustain(false);
    EXPECT_EQ(recorder.events, (Events{"on 0/60/100", "off 0/60/20", "on 0/60/90"}))
        << "the lift must not cut the new note, which is still held";
    EXPECT_EQ(handler.activeCount(), 1u);
}

TEST(SustainPedalHandler, PedalDownAndUpWithoutNotesDoesNothing)
{
    Recorder recorder;
    SustainPedalHandler<4> handler;
    handler.setListener(&recorder);
    handler.setSustain(true);
    EXPECT_TRUE(handler.isSustaining());
    handler.setSustain(false);
    EXPECT_FALSE(handler.isSustaining());
    EXPECT_TRUE(recorder.events.empty());
}

TEST(SustainPedalHandler, WithAllSlotsUsedTheOldestNoteIsEnded)
{
    Recorder recorder;
    SustainPedalHandler<2> handler;
    handler.setListener(&recorder);
    handler.noteOn(0, 60, 100);
    handler.noteOn(0, 61, 100);
    handler.noteOn(0, 62, 100);
    EXPECT_EQ(recorder.events, (Events{"on 0/60/100", "on 0/61/100", "off 0/60/100", "on 0/62/100"}));
    EXPECT_EQ(handler.activeCount(), 2u);
    handler.noteOn(0, 63, 100);
    EXPECT_EQ(recorder.events.back(), "on 0/63/100");
    EXPECT_EQ(recorder.events[recorder.events.size() - 2], "off 0/61/100") << "then the next oldest";
}

TEST(SustainPedalHandler, TheSameNoteOnTwoChannelsIsTwoNotes)
{
    Recorder recorder;
    SustainPedalHandler<4> handler;
    handler.setListener(&recorder);
    handler.noteOn(1, 60, 100);
    handler.noteOn(2, 60, 100);
    EXPECT_EQ(handler.activeCount(), 2u);
    handler.noteOff(2, 60, 0);
    EXPECT_EQ(recorder.events.back(), "off 2/60/0");
    EXPECT_EQ(handler.activeCount(), 1u);
    handler.noteOff(1, 60, 0);
    EXPECT_EQ(recorder.events.back(), "off 1/60/0");
}

TEST(SustainPedalHandler, ANoteOffOfAnUnknownNoteIsDropped)
{
    Recorder recorder;
    SustainPedalHandler<4> handler;
    handler.setListener(&recorder);
    handler.noteOff(0, 60, 0);
    handler.noteOn(0, 61, 100);
    handler.noteOff(1, 61, 0);
    EXPECT_EQ(recorder.events, (Events{"on 0/61/100"}));
}

TEST(SustainPedalHandler, AllNotesOffEndsEverythingAndClearsThePedal)
{
    Recorder recorder;
    SustainPedalHandler<4> handler;
    handler.setListener(&recorder);
    handler.setSustain(true);
    handler.noteOn(0, 60, 100);
    handler.noteOn(0, 64, 100);
    handler.noteOff(0, 64, 10);
    handler.allNotesOff();
    EXPECT_EQ(recorder.events, (Events{"on 0/60/100", "on 0/64/100", "off 0/60/100", "off 0/64/10"}));
    EXPECT_EQ(handler.activeCount(), 0u);
    EXPECT_FALSE(handler.isSustaining());
    recorder.events.clear();
    handler.noteOn(0, 67, 100);
    handler.noteOff(0, 67, 0);
    EXPECT_EQ(recorder.events, (Events{"on 0/67/100", "off 0/67/0"})) << "the pedal is up: no deferral";
}

TEST(SustainPedalHandler, WithoutAListenerTheNotesAreStillTracked)
{
    SustainPedalHandler<4> handler;
    handler.noteOn(0, 60, 100);
    EXPECT_EQ(handler.activeCount(), 1u);
    handler.noteOff(0, 60, 0);
    EXPECT_EQ(handler.activeCount(), 0u);
}

}
