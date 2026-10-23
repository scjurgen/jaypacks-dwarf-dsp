// The script host: loading, hot reload, the way events and commands cross between the audio and the worker thread.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "ToyBindings.h"
#include "lv2/PedalServices.h"
#include "scripting/ScriptHost.h"

namespace jp::script::test
{

namespace fs = std::filesystem;

namespace
{

using Host = ScriptHost<ToyEvent, ToyCommand>;

/// The host's worker as the plugin sees it: schedule_work counts a job, the test runs the host's work() for it.
class WorkerStub
{
  public:
    WorkerStub()
    {
        schedule.handle = this;
        schedule.schedule_work = &WorkerStub::scheduleWork;
    }

    static LV2_Worker_Status scheduleWork(LV2_Worker_Schedule_Handle handle, uint32_t /*size*/, const void* /*data*/)
    {
        auto* self = static_cast<WorkerStub*>(handle);
        if (self->refuse)
        {
            ++self->refused;
            return LV2_WORKER_ERR_NO_SPACE;
        }
        {
            std::lock_guard<std::mutex> lock(self->mutex);
            ++self->pending;
            ++self->scheduled;
        }
        self->wake.notify_one();
        return LV2_WORKER_SUCCESS;
    }

    bool take()
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (pending == 0)
        {
            return false;
        }
        --pending;
        return true;
    }

    LV2_Worker_Schedule schedule{};
    std::mutex mutex;
    std::condition_variable wake;
    int pending{0};
    int scheduled{0};
    std::atomic<bool> refuse{false};
    std::atomic<int> refused{0};
};

/// A scratch folder below the working directory of the test run (never /tmp).
class ScratchDir
{
  public:
    explicit ScratchDir(const std::string& name)
        : m_path(fs::current_path() / "scripting-test" / name)
    {
        fs::remove_all(m_path);
        fs::create_directories(m_path / "factory");
        fs::create_directories(m_path / "user");
    }

    ~ScratchDir()
    {
        fs::remove_all(m_path);
    }

    ScratchDir(const ScratchDir&) = delete;
    ScratchDir& operator=(const ScratchDir&) = delete;

    [[nodiscard]] std::string factory() const
    {
        return (m_path / "factory").string();
    }

    [[nodiscard]] std::string user() const
    {
        return (m_path / "user").string();
    }

    /// Writes a file and gives it a modification time of its own, so a rewrite is always seen as a change.
    void write(const std::string& relative, const std::string& text)
    {
        const fs::path path = m_path / relative;
        {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out << text;
        }
        fs::last_write_time(path, fs::file_time_type::clock::now() + std::chrono::seconds(++m_edits));
    }

    [[nodiscard]] std::string read(const std::string& relative) const
    {
        std::ifstream in(m_path / relative);
        std::stringstream text;
        text << in.rdbuf();
        return text.str();
    }

  private:
    fs::path m_path;
    int m_edits{0};
};

struct Popped
{
    ToyCommand command;
    uint32_t generation;
};

class HostRig
{
  public:
    explicit HostRig(const std::string& name)
        : dir(name)
        , host(binder)
    {
        HostConfig config;
        config.factoryDir = dir.factory();
        config.userDir = dir.user();
        config.pollSeconds = 0.5;
        config.sampleRate = 48000.0;
        config.timerCommandBase = c_timerCommandBase;
        host.configure(config);
        scheduler.setSchedule(&worker.schedule);
        host.setWorkScheduler(scheduler);
    }

    void pump()
    {
        while (worker.take())
        {
            host.work();
        }
    }

    /// A block's worth of time that is long enough to be the poll interval.
    void poll()
    {
        host.advance(24000);
        pump();
    }

    std::vector<Popped> popAll()
    {
        std::vector<Popped> all;
        Popped item{};
        while (host.popCommand(item.command, item.generation))
        {
            all.push_back(item);
        }
        return all;
    }

    void select(const std::string& name, const Origin origin = Origin::Factory)
    {
        ASSERT_TRUE(host.select(origin, name.c_str()));
        pump();
    }

    static ToyEvent noteOn(const uint8_t note, const uint8_t velocity)
    {
        ToyEvent event;
        event.kind = 1;
        event.channel = 1;
        event.note = note;
        event.velocity = velocity;
        return event;
    }

    ScratchDir dir;
    ToyBinder binder;
    Host host;
    WorkerStub worker;
    WorkScheduler scheduler;
};

constexpr const char* c_voiceScript =
    "function OnStart() SetGain(-3) end\n"
    "function OnNoteOn(ch, note, vel) SetVoice(0, {level = vel / 127, detune = note}) end\n";

}

TEST(ScriptHost, ALoadedScriptQueuesTheCommandsOfItsTopLevelAndItsStartAsOneGeneration)
{
    HostRig rig("loads");
    rig.dir.write("factory/a.lua", "SetPair(1, 2)\nfunction OnStart() SetGain(-3) end\n");
    EXPECT_EQ(rig.host.state(), ScriptState::Idle);
    EXPECT_EQ(rig.host.generation(), 0u);
    rig.select("a");
    EXPECT_EQ(rig.host.state(), ScriptState::Ok);
    EXPECT_EQ(rig.host.generation(), 1u);
    const auto commands = rig.popAll();
    ASSERT_EQ(commands.size(), 2u);
    EXPECT_EQ(commands[0].command.kind, ToyCommandKind::Pair);
    EXPECT_EQ(commands[1].command.kind, ToyCommandKind::Gain);
    EXPECT_FLOAT_EQ(commands[1].command.a, -3.0f);
    EXPECT_EQ(commands[0].generation, 1u);
    EXPECT_GT(rig.host.loadMillis(), 0.0f);
    EXPECT_GT(rig.host.memoryPeak(), 1000u);
}

TEST(ScriptHost, EventsReachTheHooksAndTheirCommandsComeBack)
{
    HostRig rig("events");
    rig.dir.write("factory/a.lua", c_voiceScript);
    rig.select("a");
    rig.popAll();
    ASSERT_TRUE(rig.host.pushEvent(HostRig::noteOn(60, 127)));
    ASSERT_TRUE(rig.host.pushEvent(HostRig::noteOn(61, 0)));
    EXPECT_EQ(rig.worker.scheduled, 2)
        << "one wake-up for the select, one for both events (the second push found one pending)";
    rig.pump();
    const auto commands = rig.popAll();
    ASSERT_EQ(commands.size(), 2u);
    EXPECT_FLOAT_EQ(commands[0].command.a, 1.0f);
    EXPECT_FLOAT_EQ(commands[0].command.b, 12.0f) << "detune is clamped to 12";
    EXPECT_FLOAT_EQ(commands[1].command.a, 0.0f);
}

TEST(ScriptHost, OnlyOneWakeUpJobIsOutstandingAtATime)
{
    HostRig rig("wake");
    rig.dir.write("factory/a.lua", c_voiceScript);
    rig.select("a");
    const int before = rig.worker.scheduled;
    for (int i = 0; i < 50; ++i)
    {
        rig.host.pushEvent(HostRig::noteOn(60, 100));
    }
    EXPECT_EQ(rig.worker.scheduled, before + 1);
    rig.pump();
    rig.host.pushEvent(HostRig::noteOn(60, 100));
    EXPECT_EQ(rig.worker.scheduled, before + 2);
}

TEST(ScriptHost, ANameThatIsNoFileNameIsRefused)
{
    HostRig rig("names");
    for (const char* name : {"../x", "a/b", "a.b", "", "with space", "/abs"})
    {
        EXPECT_FALSE(rig.host.select(Origin::Factory, name)) << name;
    }
    EXPECT_FALSE(rig.host.select(Origin::User, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
    EXPECT_EQ(rig.worker.scheduled, 0);
}

TEST(ScriptHost, ABrokenFileLeavesThePreviousScriptRunningAndSaysWhy)
{
    HostRig rig("broken");
    rig.dir.write("factory/a.lua", c_voiceScript);
    rig.select("a");
    rig.popAll();
    rig.dir.write("factory/a.lua", "function OnStart() SetGain( end\n");
    rig.poll();
    EXPECT_EQ(rig.host.state(), ScriptState::CompileError);
    EXPECT_EQ(rig.host.generation(), 1u);
    EXPECT_TRUE(rig.popAll().empty());
    const std::string log = rig.dir.read("user/a.log");
    EXPECT_NE(log.find("state: compile error"), std::string::npos) << log;
    EXPECT_NE(log.find("not loaded, the previous script stays: a:1:"), std::string::npos) << log;
    // the old script still answers
    rig.host.pushEvent(HostRig::noteOn(60, 127));
    rig.pump();
    EXPECT_EQ(rig.popAll().size(), 1u);
    // and the broken file is not tried again until it changes
    rig.poll();
    rig.poll();
    EXPECT_EQ(rig.host.state(), ScriptState::CompileError);
    rig.dir.write("factory/a.lua", "function OnStart() SetGain(5) end\n");
    rig.poll();
    EXPECT_EQ(rig.host.state(), ScriptState::Ok);
    EXPECT_EQ(rig.host.generation(), 2u);
}

TEST(ScriptHost, AnErrorInOnStartRejectsTheWholeScriptAndNoneOfItsCommandsArrive)
{
    HostRig rig("onstart");
    rig.dir.write("factory/a.lua", c_voiceScript);
    rig.select("a");
    rig.popAll();
    rig.dir.write("factory/a.lua", "function OnStart() SetGain(1) error('late') end\n");
    rig.poll();
    EXPECT_EQ(rig.host.state(), ScriptState::RuntimeError);
    EXPECT_EQ(rig.host.generation(), 1u);
    EXPECT_TRUE(rig.popAll().empty()) << "the commands called before the error were held back";
}

TEST(ScriptHost, AChangedFileIsReloadedAtTheNextCheckAndOlderCommandsAreDropped)
{
    HostRig rig("reload");
    rig.dir.write("factory/a.lua", c_voiceScript);
    rig.select("a");
    rig.popAll();
    rig.host.pushEvent(HostRig::noteOn(60, 127));
    rig.pump(); // a command of generation 1 waits in the queue
    rig.dir.write("factory/a.lua", "function OnStart() SetGain(-9) end\nfunction OnNoteOn() SetPair(7, 8) end\n");
    rig.poll();
    EXPECT_EQ(rig.host.generation(), 2u);
    auto commands = rig.popAll();
    ASSERT_EQ(commands.size(), 1u) << "the command of the old script is skipped";
    EXPECT_EQ(commands[0].generation, 2u);
    EXPECT_FLOAT_EQ(commands[0].command.a, -9.0f);
    rig.host.pushEvent(HostRig::noteOn(60, 127));
    rig.pump();
    commands = rig.popAll();
    ASSERT_EQ(commands.size(), 1u);
    EXPECT_EQ(commands[0].command.kind, ToyCommandKind::Pair);
}

TEST(ScriptHost, AnUnchangedFileIsNotReloaded)
{
    HostRig rig("unchanged");
    rig.dir.write("factory/a.lua", c_voiceScript);
    rig.select("a");
    for (int i = 0; i < 5; ++i)
    {
        rig.poll();
    }
    EXPECT_EQ(rig.host.generation(), 1u);
}

TEST(ScriptHost, SelectingTheLoadedScriptAgainLoadsItAgain)
{
    HostRig rig("reselect");
    rig.dir.write("factory/a.lua", c_voiceScript);
    rig.select("a");
    rig.select("a");
    EXPECT_EQ(rig.host.generation(), 2u);
}

TEST(ScriptHost, AMissingFileIsReportedAndLoadedWhenItAppears)
{
    HostRig rig("missing");
    rig.select("later", Origin::User);
    EXPECT_EQ(rig.host.state(), ScriptState::FileMissing);
    EXPECT_EQ(rig.host.generation(), 0u);
    rig.poll();
    EXPECT_EQ(rig.host.state(), ScriptState::FileMissing);
    rig.dir.write("user/later.lua", "function OnStart() SetGain(2) end");
    rig.poll();
    EXPECT_EQ(rig.host.state(), ScriptState::Ok);
    EXPECT_EQ(rig.host.generation(), 1u);
    EXPECT_NE(rig.dir.read("user/later.log").find("state: ok"), std::string::npos);
}

TEST(ScriptHost, TheLogFileHasTheOutcomeTheNumbersAndTheScriptsOwnLines)
{
    HostRig rig("logfile");
    rig.dir.write("user/mine.lua", "Log('hello from the script')\nSetVoice(9)\n");
    rig.select("mine", Origin::User);
    const std::string log = rig.dir.read("user/mine.log");
    for (const char* fragment : {"script: user/mine.lua", "state: ok", "message: loaded", "generation: 1",
                                 "load: ", "memory peak: ", "--- script log", "hello from the script",
                                 "'index' = 9 is outside 0..2, call ignored"})
    {
        EXPECT_NE(log.find(fragment), std::string::npos) << fragment << "\n" << log;
    }
}

TEST(ScriptHost, AFailingHookIsReportedAndTheNextEventsStillRun)
{
    HostRig rig("hookerror");
    rig.dir.write("factory/a.lua",
                  "function OnNoteOn(ch, note, vel) if vel == 0 then error('silent note') end SetGain(vel) end\n");
    rig.select("a");
    rig.popAll();
    rig.host.pushEvent(HostRig::noteOn(60, 0));
    rig.host.pushEvent(HostRig::noteOn(61, 5));
    rig.pump();
    EXPECT_EQ(rig.host.state(), ScriptState::RuntimeError);
    EXPECT_EQ(rig.host.hookFailures(), 1u);
    const auto commands = rig.popAll();
    ASSERT_EQ(commands.size(), 1u);
    EXPECT_FLOAT_EQ(commands[0].command.a, 5.0f);
    EXPECT_NE(rig.dir.read("user/a.log").find("a hook failed: a:1: silent note"), std::string::npos);
}

TEST(ScriptHost, OnlyAFailedHookCountsAsAFailureNotAFileThatDoesNotLoad)
{
    HostRig rig("failures");
    rig.dir.write("factory/a.lua", "function OnNoteOn(ch, note, vel) SetGain(vel) end\n");
    rig.select("a");
    rig.popAll();
    rig.dir.write("factory/a.lua", "function OnNoteOn( oops\n");
    rig.poll();
    EXPECT_EQ(rig.host.state(), ScriptState::CompileError);
    EXPECT_EQ(rig.host.hookFailures(), 0u) << "the old script is still the one that runs";
    rig.host.pushEvent(HostRig::noteOn(60, 7));
    rig.pump();
    ASSERT_EQ(rig.popAll().size(), 1u);
}

TEST(ScriptHost, AHookThatLoopsForeverIsStoppedAndTheHostGoesOn)
{
    HostRig rig("hookloop");
    rig.dir.write("factory/a.lua",
                  "function OnNoteOn(ch, note, vel) if vel == 1 then while true do end end SetGain(vel) end\n");
    rig.select("a");
    rig.popAll();
    rig.host.pushEvent(HostRig::noteOn(60, 1));
    rig.host.pushEvent(HostRig::noteOn(60, 9));
    const auto start = std::chrono::steady_clock::now();
    rig.pump();
    EXPECT_LT(std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(), 2.0);
    EXPECT_EQ(rig.host.state(), ScriptState::BudgetExceeded);
    const auto commands = rig.popAll();
    ASSERT_EQ(commands.size(), 1u);
    EXPECT_FLOAT_EQ(commands[0].command.a, 9.0f);
}

TEST(ScriptHost, AScriptThatNeverEndsAtTheTopLevelIsRejected)
{
    HostRig rig("toplevelloop");
    rig.dir.write("factory/a.lua", "while true do end\n");
    rig.select("a");
    EXPECT_EQ(rig.host.state(), ScriptState::BudgetExceeded);
    EXPECT_EQ(rig.host.generation(), 0u);
}

TEST(ScriptHost, AFloodOfCommandsFillsTheQueueAndIsCountedNotWaitedFor)
{
    HostRig rig("flood");
    rig.dir.write("factory/a.lua", "function OnNoteOn() for i = 1, 100 do SetGain(i % 10) end end\n");
    rig.select("a");
    for (int i = 0; i < 20; ++i)
    {
        rig.host.pushEvent(HostRig::noteOn(60, 100));
    }
    rig.pump();
    EXPECT_GT(rig.host.droppedCommands(), 0u);
    EXPECT_NE(rig.dir.read("user/a.log").find("the command queue is full"), std::string::npos)
        << "the script log says so (written at the next load or failure)";
    size_t count = 0;
    for (const auto& item : rig.popAll())
    {
        (void) item;
        ++count;
    }
    EXPECT_EQ(count, 1023u);
}

TEST(ScriptHost, AFullEventQueueDropsAndNeverBlocks)
{
    HostRig rig("eventflood");
    rig.dir.write("factory/a.lua", c_voiceScript);
    rig.select("a");
    int refused = 0;
    for (int i = 0; i < 1000; ++i)
    {
        refused += rig.host.pushEvent(HostRig::noteOn(60, 100)) ? 0 : 1;
    }
    EXPECT_EQ(refused, 1000 - 255);
    EXPECT_EQ(rig.host.droppedEvents(), static_cast<uint32_t>(refused));
}

TEST(ScriptHost, ARefusedWakeUpIsTriedAgainAtTheNextBlock)
{
    HostRig rig("refused");
    rig.dir.write("factory/a.lua", c_voiceScript);
    rig.select("a");
    rig.popAll();
    rig.worker.refuse = true;
    rig.host.pushEvent(HostRig::noteOn(60, 100));
    EXPECT_EQ(rig.host.refusedWakeUps(), 1u);
    rig.host.advance(128);
    EXPECT_EQ(rig.host.refusedWakeUps(), 2u) << "tried again with the event still waiting";
    rig.worker.refuse = false;
    rig.host.advance(128);
    rig.pump();
    EXPECT_EQ(rig.popAll().size(), 1u);
}

TEST(ScriptHost, ModulesComeFromThePluginThenTheUserThenTheFactory)
{
    HostRig rig("modules");
    rig.dir.write("user/shared.lua", "origin = 'user'");
    rig.dir.write("factory/shared.lua", "origin = 'factory'");
    rig.dir.write("factory/onlyfactory.lua", "factory_only = true");
    rig.dir.write("factory/a.lua", "import 'embedded' import 'shared' import 'onlyfactory'\n"
                                   "assert(embedded_loaded and factory_only)\nLog(origin)\n");
    rig.select("a");
    EXPECT_EQ(rig.host.state(), ScriptState::Ok);
    EXPECT_NE(rig.dir.read("user/a.log").find("user"), std::string::npos);
}

TEST(ScriptHost, TheWorkerThreadAndTheAudioThreadShareOnlyTheQueues)
{
    HostRig rig("threads");
    rig.dir.write("factory/a.lua", "function OnNoteOn(ch, note, vel) SetPair(note, vel) end\n");
    rig.select("a");
    rig.popAll();
    std::atomic<bool> stop{false};
    std::thread workerThread(
        [&]
        {
            std::unique_lock<std::mutex> lock(rig.worker.mutex);
            while (!stop)
            {
                rig.worker.wake.wait_for(lock, std::chrono::milliseconds(2));
                while (rig.worker.pending > 0)
                {
                    --rig.worker.pending;
                    lock.unlock();
                    rig.host.work();
                    lock.lock();
                }
            }
        });
    uint32_t received = 0;
    uint32_t next = 0;
    for (int round = 0; round < 100; ++round)
    {
        for (int i = 0; i < 100; ++i)
        {
            ToyEvent event = HostRig::noteOn(static_cast<uint8_t>(next % 100), static_cast<uint8_t>(next % 90 + 1));
            ASSERT_TRUE(rig.host.pushEvent(event));
            ++next;
        }
        const auto start = std::chrono::steady_clock::now();
        ToyCommand command;
        uint32_t generation = 0;
        uint32_t wanted = received + 100;
        while (received < wanted && std::chrono::steady_clock::now() - start < std::chrono::seconds(5))
        {
            rig.host.advance(128);
            while (rig.host.popCommand(command, generation))
            {
                EXPECT_EQ(command.a, static_cast<float>(received % 100));
                EXPECT_EQ(command.b, static_cast<float>(received % 90 + 1));
                ++received;
            }
        }
        ASSERT_EQ(received, wanted) << "round " << round;
    }
    stop = true;
    workerThread.join();
    EXPECT_EQ(received, 10000u);
    EXPECT_EQ(rig.host.droppedEvents(), 0u);
}


TEST(ScriptHost, DeselectingDropsTheScriptAndSkipsItsQueuedCommands)
{
    HostRig rig("deselect");
    rig.dir.write("factory/a.lua", c_voiceScript);
    rig.select("a");
    rig.host.pushEvent(HostRig::noteOn(60, 100));
    rig.pump(); // a command of the script waits in the queue
    const uint32_t before = rig.host.generation();
    ASSERT_TRUE(rig.host.deselect());
    rig.pump();
    EXPECT_EQ(rig.host.state(), ScriptState::Idle);
    EXPECT_EQ(rig.host.generation(), before + 1);
    EXPECT_EQ(rig.host.readyGeneration(), before) << "ready names the last script that loaded";
    EXPECT_TRUE(rig.popAll().empty());
    EXPECT_FALSE(rig.host.wantsHook(1));
    rig.host.pushEvent(HostRig::noteOn(60, 100));
    rig.pump();
    EXPECT_TRUE(rig.popAll().empty()) << "no script, no answers";
    rig.poll();
    EXPECT_EQ(rig.host.state(), ScriptState::Idle) << "and no file check either";
}

TEST(ScriptHost, TheHostKnowsWhichHooksTheLoadedScriptDefines)
{
    HostRig rig("mask");
    rig.dir.write("factory/a.lua", "function OnNoteOn(ch, n, v) end\n");
    rig.select("a");
    EXPECT_FALSE(rig.host.wantsHook(0)) << "OnStart is not defined";
    EXPECT_TRUE(rig.host.wantsHook(1));
    EXPECT_FALSE(rig.host.wantsHook(2));
    EXPECT_FALSE(rig.host.wantsHook(99));
    rig.dir.write("factory/a.lua", "function OnStart() end\n");
    rig.poll();
    EXPECT_TRUE(rig.host.wantsHook(0));
    EXPECT_FALSE(rig.host.wantsHook(1));
}

TEST(ScriptHost, TheStartOfAScriptIsQueuedWholeBeforeItIsReadyAndPeekingTakesNothing)
{
    HostRig rig("ready");
    rig.dir.write("factory/a.lua", "SetPair(1, 2) SetPair(3, 4) SetGain(5)\nfunction OnStart() SetGain(6) end\n");
    EXPECT_EQ(rig.host.readyGeneration(), 0u);
    rig.select("a");
    EXPECT_EQ(rig.host.readyGeneration(), 1u);
    ToyCommand command;
    uint32_t generation = 0;
    ASSERT_TRUE(rig.host.peekCommand(command, generation));
    EXPECT_EQ(command.kind, ToyCommandKind::Pair);
    EXPECT_EQ(generation, 1u);
    ASSERT_TRUE(rig.host.peekCommand(command, generation)) << "peeking again gives the same command";
    EXPECT_FLOAT_EQ(command.a, 1.0f);
    rig.host.discardCommand();
    ASSERT_TRUE(rig.host.peekCommand(command, generation));
    EXPECT_FLOAT_EQ(command.a, 3.0f);
    rig.host.discardCommand();
    rig.host.discardCommand();
    ASSERT_TRUE(rig.host.peekCommand(command, generation));
    EXPECT_FLOAT_EQ(command.a, 6.0f) << "the command of OnStart comes after those of the top level";
    rig.host.discardCommand();
    EXPECT_FALSE(rig.host.peekCommand(command, generation));
}

TEST(ScriptHost, LoadingFromTwoThreadsAtOnceIsSerialised)
{
    HostRig rig("serial");
    rig.dir.write("factory/a.lua", c_voiceScript);
    rig.select("a");
    std::atomic<bool> stop{false};
    std::thread other(
        [&]
        {
            while (!stop)
            {
                rig.host.work();
            }
        });
    for (int i = 0; i < 200; ++i)
    {
        rig.host.select(Origin::Factory, "a"); // a reload each time
        rig.host.pushEvent(HostRig::noteOn(60, 100));
        rig.host.work();
        ToyCommand command;
        uint32_t generation = 0;
        while (rig.host.popCommand(command, generation))
        {
        }
    }
    stop = true;
    other.join();
    EXPECT_EQ(rig.host.state(), ScriptState::Ok);
    EXPECT_GE(rig.host.generation(), 100u);
}

namespace
{

struct Stamped
{
    ToyCommand command;
    uint64_t time;
};

std::vector<Stamped> popStamped(Host& host)
{
    std::vector<Stamped> all;
    Stamped item{};
    uint32_t generation = 0;
    while (host.peekCommand(item.command, generation, item.time))
    {
        host.discardCommand();
        all.push_back(item);
    }
    return all;
}

}

TEST(ScriptHost, TheCommandsOfAHookCarryTheSampleOfTheEventThatCalledIt)
{
    HostRig rig("stamps");
    rig.dir.write("factory/a.lua", "SetGain(1)\nfunction OnNoteOn(ch, note, vel) SetPair(note, vel) end\n");
    rig.select("a");
    ASSERT_TRUE(rig.host.pushEvent(HostRig::noteOn(60, 100), 123456));
    ASSERT_TRUE(rig.host.pushEvent(HostRig::noteOn(61, 90), 987654321012ull));
    rig.pump();
    const auto commands = popStamped(rig.host);
    ASSERT_EQ(commands.size(), 3u);
    EXPECT_EQ(commands[0].time, 0u) << "the commands of a load have no event";
    EXPECT_EQ(commands[1].time, 123456u);
    EXPECT_EQ(commands[2].time, 987654321012ull);
}

TEST(ScriptHost, TheEventsOfTheClockCallTheirHooksAndKeepTheOrderOfTimesWithTheEventsOfThePlugin)
{
    HostRig rig("core");
    rig.dir.write("factory/a.lua", "function OnTick(t) SetPair(1, t) end\n"
                                   "function OnTransportStart() SetPair(2, 0) end\n"
                                   "function OnTransportStop() SetPair(3, 0) end\n"
                                   "function OnTimer(id) SetPair(4, id) end\n"
                                   "function OnNoteOn(ch, note, vel) SetPair(5, note) end\n");
    rig.select("a");
    EXPECT_TRUE(rig.host.wantsCoreHook(CoreEvent::Kind::Tick));
    EXPECT_TRUE(rig.host.wantsCoreHook(CoreEvent::Kind::Timer));
    // each queue is in the order of its times (as each source makes it); the two are merged by time
    ASSERT_TRUE(rig.host.pushCoreEvent({CoreEvent::Kind::TransportStart, 0, 1000}));
    ASSERT_TRUE(rig.host.pushCoreEvent({CoreEvent::Kind::Timer, 5, 2000}));
    ASSERT_TRUE(rig.host.pushCoreEvent({CoreEvent::Kind::Tick, 7, 3000}));
    ASSERT_TRUE(rig.host.pushCoreEvent({CoreEvent::Kind::TransportStop, 0, 5000}));
    ASSERT_TRUE(rig.host.pushEvent(HostRig::noteOn(64, 100), 2500));
    ASSERT_TRUE(rig.host.pushEvent(HostRig::noteOn(60, 100), 4000));
    rig.pump();
    const auto commands = popStamped(rig.host);
    ASSERT_EQ(commands.size(), 6u);
    const std::vector<std::pair<float, uint64_t>> wanted{{2, 1000}, {4, 2000}, {5, 2500},
                                                         {1, 3000}, {5, 4000}, {3, 5000}};
    for (size_t i = 0; i < wanted.size(); ++i)
    {
        EXPECT_EQ(commands[i].command.a, wanted[i].first) << i;
        EXPECT_EQ(commands[i].time, wanted[i].second) << i;
    }
    EXPECT_FLOAT_EQ(commands[1].command.b, 5.0f) << "the id of the timer";
    EXPECT_FLOAT_EQ(commands[3].command.b, 7.0f) << "the tick";
}

TEST(ScriptHost, AHookTheScriptDoesNotDefineIsNotWantedAndItsEventsAreHarmless)
{
    HostRig rig("nocore");
    rig.dir.write("factory/a.lua", "function OnTick(t) SetGain(t) end\n");
    rig.select("a");
    EXPECT_TRUE(rig.host.wantsCoreHook(CoreEvent::Kind::Tick));
    EXPECT_FALSE(rig.host.wantsCoreHook(CoreEvent::Kind::Timer));
    EXPECT_FALSE(rig.host.wantsCoreHook(CoreEvent::Kind::TransportStart));
    rig.host.pushCoreEvent({CoreEvent::Kind::Timer, 1, 10});
    rig.host.pushCoreEvent({CoreEvent::Kind::TransportStop, 0, 20});
    rig.host.pushCoreEvent({CoreEvent::Kind::Tick, 3, 30});
    rig.pump();
    const auto commands = popStamped(rig.host);
    ASSERT_EQ(commands.size(), 1u);
    EXPECT_FLOAT_EQ(commands[0].command.a, 3.0f);
    EXPECT_EQ(rig.host.state(), ScriptState::Ok);
}

TEST(ScriptHost, ATimerCommandBecomesARequestWithTheTimeOfItsEventAndNeverReachesTheBinder)
{
    HostRig rig("timers");
    rig.dir.write("factory/a.lua",
                  "SetTimer(1, {ms = 10})\n"
                  "function OnNoteOn(ch, note, vel) SetTimer(2, {ms = 50, ticks = 3, repeating = true}) end\n"
                  "function OnTick(t) StopTimer(5) end\n");
    rig.select("a");
    EXPECT_TRUE(popStamped(rig.host).empty()) << "no command for the binder";
    TimerRequest request;
    ASSERT_TRUE(rig.host.popTimerRequest(request));
    EXPECT_EQ(request.id, 1);
    EXPECT_FALSE(request.stop);
    EXPECT_DOUBLE_EQ(request.milliseconds, 10.0);
    EXPECT_EQ(request.base, 0u);
    EXPECT_EQ(request.generation, 1u);
    EXPECT_FALSE(rig.host.popTimerRequest(request));
    rig.host.pushEvent(HostRig::noteOn(60, 100), 777);
    rig.host.pushCoreEvent({CoreEvent::Kind::Tick, 1, 900});
    rig.pump();
    ASSERT_TRUE(rig.host.popTimerRequest(request));
    EXPECT_EQ(request.id, 2);
    EXPECT_DOUBLE_EQ(request.milliseconds, 50.0);
    EXPECT_DOUBLE_EQ(request.ticks, 3.0);
    EXPECT_TRUE(request.repeat);
    EXPECT_EQ(request.base, 777u);
    ASSERT_TRUE(rig.host.popTimerRequest(request));
    EXPECT_EQ(request.id, 5);
    EXPECT_TRUE(request.stop);
    EXPECT_EQ(request.base, 900u);
}

TEST(ScriptHost, TimerRequestsOfAnOlderScriptAreSkippedAndThoseOfARejectedOneNeverQueued)
{
    HostRig rig("timerstale");
    rig.dir.write("factory/a.lua", "function OnNoteOn(ch, note, vel) SetTimer(2, {ms = 5}) end\n");
    rig.select("a");
    rig.host.pushEvent(HostRig::noteOn(60, 100), 10);
    rig.pump(); // a request of generation 1 waits
    rig.dir.write("factory/a.lua", "SetTimer(3, {ms = 5})\nerror('no')\n");
    rig.poll();
    EXPECT_EQ(rig.host.state(), ScriptState::RuntimeError);
    TimerRequest request;
    ASSERT_TRUE(rig.host.popTimerRequest(request)) << "the old script is still the one that runs";
    EXPECT_EQ(request.id, 2);
    EXPECT_FALSE(rig.host.popTimerRequest(request)) << "the timer of the rejected script was held back and dropped";
    rig.host.pushEvent(HostRig::noteOn(60, 100), 20);
    rig.pump();
    rig.dir.write("factory/a.lua", "SetTimer(4, {ms = 7})\n");
    rig.poll(); // a new generation: what waits from the old one is stale
    ASSERT_TRUE(rig.host.popTimerRequest(request));
    EXPECT_EQ(request.id, 4) << "the request of the old generation was skipped";
    EXPECT_EQ(request.generation, 2u);
}
}
