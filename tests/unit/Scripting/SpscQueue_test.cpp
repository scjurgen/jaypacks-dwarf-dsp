#include <atomic>
#include <cstdint>
#include <gtest/gtest.h>
#include <thread>

#include "scripting/SpscQueue.h"

namespace jp::script::test
{

TEST(SpscQueue, ItemsComeOutInOrderAndAFullQueueRefusesAndCounts)
{
    SpscQueue<int, 8> queue;
    int item = 0;
    EXPECT_FALSE(queue.pop(item));
    for (int i = 1; i <= 7; ++i)
    {
        EXPECT_TRUE(queue.push(i));
    }
    EXPECT_EQ(queue.size(), 7u);
    EXPECT_FALSE(queue.push(8)) << "capacity - 1 items";
    EXPECT_FALSE(queue.push(9));
    EXPECT_EQ(queue.dropped(), 2u);
    for (int i = 1; i <= 7; ++i)
    {
        ASSERT_TRUE(queue.pop(item));
        EXPECT_EQ(item, i);
    }
    EXPECT_FALSE(queue.pop(item));
    EXPECT_EQ(queue.size(), 0u);
}

TEST(SpscQueue, PeekLooksAtTheOldestItemAndDiscardTakesItAway)
{
    SpscQueue<int, 4> queue;
    int item = 0;
    EXPECT_FALSE(queue.peek(item));
    queue.discard(); // nothing to take: no harm
    ASSERT_TRUE(queue.push(7));
    ASSERT_TRUE(queue.push(8));
    ASSERT_TRUE(queue.peek(item));
    EXPECT_EQ(item, 7);
    ASSERT_TRUE(queue.peek(item));
    EXPECT_EQ(item, 7);
    EXPECT_EQ(queue.size(), 2u);
    queue.discard();
    ASSERT_TRUE(queue.peek(item));
    EXPECT_EQ(item, 8);
    ASSERT_TRUE(queue.pop(item));
    EXPECT_EQ(item, 8);
    EXPECT_FALSE(queue.peek(item));
}

TEST(SpscQueue, TheIndicesWrapAround)
{
    SpscQueue<uint32_t, 4> queue;
    uint32_t item = 0;
    uint32_t next = 0;
    uint32_t expected = 0;
    for (int round = 0; round < 1000; ++round)
    {
        ASSERT_TRUE(queue.push(next++));
        ASSERT_TRUE(queue.push(next++));
        ASSERT_TRUE(queue.pop(item));
        EXPECT_EQ(item, expected++);
        ASSERT_TRUE(queue.pop(item));
        EXPECT_EQ(item, expected++);
        EXPECT_FALSE(queue.pop(item));
    }
    EXPECT_EQ(queue.dropped(), 0u);
}

TEST(SpscQueue, TwoThreadsTransferEveryItemInOrder)
{
    constexpr uint32_t c_count{500000};
    SpscQueue<uint64_t, 64> queue;
    std::atomic<bool> failed{false};
    std::thread consumer(
        [&]
        {
            uint64_t expected = 0;
            uint64_t item = 0;
            while (expected < c_count)
            {
                if (queue.pop(item))
                {
                    if (item != expected * 3 + 1)
                    {
                        failed = true;
                        return;
                    }
                    ++expected;
                }
            }
        });
    for (uint64_t i = 0; i < c_count;)
    {
        if (queue.push(i * 3 + 1))
        {
            ++i;
        }
    }
    consumer.join();
    EXPECT_FALSE(failed);
}

}
