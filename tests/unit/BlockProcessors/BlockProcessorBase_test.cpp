#include <array>
#include <gtest/gtest.h>

#include "dsp/BlockProcessors/BlockProcessorBase.h"

namespace jp::test
{

namespace
{
constexpr size_t c_block{4};
constexpr size_t c_order{4};

class AddConstant final : public BlockProcessorBase<c_block>
{
  public:
    explicit AddConstant(const float value)
        : m_value(value)
    {
    }

    void process(std::array<float, c_block>& block) noexcept override
    {
        for (auto& x : block)
        {
            x += m_value;
        }
        ++processCount;
    }

    void reset() noexcept override
    {
        ++resetCount;
    }

    int processCount{0};
    int resetCount{0};

  private:
    float m_value;
};

using Rows = std::array<std::array<float, c_block>, c_order>;
}

TEST(CallbackManager, HasCallbackReflectsSetAndRemove)
{
    CallbackManager<c_order, c_block> sut;
    AddConstant processor(1.0f);
    EXPECT_FALSE(sut.hasCallback(1));
    sut.setCallback(1, &processor);
    EXPECT_TRUE(sut.hasCallback(1));
    sut.removeCallback(1);
    EXPECT_FALSE(sut.hasCallback(1));
}

TEST(CallbackManager, OutOfRangeIndexIsSafeNoOp)
{
    CallbackManager<c_order, c_block> sut;
    AddConstant processor(1.0f);
    sut.setCallback(c_order, &processor);
    sut.removeCallback(c_order + 5);
    EXPECT_FALSE(sut.hasCallback(c_order));
    Rows rows{};
    sut.processCallbacks(rows);
    EXPECT_EQ(processor.processCount, 0);
}

TEST(CallbackManager, ProcessCallbacksDispatchesToRegisteredRowsOnly)
{
    CallbackManager<c_order, c_block> sut;
    AddConstant addOne(1.0f);
    AddConstant addTen(10.0f);
    sut.setCallback(0, &addOne);
    sut.setCallback(2, &addTen);
    Rows rows{};
    sut.processCallbacks(rows);
    for (size_t i = 0; i < c_block; ++i)
    {
        EXPECT_EQ(rows[0][i], 1.0f);
        EXPECT_EQ(rows[1][i], 0.0f);
        EXPECT_EQ(rows[2][i], 10.0f);
        EXPECT_EQ(rows[3][i], 0.0f);
    }
}

TEST(CallbackManager, OneProcessorInTwoSlotsRunsTwice)
{
    CallbackManager<c_order, c_block> sut;
    AddConstant processor(1.0f);
    sut.setCallback(0, &processor);
    sut.setCallback(3, &processor);
    Rows rows{};
    sut.processCallbacks(rows);
    EXPECT_EQ(processor.processCount, 2);
}

TEST(CallbackManager, ResetForwardsOnlyToRegisteredProcessors)
{
    CallbackManager<c_order, c_block> sut;
    AddConstant registered(1.0f);
    AddConstant removed(1.0f);
    sut.setCallback(1, &registered);
    sut.setCallback(2, &removed);
    sut.removeCallback(2);
    sut.reset();
    EXPECT_EQ(registered.resetCount, 1);
    EXPECT_EQ(removed.resetCount, 0);
}

}
