// Commands as scripts call them: the checks against their specs, and the documents generated from the specs.

#include <cmath>
#include <gtest/gtest.h>
#include <string>

#include "ToyBindings.h"
#include "scripting/ApiDescription.h"
#include "scripting/BindingCheck.h"
#include "scripting/LuaRuntime.h"

namespace jp::script::test
{

namespace
{

struct Rig
{
    Rig()
        : runtime(toyBindings(), sink, nullptr, Limits{}, 1)
    {
    }

    Status load(const std::string& text)
    {
        return runtime.load("test", text);
    }

    [[nodiscard]] bool logged(const std::string& fragment) const
    {
        for (const std::string& line : runtime.log())
        {
            if (line.find(fragment) != std::string::npos)
            {
                return true;
            }
        }
        return false;
    }

    RecordingSink sink;
    Runtime runtime;
};

}

TEST(Binding, PositionalArgumentsAndTableFieldsArriveInTheOrderOfTheSpec)
{
    Rig rig;
    ASSERT_EQ(rig.load("SetVoice(1, {level = 0.25, detune = -3, wave = Wave.Saw, label = 'lead', on = true})"),
              Status::Ok)
        << rig.runtime.error();
    ASSERT_EQ(rig.sink.calls.size(), 1u);
    const CommandCall& call = rig.sink.calls[0];
    EXPECT_EQ(call.command, 1u);
    EXPECT_EQ(call.valueCount, 6u);
    EXPECT_EQ(call.integer(0), 1);
    EXPECT_FLOAT_EQ(call.real(1), 0.25f);
    EXPECT_FLOAT_EQ(call.real(2), -3.0f);
    EXPECT_EQ(call.integer(3), 1);
    EXPECT_STREQ(call.string(4), "lead");
    EXPECT_TRUE(call.flag(5));
    for (size_t i = 0; i < 6; ++i)
    {
        EXPECT_TRUE(call.given[i]) << i;
    }
}

TEST(Binding, AMissingFieldTakesItsDefault)
{
    Rig rig;
    ASSERT_EQ(rig.load("SetVoice(2, {detune = 5}) SetVoice(0) SetVoice(0, nil) SetVoice(0, {})"), Status::Ok);
    ASSERT_EQ(rig.sink.calls.size(), 4u);
    EXPECT_FLOAT_EQ(rig.sink.calls[0].real(1), 0.5f);
    EXPECT_FALSE(rig.sink.calls[0].given[1]);
    EXPECT_TRUE(rig.sink.calls[0].given[2]);
    EXPECT_FALSE(rig.sink.calls[0].flag(5));
    EXPECT_STREQ(rig.sink.calls[0].string(4), "");
    for (size_t i = 1; i < 4; ++i)
    {
        EXPECT_FLOAT_EQ(rig.sink.calls[i].real(1), 0.5f) << i;
    }
}

TEST(Binding, NumbersAreClampedWholeNumbersAreRangeChecked)
{
    Rig rig;
    ASSERT_EQ(rig.load("SetGain(100) SetGain(-100) SetVoice(0, {level = 7, detune = -99})"), Status::Ok);
    ASSERT_EQ(rig.sink.calls.size(), 3u);
    EXPECT_FLOAT_EQ(rig.sink.calls[0].real(0), 12.0f);
    EXPECT_FLOAT_EQ(rig.sink.calls[1].real(0), -60.0f);
    EXPECT_FLOAT_EQ(rig.sink.calls[2].real(1), 1.0f);
    EXPECT_FLOAT_EQ(rig.sink.calls[2].real(2), -12.0f);
}

TEST(Binding, AnUnusableValueDropsTheCallAndSaysWhy)
{
    Rig rig;
    ASSERT_EQ(rig.load("SetVoice(7)\n"
                       "SetVoice(1.5)\n"
                       "SetGain(0 / 0)\n"
                       "SetGain(math.huge)\n"
                       "SetVoice(0, {wave = 3})\n"
                       "SetVoice(0, {label = string.rep('x', 40)})\n"
                       "SetGain(-6)"),
              Status::Ok);
    ASSERT_EQ(rig.sink.calls.size(), 1u) << "only the last call is usable";
    EXPECT_FLOAT_EQ(rig.sink.calls[0].real(0), -6.0f);
    ASSERT_EQ(rig.runtime.log().size(), 6u);
    EXPECT_NE(rig.runtime.log()[0].find("test:1:"), std::string::npos) << "the line of the script";
    EXPECT_TRUE(rig.logged("'index' = 7 is outside 0..2, call ignored"));
    EXPECT_TRUE(rig.logged("'index' = 1.5 is not a whole number"));
    EXPECT_TRUE(rig.logged("'db' is not a finite number"));
    EXPECT_TRUE(rig.logged("'wave' = 3 is outside 0..2"));
    EXPECT_TRUE(rig.logged("is longer than 31 characters"));
}

TEST(Binding, AWrongTypeOrArityIsAnErrorThatStopsTheScript)
{
    Rig rig;
    EXPECT_EQ(rig.load("SetGain('loud')"), Status::RuntimeError);
    EXPECT_NE(rig.runtime.error().find("SetGain: 'db' must be a number, not string"), std::string::npos)
        << rig.runtime.error();
    EXPECT_EQ(rig.load("SetGain()"), Status::RuntimeError);
    EXPECT_NE(rig.runtime.error().find("expects 1 argument(s) and got 0"), std::string::npos) << rig.runtime.error();
    EXPECT_EQ(rig.load("SetGain(1, 2)"), Status::RuntimeError);
    EXPECT_NE(rig.runtime.error().find("takes at most 1"), std::string::npos) << rig.runtime.error();
    EXPECT_EQ(rig.load("SetVoice(0, 5)"), Status::RuntimeError);
    EXPECT_NE(rig.runtime.error().find("must be a table of fields, not number"), std::string::npos)
        << rig.runtime.error();
    EXPECT_EQ(rig.load("SetVoice(0, {on = 1})"), Status::RuntimeError);
    EXPECT_NE(rig.runtime.error().find("'on' must be true or false, not number"), std::string::npos)
        << rig.runtime.error();
    EXPECT_EQ(rig.load("SetVoice(0, {label = 5})"), Status::RuntimeError);
    EXPECT_EQ(rig.load("SetPair(1)"), Status::RuntimeError);
    EXPECT_EQ(rig.load("SetPair(1, {})"), Status::RuntimeError);
    EXPECT_TRUE(rig.sink.calls.empty());
}

TEST(Binding, AFieldTheCommandDoesNotKnowIsReportedAndTheRestStillCounts)
{
    Rig rig;
    ASSERT_EQ(rig.load("SetVoice(0, {level = 0.1, detun = 2, 7})"), Status::Ok);
    ASSERT_EQ(rig.sink.calls.size(), 1u);
    EXPECT_FLOAT_EQ(rig.sink.calls[0].real(1), 0.1f);
    EXPECT_FLOAT_EQ(rig.sink.calls[0].real(2), 0.0f) << "the misspelled field left the default";
    EXPECT_TRUE(rig.logged("unknown field 'detun' is ignored"));
    EXPECT_TRUE(rig.logged("entries without a name are ignored"));
}

TEST(Binding, ThePluginMayRejectACommandAndTheScriptGoesOn)
{
    Rig rig;
    ASSERT_EQ(rig.load("SetVoice(0, {label = 'bad'}) SetGain(1)"), Status::Ok);
    ASSERT_EQ(rig.sink.calls.size(), 1u);
    EXPECT_TRUE(rig.logged("SetVoice: the label 'bad' is not allowed, call ignored"));
}

TEST(Binding, CommandsMayBeCalledAtTheTopLevelAndFromHooks)
{
    Rig rig;
    ASSERT_EQ(rig.load("SetGain(1) function OnStart() SetGain(2) end"), Status::Ok);
    ASSERT_EQ(rig.runtime.call("OnStart"), Status::Ok);
    ASSERT_EQ(rig.sink.calls.size(), 2u);
    EXPECT_FLOAT_EQ(rig.sink.calls[1].real(0), 2.0f);
}

TEST(Binding, TheApiDescriptionListsEverythingTheTableDefines)
{
    const std::string text = apiMarkdown(toyBindings(), Limits{});
    for (const char* fragment :
         {"# Toy scripting API", "### SetGain(db)", "### SetVoice(index, { fields })", "### SetPair(a, b)",
          "| `db` | number | -60 to 12 | level in dB |", "| `index` | whole number | 0 to 2 | which voice |",
          "| `level` | number | 0 to 1 | 0.5 | linear level |",
          "| `wave` | whole number | 0 to 2 | 0 | waveform (a Wave constant) |",
          "| `label` | string | up to 31 characters |  | a short name |",
          "| `on` | true or false |  | 0 | switched on |", "### OnNoteOn(channel, note, velocity)", "### OnStart()",
          "### Wave", "- `Wave.Saw` = 1", "100000 VM instructions"})
    {
        EXPECT_NE(text.find(fragment), std::string::npos) << fragment << "\n" << text;
    }
    EXPECT_NE(text.find("1024 kB of memory"), std::string::npos);
}

TEST(Binding, TheConstantsFileIsValidLuaAndAgreesWithTheGlobals)
{
    const std::string text = constantsLua(toyBindings());
    EXPECT_NE(text.find("Wave = {\n    Sine = 0,\n    Saw = 1,\n    Square = 2,\n}"), std::string::npos) << text;
    Rig rig;
    ASSERT_EQ(rig.load(text + "\nassert(Wave.Saw == 1)"), Status::Ok) << rig.runtime.error();
}


TEST(BindingCheck, TheToyBindingsAreFine)
{
    EXPECT_EQ(checkBindings(toyBindings()), "");
}

TEST(BindingCheck, AReservedWordOfLuaIsNoNameForAFieldOrAnythingElse)
{
    EXPECT_TRUE(isLuaKeyword("repeat"));
    EXPECT_TRUE(isLuaKeyword("end"));
    EXPECT_FALSE(isLuaKeyword("repeating"));
    EXPECT_TRUE(isLuaName("wave2"));
    EXPECT_FALSE(isLuaName("2wave"));
    EXPECT_FALSE(isLuaName("with space"));
    EXPECT_FALSE(isLuaName("a-b"));
    EXPECT_FALSE(isLuaName(""));

    static constexpr std::array<ValueSpec, 1> badFields{{{"repeat", ValueKind::Flag, 0.0, 1.0, 0.0, "x"}}};
    static constexpr std::array<CommandSpec, 1> commands{{{"SetX", "x", nullptr, 0, badFields.data(), 1}}};
    const BindingSet bad{"Bad", commands.data(), commands.size(), nullptr, 0, nullptr, 0};
    EXPECT_NE(checkBindings(bad).find("'repeat' is not a name"), std::string::npos) << checkBindings(bad);
}

TEST(BindingCheck, TheOtherMistakesOfATableAreFoundToo)
{
    static constexpr std::array<ValueSpec, 2> twice{
        {{"a", ValueKind::Number, 0.0, 1.0, 0.0, "x"}, {"a", ValueKind::Number, 0.0, 1.0, 0.0, "x"}}};
    static constexpr std::array<ValueSpec, 1> turned{{{"a", ValueKind::Number, 1.0, 0.0, 0.0, "x"}}};
    static constexpr std::array<ValueSpec, 1> outside{{{"a", ValueKind::Number, 0.0, 1.0, 5.0, "x"}}};
    static constexpr std::array<ValueSpec, 1> unknownEnum{{{"a", ValueKind::Integer, 0.0, 1.0, 0.0, "x", "Nope"}}};
    static constexpr std::array<CommandSpec, 4> commands{{{"A", "x", nullptr, 0, twice.data(), 2},
                                                          {"B", "x", nullptr, 0, turned.data(), 1},
                                                          {"C", "x", nullptr, 0, outside.data(), 1},
                                                          {"D", "x", nullptr, 0, unknownEnum.data(), 1}}};
    for (size_t i = 0; i < commands.size(); ++i)
    {
        const BindingSet one{"One", commands.data() + i, 1, nullptr, 0, nullptr, 0};
        EXPECT_FALSE(checkBindings(one).empty()) << i;
    }
    static constexpr std::array<CommandSpec, 2> same{
        {{"A", "x", nullptr, 0, nullptr, 0}, {"A", "x", nullptr, 0, nullptr, 0}}};
    EXPECT_NE(checkBindings(BindingSet{"Same", same.data(), 2, nullptr, 0, nullptr, 0}).find("used twice"),
              std::string::npos);
}

namespace
{

/// A sink that only answers getters: index 0 gives 42.5, index 1 gives -1.
class GetterSink : public CommandSink
{
  public:
    bool onCommand(const CommandCall& /*call*/, char* /*error*/, const size_t /*errorSize*/) noexcept override
    {
        return true;
    }

    [[nodiscard]] double value(const size_t getter) noexcept override
    {
        ++reads;
        return getter == 0 ? 42.5 : -1.0;
    }

    int reads{0};
};

constexpr std::array<GetterSpec, 2> c_getters{
    {{"GetA", "The first value.", 0.0, 100.0}, {"GetB", "The second.", -1.0, 1.0}}};

const BindingSet& getterBindings()
{
    static const BindingSet set{"Getters", nullptr, 0, nullptr, 0, nullptr, 0, nullptr, c_getters.data(), 2};
    return set;
}

}

TEST(Getter, AScriptReadsTheValueThePluginKeepsAtEveryCall)
{
    GetterSink sink;
    Runtime runtime(getterBindings(), sink, nullptr, Limits{}, 1);
    ASSERT_EQ(runtime.load("test", "assert(GetA() == 42.5)\nassert(GetB() == -1)\nlocal x = GetA() + GetB()"),
              Status::Ok)
        << runtime.error();
    EXPECT_EQ(sink.reads, 4);
}

TEST(Getter, TheApiDescriptionListsGettersWithTheirRange)
{
    const std::string text = apiMarkdown(getterBindings(), Limits{});
    EXPECT_NE(text.find("### GetA()"), std::string::npos) << text;
    EXPECT_NE(text.find("The first value. Range 0 to 100."), std::string::npos) << text;
}

TEST(Getter, ANameThatCollidesIsFound)
{
    static constexpr std::array<GetterSpec, 2> twice{{{"GetA", "x", 0.0, 1.0}, {"GetA", "x", 0.0, 1.0}}};
    EXPECT_EQ(checkBindings(getterBindings()), "");
    EXPECT_NE(checkBindings(BindingSet{"Twice", nullptr, 0, nullptr, 0, nullptr, 0, nullptr, twice.data(), 2})
                  .find("used twice"),
              std::string::npos);
}

}
