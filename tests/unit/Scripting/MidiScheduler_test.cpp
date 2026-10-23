#include <cstdint>
#include <gtest/gtest.h>
#include <vector>

#include "scripting/MidiScheduler.h"

namespace jp::script::test
{

namespace
{

struct Sent
{
    uint64_t sample;
    uint8_t status;
    uint8_t data1;
    uint8_t data2;
    uint8_t size;

    bool operator==(const Sent& other) const
    {
        return sample == other.sample && status == other.status && data1 == other.data1 && data2 == other.data2 &&
               size == other.size;
    }
};

/// What the adapter's MidiOutput does with send(): takes the message at a frame of the stretch being processed.
struct Output
{
    bool send(const uint32_t frame, const uint8_t* bytes, const uint32_t size)
    {
        if (refuseAll || (refuseNoteOffs && (bytes[0] & 0xF0) == 0x80))
        {
            return false;
        }
        sent.push_back({start + frame, bytes[0], bytes[1], bytes[2], static_cast<uint8_t>(size)});
        return true;
    }

    uint64_t start{0};
    bool refuseAll{false};
    bool refuseNoteOffs{false};
    std::vector<Sent> sent;
};

template <size_t N>
void run(MidiScheduler<N>& scheduler, Output& output, const uint64_t length, const uint32_t block)
{
    for (uint64_t start = 0; start < length; start += block)
    {
        output.start = start;
        scheduler.emit(output, start, static_cast<uint32_t>(std::min<uint64_t>(block, length - start)));
    }
}

MidiBytes on(const uint8_t channel, const uint8_t note, const uint8_t velocity = 100)
{
    return MidiBytes{static_cast<uint8_t>(0x90 | channel), note, velocity, 3};
}

MidiBytes off(const uint8_t channel, const uint8_t note)
{
    return MidiBytes{static_cast<uint8_t>(0x80 | channel), note, 0, 3};
}

}

TEST(MidiScheduler, MessagesLeaveAtTheirSampleWhateverTheBlockLength)
{
    std::vector<Sent> reference;
    for (const uint32_t block : {1u, 3u, 64u, 128u, 509u, 4096u})
    {
        MidiScheduler<64> scheduler;
        scheduler.setBudget(1000, 128);
        for (uint8_t i = 0; i < 20; ++i)
        {
            scheduler.schedule(100 + 977ull * i, on(i % 16, 40 + i));
            scheduler.schedule(100 + 977ull * i + 500, off(i % 16, 40 + i));
        }
        Output output;
        run(scheduler, output, 30000, block);
        ASSERT_EQ(output.sent.size(), 40u) << block;
        if (reference.empty())
        {
            reference = output.sent;
        }
        EXPECT_EQ(output.sent, reference) << "block " << block;
        EXPECT_EQ(output.sent[0].sample, 100u);
        EXPECT_EQ(output.sent[1].sample, 600u);
        EXPECT_EQ(scheduler.late(), 0u);
        EXPECT_EQ(scheduler.guard().count(), 0u);
    }
}

TEST(MidiScheduler, ANoteIsRememberedUntilItsNoteOffHasGoneOut)
{
    MidiScheduler<64> scheduler;
    Output output;
    scheduler.schedule(10, on(2, 60));
    scheduler.schedule(10, on(2, 64));
    scheduler.schedule(20, on(5, 60));
    scheduler.schedule(500, off(2, 60));
    run(scheduler, output, 100, 100);
    EXPECT_EQ(scheduler.guard().count(), 3u);
    EXPECT_TRUE(scheduler.guard().sounding(2, 60));
    EXPECT_TRUE(scheduler.guard().sounding(5, 60));
    EXPECT_FALSE(scheduler.guard().sounding(2, 61));
    output.start = 100;
    scheduler.emit(output, 100, 1000);
    EXPECT_FALSE(scheduler.guard().sounding(2, 60));
    EXPECT_EQ(scheduler.guard().count(), 2u);
}

TEST(MidiScheduler, ANoteOnWithVelocityZeroAndAllNotesOffEndNotes)
{
    MidiScheduler<64> scheduler;
    Output output;
    scheduler.schedule(1, on(0, 60));
    scheduler.schedule(2, on(0, 61));
    scheduler.schedule(3, on(1, 62));
    scheduler.schedule(10, on(0, 60, 0));
    scheduler.schedule(20, MidiBytes{0xB0, 123, 0, 3});
    run(scheduler, output, 15, 15);
    EXPECT_FALSE(scheduler.guard().sounding(0, 60)) << "velocity 0 is a note off";
    EXPECT_TRUE(scheduler.guard().sounding(0, 61));
    output.start = 15;
    scheduler.emit(output, 15, 100);
    EXPECT_FALSE(scheduler.guard().sounding(0, 61)) << "CC 123 cleared the channel";
    EXPECT_TRUE(scheduler.guard().sounding(1, 62)) << "and only that channel";
}

TEST(MidiScheduler, PanicEndsEveryNoteAtOnceAndDropsWhatWasWaiting)
{
    MidiScheduler<64> scheduler;
    Output output;
    scheduler.schedule(1, on(0, 60));
    scheduler.schedule(2, on(3, 72));
    scheduler.schedule(3, on(3, 73));
    scheduler.schedule(5000, on(0, 99));  // still waiting
    scheduler.schedule(6000, off(0, 60)); // so is this
    run(scheduler, output, 100, 100);
    ASSERT_EQ(scheduler.guard().count(), 3u);
    output.sent.clear();
    scheduler.panic(150);
    output.start = 100;
    scheduler.emit(output, 100, 128);
    const std::vector<Sent> wanted{{150, 0x80, 60, 0, 3}, {150, 0x83, 72, 0, 3}, {150, 0x83, 73, 0, 3}};
    EXPECT_EQ(output.sent, wanted);
    EXPECT_EQ(scheduler.guard().count(), 0u);
    EXPECT_EQ(scheduler.pending(), 0u);
    output.start = 5000;
    scheduler.emit(output, 5000, 2000);
    EXPECT_EQ(output.sent.size(), 3u) << "nothing of what was waiting comes out later";
}

TEST(MidiScheduler, ANoteOffAlwaysFindsRoomAndPushesALaterMessageOut)
{
    MidiScheduler<8> scheduler;
    scheduler.setBudget(1000, 128);
    for (uint8_t i = 0; i < 8; ++i)
    {
        ASSERT_TRUE(scheduler.schedule(1000 + i, on(0, i)));
    }
    EXPECT_FALSE(scheduler.schedule(2000, on(0, 99))) << "a note on finds the buffer full";
    EXPECT_EQ(scheduler.dropped(), 1u);
    EXPECT_TRUE(scheduler.schedule(10, off(0, 3))) << "a note off pushes the last note on out";
    EXPECT_EQ(scheduler.dropped(), 2u);
    Output output;
    run(scheduler, output, 3000, 3000);
    ASSERT_EQ(output.sent.size(), 8u);
    EXPECT_EQ(output.sent.front(), (Sent{10, 0x80, 3, 0, 3}));
    EXPECT_EQ(output.sent.back().data1, 6) << "note 7, due last, was the one that made room";
}

TEST(MidiScheduler, AScriptInALoopIsHeldToTheBudgetButNoteOffsAlwaysGoOut)
{
    MidiScheduler<2048> scheduler;
    scheduler.setBudget(4, 128);
    for (int i = 0; i < 40; ++i)
    {
        scheduler.schedule(10, on(0, static_cast<uint8_t>(i)));
    }
    Output output;
    run(scheduler, output, 128, 128);
    EXPECT_EQ(output.sent.size(), 4u);
    EXPECT_EQ(scheduler.refused(), 36u);
    EXPECT_EQ(scheduler.guard().count(), 4u);
    for (uint8_t note = 0; note < 4; ++note)
    {
        scheduler.schedule(200, off(0, note));
    }
    for (int i = 0; i < 10; ++i)
    {
        scheduler.schedule(200, on(1, static_cast<uint8_t>(i)));
    }
    output.start = 128;
    scheduler.emit(output, 128, 128);
    size_t offs = 0;
    size_t ons = 0;
    for (size_t i = 4; i < output.sent.size(); ++i)
    {
        (output.sent[i].status == 0x80 ? offs : ons)++;
    }
    EXPECT_EQ(offs, 4u) << "the note offs are not counted against the budget";
    EXPECT_EQ(ons, 4u);
    EXPECT_EQ(scheduler.guard().count(), 4u) << "the four notes of channel 1";
}

TEST(MidiScheduler, AMessageTheOutputRefusesIsNotRememberedAndANoteOffItRefusesStaysRemembered)
{
    MidiScheduler<16> scheduler;
    Output output;
    output.refuseAll = true;
    scheduler.schedule(1, on(0, 60));
    run(scheduler, output, 10, 10);
    EXPECT_EQ(scheduler.guard().count(), 0u);
    output.refuseAll = false;
    scheduler.schedule(11, on(0, 60));
    output.start = 10;
    scheduler.emit(output, 10, 10);
    ASSERT_TRUE(scheduler.guard().sounding(0, 60));
    output.refuseNoteOffs = true;
    scheduler.schedule(21, off(0, 60));
    output.start = 20;
    scheduler.emit(output, 20, 10);
    EXPECT_TRUE(scheduler.guard().sounding(0, 60)) << "the note off did not go out: the note still sounds";
    scheduler.panic(30);
    output.refuseNoteOffs = false;
    output.start = 30;
    scheduler.emit(output, 30, 10);
    EXPECT_EQ(scheduler.guard().count(), 0u) << "and a panic finds it";
}

TEST(MidiScheduler, AMessageAfterItsTimeGoesOutAtTheFirstFrameAndIsCounted)
{
    MidiScheduler<16> scheduler;
    Output output;
    scheduler.schedule(5, on(0, 60));
    output.start = 1000;
    scheduler.emit(output, 1000, 128);
    ASSERT_EQ(output.sent.size(), 1u);
    EXPECT_EQ(output.sent[0].sample, 1000u);
    EXPECT_EQ(scheduler.late(), 1u);
}

TEST(NoteGuard, ChannelsAndNotesAreKeptApart)
{
    NoteGuard guard;
    guard.noteOn(15, 127);
    guard.noteOn(0, 0);
    guard.noteOn(7, 64);
    EXPECT_EQ(guard.count(), 3u);
    EXPECT_TRUE(guard.sounding(15, 127));
    EXPECT_TRUE(guard.sounding(0, 0));
    EXPECT_FALSE(guard.sounding(0, 127));
    guard.noteOff(7, 64);
    guard.noteOff(7, 64);
    EXPECT_EQ(guard.count(), 2u);
    guard.allOff(15);
    EXPECT_EQ(guard.count(), 1u);
    size_t visited = 0;
    guard.forEach(
        [&](const uint8_t channel, const uint8_t note)
        {
            EXPECT_EQ(channel, 0);
            EXPECT_EQ(note, 0);
            ++visited;
        });
    EXPECT_EQ(visited, 1u);
}

}
