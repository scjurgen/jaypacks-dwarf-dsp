// The sandboxed Lua runtime: what a script may not do, how it is stopped, that it behaves the same in every run.

#include <chrono>
#include <gtest/gtest.h>
#include <map>
#include <memory>
#include <string>

#include "ToyBindings.h"
#include "scripting/LuaRuntime.h"

namespace jp::script::test
{

namespace
{

class MapModules : public ModuleSource
{
  public:
    bool find(const char* name, std::string& source) noexcept override
    {
        const auto found = modules.find(name);
        if (found == modules.end())
        {
            return false;
        }
        source = found->second;
        return true;
    }

    std::map<std::string, std::string> modules;
};

struct Rig
{
    explicit Rig(const Limits& limits = Limits{}, const uint32_t seed = 1)
        : runtime(toyBindings(), sink, &modules, limits, seed)
    {
    }

    Status load(const std::string& text)
    {
        return runtime.load("test", text);
    }

    RecordingSink sink;
    MapModules modules;
    Runtime runtime;
};

double milliseconds(const std::chrono::steady_clock::time_point since)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - since).count();
}

}

TEST(LuaRuntime, ALoadedScriptsHooksAreCalledWithTheirArguments)
{
    Rig rig;
    ASSERT_EQ(rig.load("function OnNoteOn(ch, note, vel) SetPair(note, vel) end"), Status::Ok);
    const std::array<double, 3> arguments{1, 60, 100};
    EXPECT_EQ(rig.runtime.call("OnNoteOn", arguments.data(), 3), Status::Ok);
    ASSERT_EQ(rig.sink.calls.size(), 1u);
    EXPECT_EQ(rig.sink.calls[0].command, 2u);
    EXPECT_EQ(rig.sink.calls[0].number[0], 60.0);
    EXPECT_EQ(rig.sink.calls[0].number[1], 100.0);
}

TEST(LuaRuntime, AHookTheScriptDoesNotDefineIsFine)
{
    Rig rig;
    ASSERT_EQ(rig.load("x = 1"), Status::Ok);
    EXPECT_EQ(rig.runtime.call("OnStart"), Status::Ok);
    EXPECT_FALSE(rig.runtime.hasFunction("OnStart"));
}

TEST(LuaRuntime, WholeHookArgumentsArriveAsIntegersAndOthersAsFloats)
{
    Rig rig;
    ASSERT_EQ(rig.load("function OnNoteOn(a, b, c) Log(math.type(a), math.type(b), math.type(c)) end"), Status::Ok);
    const std::array<double, 3> arguments{60, 0.5, -3};
    ASSERT_EQ(rig.runtime.call("OnNoteOn", arguments.data(), 3), Status::Ok);
    ASSERT_EQ(rig.runtime.log().size(), 1u);
    EXPECT_EQ(rig.runtime.log()[0], "integer float integer");
}

TEST(LuaRuntime, TheSandboxHasNoWayOutOfTheScript)
{
    Rig rig;
    const char* script = R"(
        for _, name in ipairs({"os", "io", "package", "debug", "require", "dofile", "loadfile", "load",
                               "collectgarbage", "warn", "coroutine", "utf8", "loadstring", "module"}) do
            assert(_G[name] == nil, name .. " is reachable")
        end
        assert(string.dump == nil)
        assert(math and table and string and select and pcall and setmetatable)
        assert(getmetatable("").__index == string)
    )";
    EXPECT_EQ(rig.load(script), Status::Ok) << rig.runtime.error();
}

TEST(LuaRuntime, PrecompiledChunksAreNeverLoaded)
{
    Rig rig;
    const std::string bytecode = std::string("\x1b") + "Lua\x54\x00garbage";
    EXPECT_EQ(rig.load(bytecode), Status::CompileError);
    EXPECT_NE(rig.runtime.error().find("binary"), std::string::npos) << rig.runtime.error();
}

TEST(LuaRuntime, PrintAndLogWriteLines)
{
    Rig rig;
    ASSERT_EQ(rig.load("print('a', 1, true, nil) Log('b', {} ~= nil)"), Status::Ok);
    ASSERT_EQ(rig.runtime.log().size(), 2u);
    EXPECT_EQ(rig.runtime.log()[0], "a 1 true nil");
    EXPECT_EQ(rig.runtime.log()[1], "b true");
}

TEST(LuaRuntime, LogLinesAreShortAndTheLogKeepsTheLatest)
{
    Rig rig;
    ASSERT_EQ(rig.load("Log(string.rep('x', 1000)) for i = 1, 100 do Log('line', i) end"), Status::Ok);
    ASSERT_EQ(rig.runtime.log().size(), Runtime::c_logLines);
    EXPECT_EQ(rig.runtime.log().back(), "line 100");
    EXPECT_EQ(rig.runtime.log().front(), "line 37");
    rig.runtime.clearLog();
    ASSERT_EQ(rig.load("Log(string.rep('x', 1000))"), Status::Ok);
    EXPECT_EQ(rig.runtime.log()[0].size(), Runtime::c_logLineLength);
}

TEST(LuaRuntime, RandomNumbersAndTableOrderAreTheSameInEveryRunForASeed)
{
    const char* script = R"(
        local t = {}
        for i = 1, 40 do t["key" .. i] = i end
        local order = {}
        for k in pairs(t) do order[#order + 1] = k end
        Log(table.concat(order, ","))
        local r = {}
        for i = 1, 6 do r[i] = math.random(1, 1000000) end
        Log(table.concat(r, ","))
    )";
    Rig first(Limits{}, 7);
    Rig second(Limits{}, 7);
    Rig other(Limits{}, 8);
    ASSERT_EQ(first.load(script), Status::Ok);
    ASSERT_EQ(second.load(script), Status::Ok);
    ASSERT_EQ(other.load(script), Status::Ok);
    EXPECT_EQ(first.runtime.log(), second.runtime.log());
    EXPECT_EQ(first.runtime.log()[0], other.runtime.log()[0]) << "the table order does not depend on the seed";
    EXPECT_NE(first.runtime.log()[1], other.runtime.log()[1]);
}

TEST(LuaRuntime, AnEndlessLoopIsStoppedAtTheInstructionBudget)
{
    Limits limits;
    limits.loadInstructions = 50000;
    Rig rig(limits);
    const auto start = std::chrono::steady_clock::now();
    EXPECT_EQ(rig.load("while true do end"), Status::BudgetExceeded);
    EXPECT_LT(milliseconds(start), 1000.0);
    EXPECT_NE(rig.runtime.error().find("instructions"), std::string::npos) << rig.runtime.error();
    EXPECT_GE(rig.runtime.lastInstructions(), 50000u);
    EXPECT_LE(rig.runtime.lastInstructions(), 52000u);
}

TEST(LuaRuntime, PcallInALoopCannotSwallowTheBudget)
{
    Limits limits;
    limits.loadInstructions = 50000;
    Rig rig(limits);
    const auto start = std::chrono::steady_clock::now();
    EXPECT_EQ(rig.load("while true do pcall(function() while true do end end) end"), Status::BudgetExceeded);
    EXPECT_LT(milliseconds(start), 1000.0);
    // the runtime is usable afterwards
    EXPECT_EQ(rig.load("x = 1"), Status::Ok);
}

TEST(LuaRuntime, ATailCallLoopAndADeepRecursionEndWithErrors)
{
    Limits limits;
    limits.loadInstructions = 50000;
    Rig rig(limits);
    EXPECT_EQ(rig.load("local function f() return f() end f()"), Status::BudgetExceeded);
    // the stack of Lua calls grows until the memory limit (1 MB) or the stack limit ends it
    const Status recursion = rig.load("local function f() return 1 + f() end f()");
    EXPECT_TRUE(recursion == Status::OutOfMemory || recursion == Status::RuntimeError);
    EXPECT_NE(rig.runtime.error().find("memory"), std::string::npos) << rig.runtime.error();
}

TEST(LuaRuntime, TheWallClockLimitStopsASlowScript)
{
    Limits limits;
    limits.loadInstructions = 0xFFFFFFFFu;
    limits.loadMillis = 5.0;
    Rig rig(limits);
    const auto start = std::chrono::steady_clock::now();
    EXPECT_EQ(rig.load("while true do end"), Status::BudgetExceeded);
    EXPECT_LT(milliseconds(start), 500.0);
    EXPECT_NE(rig.runtime.error().find("ms"), std::string::npos) << rig.runtime.error();
}

TEST(LuaRuntime, HooksHaveTheirOwnSmallerBudget)
{
    Limits limits;
    limits.handlerInstructions = 20000;
    Rig rig(limits);
    ASSERT_EQ(rig.load("function OnStart() for i = 1, 1e9 do end end function Quick() local x = 1 end"), Status::Ok);
    EXPECT_EQ(rig.runtime.call("OnStart"), Status::BudgetExceeded);
    EXPECT_EQ(rig.runtime.call("Quick"), Status::Ok) << "the budget is per call";
}

TEST(LuaRuntime, MemoryBombsAreStoppedAtTheLimit)
{
    Limits limits;
    limits.memoryBytes = 256 * 1024;
    Rig rig(limits);
    EXPECT_EQ(rig.load("local s = string.rep('x', 1024 * 1024 * 8)"), Status::OutOfMemory);
    EXPECT_EQ(rig.load("local t = {} while true do t[#t + 1] = {1, 2, 3} end"), Status::OutOfMemory);
    EXPECT_LE(rig.runtime.memoryPeak(), limits.memoryBytes);
    EXPECT_EQ(rig.load("local s = ('x'):rep(100) Log(#s)"), Status::Ok) << "the memory was freed";
}

TEST(LuaRuntime, AMemoryErrorCaughtByPcallInALoopStillEnds)
{
    Limits limits;
    limits.memoryBytes = 256 * 1024;
    limits.loadInstructions = 200000;
    Rig rig(limits);
    EXPECT_NE(rig.load("while true do pcall(function() local t = {} while true do t[#t + 1] = 1 end end) end"),
              Status::Ok);
}

TEST(LuaRuntime, FinalizersAreRefusedBecauseNoHookCouldStopThem)
{
    Rig rig;
    EXPECT_EQ(rig.load("setmetatable({}, {__gc = function() while true do end end})"), Status::RuntimeError);
    EXPECT_NE(rig.runtime.error().find("__gc"), std::string::npos) << rig.runtime.error();
    EXPECT_EQ(rig.load("local mt = {} rawset(mt, '__gc', function() end) setmetatable({}, mt)"), Status::RuntimeError);
    // ordinary metatables and the return value of setmetatable work as before
    EXPECT_EQ(
        rig.load("local t = setmetatable({}, {__index = function(_, k) return k .. '!' end}) assert(t.x == 'x!')\n"
                 "assert(setmetatable({}, nil) ~= nil)\n"
                 "local p = setmetatable({}, {__metatable = 'locked'}) assert(getmetatable(p) == 'locked')\n"
                 "assert(not pcall(setmetatable, p, {}))"),
        Status::Ok)
        << rig.runtime.error();
    for (int i = 0; i < 50; ++i)
    {
        rig.runtime.collectGarbage();
    }
}

TEST(LuaRuntime, StringPatternsAreBoundedBecauseTheirCostIsOutOfReachOfTheInstructionCount)
{
    Rig rig;
    EXPECT_EQ(rig.load("assert(('hello world'):find('o w') == 5)\n"
                       "assert(('key=value'):match('(%w+)=(%w+)') == 'key')\n"
                       "local n = 0 for _ in ('a,b,c'):gmatch('[^,]+') do n = n + 1 end assert(n == 3)\n"
                       "assert(('abc'):gsub('b', 'X') == 'aXc')\n"
                       "assert(string.find(string.rep('x', 8192), 'x', 1, true) == 1)"),
              Status::Ok)
        << rig.runtime.error();
    for (const char* call :
         {"string.find(string.rep('x', 8193), 'x')", "string.match(string.rep('x', 9000), 'y')",
          "string.gmatch(string.rep('x', 9000), 'x')", "string.gsub(string.rep('x', 9000), 'x', 'y')",
          "string.find('x', string.rep('x', 101))", "('x'):find(string.rep('.-', 51))"})
    {
        EXPECT_EQ(rig.load(call), Status::RuntimeError) << call;
        EXPECT_NE(rig.runtime.error().find("string patterns work on"), std::string::npos) << rig.runtime.error();
    }
    EXPECT_EQ(rig.load("string.find(123, 4)"), Status::Ok) << "numbers are converted as before";
    EXPECT_EQ(rig.load("string.find({}, 'x')"), Status::RuntimeError);
}

TEST(LuaRuntime, ErrorsNameTheScriptAndTheLine)
{
    Rig rig;
    EXPECT_EQ(rig.runtime.load("patch", "x = 1\ny = = 2\n"), Status::CompileError);
    EXPECT_EQ(rig.runtime.error().rfind("patch:2:", 0), 0u) << rig.runtime.error();
    EXPECT_EQ(rig.runtime.load("patch", "x = 1\nerror('boom')\n"), Status::RuntimeError);
    EXPECT_EQ(rig.runtime.error().rfind("patch:2: boom", 0), 0u) << rig.runtime.error();
    EXPECT_EQ(rig.runtime.load("patch", "error({})"), Status::RuntimeError);
    EXPECT_FALSE(rig.runtime.error().empty());
}

TEST(LuaRuntime, AFailedCallLeavesTheStateUsable)
{
    Rig rig;
    ASSERT_EQ(rig.load("function Bad() error('no') end function Good() Log('fine') end"), Status::Ok);
    EXPECT_EQ(rig.runtime.call("Bad"), Status::RuntimeError);
    EXPECT_EQ(rig.runtime.call("Good"), Status::Ok);
    EXPECT_TRUE(rig.runtime.error().empty());
    EXPECT_EQ(rig.runtime.log().back(), "fine");
}

TEST(LuaRuntime, ConstantsAreGlobalTablesAndImportingThemIsAccepted)
{
    Rig rig;
    EXPECT_EQ(rig.load("import 'constants' assert(Wave.Saw == 1 and Wave.Square == 2)"), Status::Ok)
        << rig.runtime.error();
}

TEST(LuaRuntime, ClampAndLerpAreThere)
{
    Rig rig;
    EXPECT_EQ(rig.load("assert(clamp(5, 0, 1) == 1 and clamp(-5, 0, 1) == 0 and clamp(0.5, 0, 1) == 0.5)\n"
                       "assert(lerp(0, 10, 0.25) == 2.5)"),
              Status::Ok)
        << rig.runtime.error();
}

TEST(LuaRuntime, ImportRunsAModuleOnceAndFindsItWhereTheHostSays)
{
    Rig rig;
    rig.modules.modules["lib"] = "count = (count or 0) + 1";
    EXPECT_EQ(rig.load("import 'lib' import 'lib' Log(count)"), Status::Ok) << rig.runtime.error();
    EXPECT_EQ(rig.runtime.log().back(), "1");
}

TEST(LuaRuntime, ImportCyclesEndAndErrorsInAModuleNameIt)
{
    Rig rig;
    rig.modules.modules["a"] = "import 'b' a_done = true";
    rig.modules.modules["b"] = "import 'a' b_done = true";
    EXPECT_EQ(rig.load("import 'a' assert(a_done and b_done)"), Status::Ok) << rig.runtime.error();
    rig.modules.modules["broken"] = "x = = 1";
    EXPECT_EQ(rig.load("import 'broken'"), Status::RuntimeError);
    EXPECT_EQ(rig.runtime.error().rfind("broken:1:", 0), 0u) << rig.runtime.error();
}

TEST(LuaRuntime, ImportTakesNamesNotPaths)
{
    Rig rig;
    rig.modules.modules["ok"] = "fine = true";
    for (const char* name : {"../secret", "a/b", "a.b", "", "with space", "/etc/passwd", "~"})
    {
        EXPECT_EQ(rig.load(std::string("import('") + name + "')"), Status::RuntimeError) << name;
        EXPECT_NE(rig.runtime.error().find("not a module name"), std::string::npos)
            << name << ": " << rig.runtime.error();
    }
    EXPECT_EQ(rig.load("import(string.rep('a', 41))"), Status::RuntimeError);
    EXPECT_EQ(rig.load("import 'nowhere'"), Status::RuntimeError);
    EXPECT_NE(rig.runtime.error().find("no module"), std::string::npos) << rig.runtime.error();
    EXPECT_EQ(rig.load("import 'ok' assert(fine)"), Status::Ok);
}

TEST(LuaRuntime, TheMemoryOfAScriptIsReportedAndFreedWithTheState)
{
    Rig rig;
    EXPECT_GT(rig.runtime.memoryUsed(), 0u);
    ASSERT_EQ(rig.load("t = {} for i = 1, 1000 do t[i] = {i} end"), Status::Ok);
    EXPECT_GT(rig.runtime.memoryPeak(), 20000u);
    ASSERT_EQ(rig.load("t = nil"), Status::Ok);
    for (int i = 0; i < 20; ++i)
    {
        rig.runtime.collectGarbage();
    }
    EXPECT_LT(rig.runtime.memoryUsed(), rig.runtime.memoryPeak());
}

TEST(LuaRuntime, ATooSmallMemoryLimitMakesAnInvalidRuntimeNotACrash)
{
    Limits limits;
    limits.memoryBytes = 2000;
    RecordingSink sink;
    Runtime runtime(toyBindings(), sink, nullptr, limits, 1);
    EXPECT_FALSE(runtime.valid());
    EXPECT_EQ(runtime.load("test", "x = 1"), Status::OutOfMemory);
    EXPECT_EQ(runtime.call("OnStart"), Status::OutOfMemory);
    runtime.collectGarbage();
}

}
