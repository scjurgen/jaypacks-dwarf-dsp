#include <algorithm>
#include <cstdint>
#include <gtest/gtest.h>
#include <utility>
#include <vector>

#include "scripting/TimedEventBuffer.h"

namespace jp::script::test
{

namespace
{

using Buffer = TimedEventBuffer<int, 16>;

/// (absolute sample, item) of everything the buffer hands out while the audio is cut into blocks of a given length.
std::vector<std::pair<uint64_t, int>> runBlocks(Buffer& buffer, const uint64_t length, const uint32_t block)
{
    std::vector<std::pair<uint64_t, int>> out;
    for (uint64_t start = 0; start < length; start += block)
    {
        const auto frames = static_cast<uint32_t>(std::min<uint64_t>(block, length - start));
        buffer.emitDue(start, frames,
                       [&](const uint32_t frame, const int item) { out.emplace_back(start + frame, item); });
    }
    return out;
}

}

TEST(TimedEventBuffer, ItemsComeOutAtTheirExactSampleWhateverTheBlockLength)
{
    const std::vector<std::pair<uint64_t, int>> wanted{{0, 1},   {1, 2},    {127, 3},  {128, 4}, {129, 5},
                                                       {500, 6}, {4095, 7}, {4096, 8}, {9000, 9}};
    for (const uint32_t block : {1u, 2u, 7u, 64u, 127u, 128u, 513u, 4096u, 10000u})
    {
        Buffer buffer;
        for (auto it = wanted.rbegin(); it != wanted.rend(); ++it)
        {
            ASSERT_TRUE(buffer.insert(it->first, it->second)) << "put in backwards";
        }
        EXPECT_EQ(runBlocks(buffer, 10000, block), wanted) << "block " << block;
        EXPECT_EQ(buffer.size(), 0u);
        EXPECT_EQ(buffer.late(), 0u);
    }
}

TEST(TimedEventBuffer, ItemsOfTheSameTimeKeepTheirOrder)
{
    Buffer buffer;
    for (int i = 0; i < 6; ++i)
    {
        buffer.insert(100, i);
    }
    buffer.insert(50, 99);
    buffer.insert(100, 6);
    const auto out = runBlocks(buffer, 200, 64);
    const std::vector<std::pair<uint64_t, int>> wanted{{50, 99}, {100, 0}, {100, 1}, {100, 2},
                                                       {100, 3}, {100, 4}, {100, 5}, {100, 6}};
    EXPECT_EQ(out, wanted);
}

TEST(TimedEventBuffer, AnItemThatIsDueLaterStaysInTheBufferUntilItsBlock)
{
    Buffer buffer;
    buffer.insert(300, 1);
    int count = 0;
    buffer.emitDue(0, 128, [&](uint32_t, int) { ++count; });
    buffer.emitDue(128, 128, [&](uint32_t, int) { ++count; });
    EXPECT_EQ(count, 0);
    EXPECT_EQ(buffer.size(), 1u);
    buffer.emitDue(256, 128,
                   [&](uint32_t frame, int)
                   {
                       EXPECT_EQ(frame, 44u);
                       ++count;
                   });
    EXPECT_EQ(count, 1);
}

TEST(TimedEventBuffer, AnItemAfterItsTimeComesOutAtTheFirstFrameAndIsCounted)
{
    Buffer buffer;
    buffer.insert(10, 1);
    buffer.insert(20, 2);
    buffer.insert(1000, 3);
    std::vector<std::pair<uint32_t, int>> out;
    buffer.emitDue(500, 128, [&](const uint32_t frame, const int item) { out.emplace_back(frame, item); });
    const std::vector<std::pair<uint32_t, int>> wanted{{0, 1}, {0, 2}};
    EXPECT_EQ(out, wanted);
    EXPECT_EQ(buffer.late(), 2u);
    EXPECT_EQ(buffer.size(), 1u);
}

TEST(TimedEventBuffer, AFullBufferDropsTheNewItemAndCountsIt)
{
    Buffer buffer;
    for (int i = 0; i < 16; ++i)
    {
        ASSERT_TRUE(buffer.insert(static_cast<uint64_t>(i), i));
    }
    EXPECT_FALSE(buffer.insert(5, 100));
    EXPECT_EQ(buffer.dropped(), 1u);
    EXPECT_EQ(buffer.size(), 16u);
}

TEST(TimedEventBuffer, InsertingWithEvictionMakesRoomByDroppingTheLastItem)
{
    Buffer buffer;
    for (int i = 0; i < 16; ++i)
    {
        buffer.insert(100 + static_cast<uint64_t>(i), i);
    }
    EXPECT_TRUE(buffer.insertEvicting(50, 77));
    EXPECT_EQ(buffer.dropped(), 1u);
    const auto out = runBlocks(buffer, 200, 200);
    ASSERT_EQ(out.size(), 16u);
    EXPECT_EQ(out.front(), (std::pair<uint64_t, int>{50, 77}));
    EXPECT_EQ(out.back().second, 14) << "item 15, the last, was evicted";
    // an item that would be the last itself is the one that goes
    Buffer full;
    for (int i = 0; i < 16; ++i)
    {
        full.insert(static_cast<uint64_t>(i), i);
    }
    EXPECT_FALSE(full.insertEvicting(1000, 5));
    EXPECT_EQ(full.dropped(), 1u);
    EXPECT_EQ(full.size(), 16u);
}

TEST(TimedEventBuffer, ClearTakesEverythingOut)
{
    Buffer buffer;
    buffer.insert(1, 1);
    buffer.insert(2, 2);
    buffer.clear();
    EXPECT_EQ(buffer.size(), 0u);
    EXPECT_TRUE(runBlocks(buffer, 100, 10).empty());
}

}
