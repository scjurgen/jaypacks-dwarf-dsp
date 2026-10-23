#include <array>
#include <gtest/gtest.h>
#include <vector>

#include "dsp/Synthesizer/VoiceAllocator.h"

namespace jp::test
{

namespace
{
template <size_t N>
struct Pool
{
    VoiceAllocator<N> allocator;
    std::array<bool, N> silent;

    Pool()
    {
        silent.fill(true);
    }

    VoiceAssignment play(const int channel, const int note)
    {
        const auto assignment = allocator.allocate(channel, note, [this](const size_t i) { return silent[i]; });
        silent[assignment.voice] = false;
        return assignment;
    }
};
}

TEST(VoiceAllocator, SilentVoicesAreUsedFirstLowestIndexFirst)
{
    Pool<4> pool;
    EXPECT_EQ(pool.play(0, 60).voice, 0u);
    EXPECT_EQ(pool.play(0, 62).voice, 1u);
    pool.silent[0] = true;
    EXPECT_EQ(pool.play(0, 64).voice, 0u);
    EXPECT_EQ(pool.play(0, 65).voice, 2u);
}

TEST(VoiceAllocator, WithEveryVoiceBusyTheOneAssignedLongestAgoIsStolen)
{
    Pool<3> pool;
    EXPECT_EQ(pool.play(0, 60).voice, 0u);
    EXPECT_EQ(pool.play(0, 61).voice, 1u);
    EXPECT_EQ(pool.play(0, 62).voice, 2u);
    EXPECT_EQ(pool.play(0, 63).voice, 0u) << "oldest is voice 0";
    EXPECT_EQ(pool.play(0, 64).voice, 1u) << "then voice 1: voice 0 was just assigned";
    EXPECT_EQ(pool.play(0, 65).voice, 2u);
    EXPECT_EQ(pool.play(0, 66).voice, 0u);
}

TEST(VoiceAllocator, AVoiceInItsReleaseIsNotFree)
{
    // the caller reports silence only when the release tail has ended
    Pool<2> pool;
    EXPECT_EQ(pool.play(0, 60).voice, 0u);
    EXPECT_EQ(pool.play(0, 61).voice, 1u);
    EXPECT_EQ(pool.play(0, 62).voice, 0u) << "both busy: steal the oldest, not a voice in release";
}

TEST(VoiceAllocator, ThePreviousNoteStartsAtSixtyAndFollowsTheLastAllocation)
{
    Pool<4> pool;
    EXPECT_EQ(pool.play(0, 72).previousNote, 60);
    EXPECT_EQ(pool.play(0, 48).previousNote, 72);
    EXPECT_EQ(pool.play(5, 50).previousNote, 48);
    pool.allocator.clear();
    EXPECT_EQ(pool.play(0, 30).previousNote, 60);
}

TEST(VoiceAllocator, TheAssignmentIsRecordedPerVoice)
{
    Pool<4> pool;
    const auto first = pool.play(3, 61);
    const auto second = pool.play(7, 70);
    EXPECT_EQ(pool.allocator.channelOf(first.voice), 3);
    EXPECT_EQ(pool.allocator.noteOf(first.voice), 61);
    EXPECT_EQ(pool.allocator.channelOf(second.voice), 7);
    EXPECT_EQ(pool.allocator.noteOf(second.voice), 70);
    EXPECT_EQ(pool.allocator.channelOf(3), -1) << "a voice that was never assigned has no channel";
}

TEST(VoiceAllocator, ANoteIsVisitedOnEveryVoiceThatPlaysIt)
{
    Pool<4> pool;
    pool.play(1, 60);
    pool.play(1, 62);
    pool.play(2, 60);
    pool.play(1, 60); // retrigger during the release of the first: two voices play (1, 60)
    std::vector<size_t> visited;
    pool.allocator.forNote(1, 60, [&](const size_t voice) { visited.push_back(voice); });
    EXPECT_EQ(visited, (std::vector<size_t>{0, 3}));
    visited.clear();
    pool.allocator.forNote(9, 60, [&](const size_t voice) { visited.push_back(voice); });
    EXPECT_TRUE(visited.empty());
}

TEST(VoiceAllocator, AChannelIsVisitedOnEveryVoiceItOwns)
{
    Pool<4> pool;
    pool.play(1, 60);
    pool.play(2, 62);
    pool.play(1, 64);
    std::vector<size_t> visited;
    pool.allocator.forChannel(1, [&](const size_t voice) { visited.push_back(voice); });
    EXPECT_EQ(visited, (std::vector<size_t>{0, 2}));
}

TEST(VoiceAllocator, ClearForgetsTheAssignmentsAndTheAges)
{
    Pool<2> pool;
    pool.play(1, 60);
    pool.play(1, 61);
    pool.allocator.clear();
    std::vector<size_t> visited;
    pool.allocator.forChannel(1, [&](const size_t voice) { visited.push_back(voice); });
    EXPECT_TRUE(visited.empty());
    pool.silent.fill(true);
    EXPECT_EQ(pool.play(0, 50).voice, 0u);
}

TEST(VoiceAllocator, ManyNotesKeepTheOrderOfTheAges)
{
    Pool<3> pool;
    std::array<size_t, 3> expected{0, 1, 2};
    for (int i = 0; i < 3000; ++i)
    {
        EXPECT_EQ(pool.play(0, 40 + i % 40).voice, expected[static_cast<size_t>(i) % 3]) << i;
    }
}

}
