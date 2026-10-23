#pragma once

#include <sys/stat.h>

#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <vector>

#include "lv2/PedalServices.h"
#include "scripting/Binding.h"
#include "scripting/LuaRuntime.h"
#include "scripting/SpscQueue.h"
#include "scripting/Timers.h"

namespace jp::script
{

/// Where a script file comes from: the plugin's own scripts or the user's.
enum class Origin : uint8_t
{
    Factory,
    User
};

/// How the last load or hook call went, as a number for a control output.
enum class ScriptState : uint8_t
{
    Idle, ///< nothing loaded yet
    Ok,
    FileMissing, ///< the selected file does not exist (or is too large)
    CompileError,
    RuntimeError,
    BudgetExceeded,
    OutOfMemory
};

struct HostConfig
{
    Limits limits{};
    std::string factoryDir; ///< the plugin's own scripts, read only
    std::string userDir;    ///< scripts of the user, and where the logs are written (empty: no logs, no user scripts)
    double pollSeconds{0.5};
    uint32_t seed{1};
    double sampleRate{48000.0};
    /// Index in the plugin's command table of SetTimer (StopTimer follows it): the commands of Timers.h at the end of
    /// the table. The default is none; the host answers them itself and the binder never sees them.
    size_t timerCommandBase{static_cast<size_t>(-1)};
};

/**
 * @brief What a plugin supplies to a ScriptHost: its bindings, the conversion of a script's command into the
 * plugin's own command type, and the call of its hooks for an event.
 *
 * makeCommand() and dispatch() run in the worker thread (dispatch() in a hook's time budget); Event and Command are
 * plain structs that cross the threads by copy.
 */
template <typename Event, typename Command>
class ScriptBinder
{
  public:
    virtual ~ScriptBinder() = default;

    [[nodiscard]] virtual const BindingSet& bindings() const noexcept = 0;

    /// Turns a checked call into a command. Returns false to reject it; error then gets a short reason.
    virtual bool makeCommand(const CommandCall& call, Command& command, char* error, size_t errorSize) noexcept = 0;

    /// Calls the hook that belongs to the event (runtime.call("OnNoteOn", ...)) and returns how it ended.
    virtual Status dispatch(Runtime& runtime, const Event& event) = 0;

    /// A module of the plugin's own that `import "name"` may find, before the user's and the factory files.
    virtual bool embeddedModule(const char* /*name*/, std::string& /*text*/) noexcept
    {
        return false;
    }
};

namespace detail
{

/// Modification time and size of a file; size < 0 when it does not exist.
struct FileStamp
{
    int64_t seconds{0};
    int64_t nanos{0};
    int64_t size{-1};

    bool operator==(const FileStamp& other) const noexcept
    {
        return seconds == other.seconds && nanos == other.nanos && size == other.size;
    }
};

inline FileStamp stampOf(const std::string& path)
{
    struct stat info{};
    FileStamp stamp;
    if (::stat(path.c_str(), &info) != 0)
    {
        return stamp;
    }
#if defined(__APPLE__)
    stamp.seconds = info.st_mtimespec.tv_sec;
    stamp.nanos = info.st_mtimespec.tv_nsec;
#else
    stamp.seconds = info.st_mtim.tv_sec;
    stamp.nanos = info.st_mtim.tv_nsec;
#endif
    stamp.size = static_cast<int64_t>(info.st_size);
    return stamp;
}

/// Reads a whole file of at most maxBytes; false if it is missing, unreadable or larger.
inline bool readTextFile(const std::string& path, const size_t maxBytes, std::string& text)
{
    text.clear();
    FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr)
    {
        return false;
    }
    char buffer[4096];
    size_t count = 0;
    while ((count = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
    {
        text.append(buffer, count);
        if (text.size() > maxBytes)
        {
            std::fclose(file);
            return false;
        }
    }
    std::fclose(file);
    return true;
}

/// Creates the folder and the folders above it that are missing (like mkdir -p); false if it still does not exist.
inline bool makeDirectories(const std::string& path)
{
    for (size_t i = 1; i <= path.size(); ++i)
    {
        if (i == path.size() || path[i] == '/')
        {
            const std::string part = path.substr(0, i);
            if (::mkdir(part.c_str(), 0755) != 0 && errno != EEXIST)
            {
                return false;
            }
        }
    }
    return true;
}

/// A file name part is letters, digits, '-' and '_' (at most 47): no path, no dots.
inline bool validName(const char* name)
{
    const size_t length = std::strlen(name);
    if (length == 0 || length > 47)
    {
        return false;
    }
    for (size_t i = 0; i < length; ++i)
    {
        const char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
        {
            return false;
        }
    }
    return true;
}

inline const char* stateName(const ScriptState state)
{
    switch (state)
    {
        case ScriptState::Idle:
            return "idle";
        case ScriptState::Ok:
            return "ok";
        case ScriptState::FileMissing:
            return "file missing";
        case ScriptState::CompileError:
            return "compile error";
        case ScriptState::RuntimeError:
            return "runtime error";
        case ScriptState::BudgetExceeded:
            return "over the instruction or time budget";
        case ScriptState::OutOfMemory:
            return "out of memory";
    }
    return "";
}

inline ScriptState stateOf(const Status status)
{
    switch (status)
    {
        case Status::Ok:
            return ScriptState::Ok;
        case Status::CompileError:
            return ScriptState::CompileError;
        case Status::RuntimeError:
            return ScriptState::RuntimeError;
        case Status::BudgetExceeded:
            return ScriptState::BudgetExceeded;
        case Status::OutOfMemory:
            return ScriptState::OutOfMemory;
    }
    return ScriptState::RuntimeError;
}

}

/**
 * @brief Scripts for a plugin: loads Lua files in the host's worker thread, hot-reloads them when the file changes,
 * carries the plugin's events to the script's hooks and the script's commands back to the audio thread.
 *
 * Two sides share only lock-free queues and atomics. The audio side (pushEvent(), select(), advance(),
 * popCommand(), the getters) never waits and never allocates; one small job per wake-up goes to the host's worker
 * (WorkScheduler), and the pedal's work() calls work(), which does all the rest: it picks up a script selection,
 * checks the file's modification time every pollSeconds and reloads a changed file, then hands every queued event to
 * the binder's dispatch(). The Lua state lives only in the worker thread.
 *
 * A reload builds a new state: the file is compiled, its top-level code and OnStart run, and the commands they call
 * are held back. Only if all of that worked is the generation counter raised, the held commands queued with the new
 * generation and the old state replaced; otherwise the old state stays (with its sound), the failure is reported in
 * state() and the log, and the same file is tried again only after it changes. popCommand() drops commands of an
 * older generation, and generation() tells a pedal that a new script has taken over, so it can release what the old
 * one set. A full queue drops events or commands and counts them; a refused wake-up job is tried again at the next
 * advance(). After each load and after a failing hook a log file <userDir>/<name>.log is written (status, message,
 * timing, memory and the script's own Log lines).
 */
template <typename Event, typename Command, size_t EventCapacity = 256, size_t CommandCapacity = 1024>
class ScriptHost : private CommandSink, private ModuleSource
{
    static_assert(std::is_trivially_copyable_v<Event> && std::is_trivially_copyable_v<Command>,
                  "events and commands cross the threads by copy");

  public:
    explicit ScriptHost(ScriptBinder<Event, Command>& binder)
        : m_binder(binder)
    {
        m_stagedCommands.reserve(c_maxStaged);
        m_stagedTimers.reserve(c_maxStagedTimers);
    }

    /// Before the first select(), not from the audio thread.
    void configure(const HostConfig& config)
    {
        m_config = config;
        m_pollFrames = static_cast<uint64_t>(config.pollSeconds * config.sampleRate);
        m_pollFrames = m_pollFrames == 0 ? 1 : m_pollFrames;
    }

    void setWorkScheduler(WorkScheduler& scheduler) noexcept
    {
        m_scheduler = &scheduler;
    }

    // -- audio thread ------------------------------------------------------------------------------------------

    /// Queues an event for the hooks and wakes the worker. `time` is the absolute sample the event happened at: the
    /// commands its hook calls carry it (peekCommand() gives it back), so that what they schedule is relative to the
    /// event. False if the queue was full (counted).
    bool pushEvent(const Event& event, const uint64_t time = 0) noexcept
    {
        const bool queued = m_events.push(StampedEvent{event, time});
        wake();
        return queued;
    }

    /// Queues an event of the clock, the transport or a timer (Timers.h) for the hooks OnTick, OnTransportStart,
    /// OnTransportStop and OnTimer, in the order of the times together with the events of pushEvent().
    bool pushCoreEvent(const CoreEvent& event) noexcept
    {
        const bool queued = m_coreEvents.push(event);
        wake();
        return queued;
    }

    /// Stores the number a script reads with the getter of this index (BindingSet::getters): any thread, never waits.
    void setValue(const size_t getter, const double value) noexcept
    {
        if (getter < c_maxGetters)
        {
            m_values[getter].store(value, std::memory_order_relaxed);
        }
    }

    /// Whether the loaded script defines the hook for a kind of core event.
    [[nodiscard]] bool wantsCoreHook(const CoreEvent::Kind kind) const noexcept
    {
        return ((m_coreHookMask.load(std::memory_order_relaxed) >> static_cast<unsigned>(kind)) & 1u) != 0;
    }

    /// The next timer request of the current script (older ones are skipped); false if there is none.
    bool popTimerRequest(TimerRequest& request) noexcept
    {
        while (m_timerRequests.pop(request))
        {
            if (request.generation == m_generation.load(std::memory_order_acquire))
            {
                return true;
            }
        }
        return false;
    }

    /// Asks for the script <name>.lua of the factory or user folder. name: letters, digits, '-' and '_'. Selecting the
    /// script that is loaded loads it again.
    bool select(const Origin origin, const char* name) noexcept
    {
        if (!detail::validName(name))
        {
            return false;
        }
        SelectRequest request{};
        request.origin = origin;
        std::strncpy(request.name, name, sizeof(request.name) - 1);
        const bool queued = m_selections.push(request);
        wake();
        return queued;
    }

    /// Asks for no script at all: the loaded one is dropped, the generation goes up (so queued commands are skipped)
    /// and state() is Idle.
    bool deselect() noexcept
    {
        SelectRequest request{};
        request.clear = true;
        const bool queued = m_selections.push(request);
        wake();
        return queued;
    }

    /// Call once per block with its length: counts the time to the next check of the script file and tries a wake-up
    /// again if events wait (the host may have refused the job).
    void advance(const uint32_t frames) noexcept
    {
        m_framesSincePoll += frames;
        if (m_framesSincePoll >= m_pollFrames)
        {
            m_framesSincePoll = 0;
            m_pollDue.store(true, std::memory_order_release);
            wake();
        }
        else if (m_events.size() > 0 || m_coreEvents.size() > 0 || m_selections.size() > 0)
        {
            wake();
        }
    }

    /// The next command of the current script, with the generation it belongs to. Commands of older scripts are
    /// skipped.
    bool popCommand(Command& command, uint32_t& generation) noexcept
    {
        uint64_t time = 0;
        if (!peekCommand(command, generation, time))
        {
            return false;
        }
        m_commands.discard();
        return true;
    }

    /// The next command of the current script without taking it (commands of older scripts are skipped). Follow with
    /// discardCommand() to take it.
    bool peekCommand(Command& command, uint32_t& generation) noexcept
    {
        uint64_t time = 0;
        return peekCommand(command, generation, time);
    }

    /// As above, and the sample of the event whose hook called the command (0 for the commands of a load).
    bool peekCommand(Command& command, uint32_t& generation, uint64_t& time) noexcept
    {
        StampedCommand stamped;
        while (m_commands.peek(stamped))
        {
            if (stamped.generation == m_generation.load(std::memory_order_acquire))
            {
                command = stamped.command;
                generation = stamped.generation;
                time = stamped.time;
                return true;
            }
            m_commands.discard();
        }
        return false;
    }

    void discardCommand() noexcept
    {
        m_commands.discard();
    }

    /// Counts up each time a script was loaded or dropped; a command of an older generation is stale (0: none yet).
    [[nodiscard]] uint32_t generation() const noexcept
    {
        return m_generation.load(std::memory_order_acquire);
    }

    /// The generation of the last script that loaded: its commands from the load (the top-level code and OnStart) are
    /// all in the queue, ahead of any later one. A pedal that wants a whole patch at once waits for this to change.
    [[nodiscard]] uint32_t readyGeneration() const noexcept
    {
        return m_readyGeneration.load(std::memory_order_acquire);
    }

    [[nodiscard]] ScriptState state() const noexcept
    {
        return static_cast<ScriptState>(m_state.load(std::memory_order_relaxed));
    }

    /// How many calls of a hook ended with an error, a budget overrun or out of memory since the host was made (a load
    /// that fails is not counted: the old script keeps running, state() says what was wrong). A pedal that needs to
    /// know that a script's state is unknown from here on (a MIDI effect ends its notes) watches it.
    [[nodiscard]] uint32_t hookFailures() const noexcept
    {
        return m_hookFailures.load(std::memory_order_acquire);
    }

    [[nodiscard]] uint32_t droppedEvents() const noexcept
    {
        return m_events.dropped() + m_coreEvents.dropped();
    }

    [[nodiscard]] uint32_t droppedCommands() const noexcept
    {
        return m_commands.dropped() + m_staleOrRejected.load(std::memory_order_relaxed);
    }

    /// Wake-up jobs the host refused.
    [[nodiscard]] uint32_t refusedWakeUps() const noexcept
    {
        return m_wakeRefused.load(std::memory_order_relaxed);
    }

    /// Milliseconds the last successful load took, and the peak Lua memory of the current script in bytes.
    [[nodiscard]] float loadMillis() const noexcept
    {
        return m_loadMillis.load(std::memory_order_relaxed);
    }

    [[nodiscard]] uint32_t memoryPeak() const noexcept
    {
        return m_memoryPeak.load(std::memory_order_relaxed);
    }

    /// Whether the loaded script defines the hook with this index in the plugin's BindingSet. A pedal that does not
    /// send events nobody listens to saves the wake-ups.
    [[nodiscard]] bool wantsHook(const size_t index) const noexcept
    {
        return index < 32 && ((m_hookMask.load(std::memory_order_relaxed) >> index) & 1u) != 0;
    }

    // -- worker thread -----------------------------------------------------------------------------------------

    /// The body of the pedal's work(): selection, file check, events. Also what a non-real-time thread may call to
    /// load a script at once (activation): the calls are serialised, so it cannot run beside the host's worker.
    void work()
    {
        const std::lock_guard<std::mutex> lock(m_workMutex);
        m_wakePending.store(false, std::memory_order_release);
        serviceSelections();
        if (m_pollDue.exchange(false, std::memory_order_acq_rel))
        {
            flushLog(true);
            pollFile();
        }
        serviceEvents();
    }

    /// The text of the last log file written (for tests and tools); worker thread or when it is idle.
    [[nodiscard]] const std::string& lastLog() const noexcept
    {
        return m_lastLog;
    }

  private:
    struct StampedCommand
    {
        Command command;
        uint32_t generation;
        uint64_t time;
    };

    struct StampedEvent
    {
        Event event;
        uint64_t time;
    };

    struct SelectRequest
    {
        Origin origin;
        bool clear;
        char name[48];
    };

    static constexpr size_t c_maxStaged{256};
    static constexpr size_t c_maxStagedTimers{64};
    static constexpr size_t c_maxScriptBytes{256 * 1024};
    static constexpr size_t c_eventsPerWake{4096};

    void wake() noexcept
    {
        if (m_scheduler == nullptr || m_wakePending.exchange(true, std::memory_order_acq_rel))
        {
            return;
        }
        const uint8_t job{'W'};
        if (!m_scheduler->schedule(&job, 1))
        {
            m_wakePending.store(false, std::memory_order_release);
            m_wakeRefused.fetch_add(1, std::memory_order_relaxed);
        }
    }

    // CommandSink: the value of a getter, inside Lua's frames
    [[nodiscard]] double value(const size_t getter) noexcept override
    {
        return getter < c_maxGetters ? m_values[getter].load(std::memory_order_relaxed) : 0.0;
    }

    // CommandSink: a command called by the script, inside Lua's frames
    bool onCommand(const CommandCall& call, char* error, const size_t errorSize) noexcept override
    {
        if (call.command >= m_config.timerCommandBase)
        {
            return onTimerCommand(call, error, errorSize);
        }
        Command command{};
        if (!m_binder.makeCommand(call, command, error, errorSize))
        {
            return false;
        }
        if (m_staging)
        {
            if (m_stagedCommands.size() >= c_maxStaged)
            {
                std::snprintf(error, errorSize, "more than %zu commands while loading", c_maxStaged);
                return false;
            }
            m_stagedCommands.push_back(command); // reserved: no allocation
            return true;
        }
        if (!m_commands.push(StampedCommand{command, m_generation.load(std::memory_order_relaxed), m_dispatchTime}))
        {
            std::snprintf(error, errorSize, "the command queue is full");
            return false;
        }
        return true;
    }

    bool onTimerCommand(const CommandCall& call, char* error, const size_t errorSize) noexcept
    {
        TimerRequest request;
        request.id = static_cast<uint8_t>(call.integer(0));
        if (call.command == m_config.timerCommandBase + 1)
        {
            request.stop = true;
        }
        else
        {
            request.milliseconds = call.number[1];
            request.ticks = call.number[2];
            request.repeat = call.flag(3);
        }
        request.base = m_dispatchTime;
        if (m_staging)
        {
            if (m_stagedTimers.size() >= c_maxStagedTimers)
            {
                std::snprintf(error, errorSize, "more than %zu timer commands while loading", c_maxStagedTimers);
                return false;
            }
            m_stagedTimers.push_back(request); // reserved: no allocation
            return true;
        }
        request.generation = m_generation.load(std::memory_order_relaxed);
        if (!m_timerRequests.push(request))
        {
            std::snprintf(error, errorSize, "the timer queue is full");
            return false;
        }
        return true;
    }

    // ModuleSource: import "name"
    bool find(const char* name, std::string& text) noexcept override
    {
        if (m_binder.embeddedModule(name, text))
        {
            return true;
        }
        for (const std::string* directory : {&m_config.userDir, &m_config.factoryDir})
        {
            if (!directory->empty() && detail::readTextFile(*directory + "/" + name + ".lua", c_maxScriptBytes, text))
            {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] std::string pathOf(const Origin origin, const char* name) const
    {
        return (origin == Origin::Factory ? m_config.factoryDir : m_config.userDir) + "/" + name + ".lua";
    }

    void serviceSelections()
    {
        SelectRequest request;
        bool any = false;
        SelectRequest last{};
        while (m_selections.pop(request))
        {
            last = request;
            any = true;
        }
        if (any && last.clear)
        {
            m_wanted = false;
            m_runtime.reset();
            m_stagedCommands.clear();
            m_hookMask.store(0, std::memory_order_relaxed);
            m_coreHookMask.store(0, std::memory_order_relaxed);
            m_generation.store(m_generation.load(std::memory_order_relaxed) + 1, std::memory_order_release);
            m_state.store(static_cast<uint8_t>(ScriptState::Idle), std::memory_order_relaxed);
        }
        else if (any)
        {
            m_wantedOrigin = last.origin;
            m_wantedName = last.name;
            m_wanted = true;
            loadWanted();
        }
    }

    void pollFile()
    {
        if (!m_wanted)
        {
            return;
        }
        if (!(detail::stampOf(pathOf(m_wantedOrigin, m_wantedName.c_str())) == m_attempted))
        {
            loadWanted();
        }
    }

    /// The events of the plugin and of the clock, in the order of their times.
    void serviceEvents()
    {
        StampedEvent event;
        CoreEvent core;
        size_t handled = 0;
        while (handled < c_eventsPerWake)
        {
            const bool haveEvent = m_events.peek(event);
            const bool haveCore = m_coreEvents.peek(core);
            if (!haveEvent && !haveCore)
            {
                break;
            }
            ++handled;
            if (haveCore && (!haveEvent || core.time <= event.time))
            {
                m_coreEvents.discard();
                dispatchCore(core);
            }
            else
            {
                m_events.discard();
                if (m_runtime != nullptr)
                {
                    m_dispatchTime = event.time;
                    noteHookResult(m_binder.dispatch(*m_runtime, event.event));
                }
            }
        }
        m_dispatchTime = 0;
        if (m_runtime != nullptr && handled > 0)
        {
            m_runtime->collectGarbage();
            flushLog(false);
        }
    }

    void dispatchCore(const CoreEvent& event)
    {
        if (m_runtime == nullptr || !wantsCoreHook(event.kind))
        {
            return;
        }
        static constexpr std::array<const char*, 4> names{"OnTick", "OnTransportStart", "OnTransportStop", "OnTimer"};
        const double argument = static_cast<double>(event.value);
        const bool hasArgument = event.kind == CoreEvent::Kind::Tick || event.kind == CoreEvent::Kind::Timer;
        m_dispatchTime = event.time;
        noteHookResult(m_runtime->call(names[static_cast<size_t>(event.kind)], hasArgument ? &argument : nullptr,
                                       hasArgument ? 1 : 0));
    }

    void noteHookResult(const Status status)
    {
        if (status != Status::Ok)
        {
            m_state.store(static_cast<uint8_t>(detail::stateOf(status)), std::memory_order_relaxed);
            m_hookFailed = true;
            m_hookError = m_runtime->error();
            m_hookFailures.fetch_add(1, std::memory_order_release);
        }
    }

    /// Writes the log file when a hook failed or the script wrote lines since the last time; at most once a second
    /// unless forced (the poll forces it, so the last lines of a burst are not left out).
    void flushLog(const bool force)
    {
        if (m_runtime == nullptr || (!m_hookFailed && m_runtime->logCount() == m_loggedCount))
        {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        if (!force && now - m_lastHookLog < std::chrono::seconds(1))
        {
            return;
        }
        m_lastHookLog = now;
        m_loggedCount = m_runtime->logCount();
        const bool failed = m_hookFailed;
        m_hookFailed = false;
        writeLog(failed ? "a hook failed: " + m_hookError : std::string("script output"));
    }

    void loadWanted()
    {
        const std::string path = pathOf(m_wantedOrigin, m_wantedName.c_str());
        m_attempted = detail::stampOf(path);
        std::string text;
        if (!detail::readTextFile(path, c_maxScriptBytes, text))
        {
            m_state.store(static_cast<uint8_t>(ScriptState::FileMissing), std::memory_order_relaxed);
            writeLog("cannot read " + path + " (missing, unreadable or larger than " +
                     std::to_string(c_maxScriptBytes / 1024) + " kB)");
            return;
        }
        CommandSink& sink = *this; // the bases are private: convert here, not inside make_unique
        ModuleSource* modules = this;
        auto fresh = std::make_unique<Runtime>(m_binder.bindings(), sink, modules, m_config.limits, m_config.seed);
        m_stagedCommands.clear();
        m_stagedTimers.clear();
        m_staging = true;
        Status status = fresh->load(m_wantedName.c_str(), text);
        if (status == Status::Ok && fresh->hasFunction("OnStart"))
        {
            status = fresh->call("OnStart");
        }
        m_staging = false;
        if (status != Status::Ok)
        {
            m_state.store(static_cast<uint8_t>(detail::stateOf(status)), std::memory_order_relaxed);
            m_runtimeLogSource = fresh.get();
            writeLog("not loaded, the previous script stays: " + fresh->error());
            m_runtimeLogSource = nullptr;
            return;
        }
        const uint32_t generation = m_generation.load(std::memory_order_relaxed) + 1;
        m_generation.store(generation, std::memory_order_release);
        for (const Command& command : m_stagedCommands)
        {
            if (!m_commands.push(StampedCommand{command, generation, 0}))
            {
                m_staleOrRejected.fetch_add(1, std::memory_order_relaxed);
            }
        }
        m_stagedCommands.clear();
        for (TimerRequest& request : m_stagedTimers)
        {
            request.generation = generation;
            if (!m_timerRequests.push(request))
            {
                m_staleOrRejected.fetch_add(1, std::memory_order_relaxed);
            }
        }
        m_stagedTimers.clear();
        m_loadMillis.store(static_cast<float>(fresh->lastMillis()), std::memory_order_relaxed);
        m_memoryPeak.store(static_cast<uint32_t>(fresh->memoryPeak()), std::memory_order_relaxed);
        uint32_t mask = 0;
        for (size_t i = 0; i < m_binder.bindings().hookCount && i < 32; ++i)
        {
            mask |= fresh->hasFunction(m_binder.bindings().hooks[i].name) ? (1u << i) : 0u;
        }
        m_hookMask.store(mask, std::memory_order_relaxed);
        static constexpr std::array<const char*, 4> coreNames{"OnTick", "OnTransportStart", "OnTransportStop",
                                                              "OnTimer"};
        uint32_t coreMask = 0;
        for (size_t i = 0; i < coreNames.size(); ++i)
        {
            coreMask |= fresh->hasFunction(coreNames[i]) ? (1u << i) : 0u;
        }
        m_coreHookMask.store(coreMask, std::memory_order_relaxed);
        m_runtime = std::move(fresh);
        m_loggedCount = m_runtime->logCount();
        m_hookFailed = false;
        m_state.store(static_cast<uint8_t>(ScriptState::Ok), std::memory_order_relaxed);
        m_readyGeneration.store(generation, std::memory_order_release);
        writeLog("loaded");
    }

    /// Writes <userDir>/<name>.log: the outcome, the numbers and the script's own log lines.
    void writeLog(const std::string& message)
    {
        const Runtime* source = m_runtimeLogSource != nullptr ? m_runtimeLogSource : m_runtime.get();
        std::string text = std::string("script: ") + (m_wantedOrigin == Origin::Factory ? "factory/" : "user/") +
                           m_wantedName + ".lua\n";
        text += std::string("state: ") + detail::stateName(state()) + "\n";
        text += "message: " + message + "\n";
        text += "generation: " + std::to_string(generation()) + "\n";
        char numbers[160];
        std::snprintf(numbers, sizeof(numbers),
                      "load: %.2f ms\nmemory peak: %u bytes\ndropped events: %u\ndropped commands: %u\n",
                      static_cast<double>(loadMillis()), memoryPeak(), droppedEvents(), droppedCommands());
        text += numbers;
        if (source != nullptr && !source->log().empty())
        {
            text += "--- script log\n";
            for (const std::string& line : source->log())
            {
                text += line + "\n";
            }
        }
        m_lastLog = text;
        if (m_config.userDir.empty())
        {
            return;
        }
        const std::string path = m_config.userDir + "/" + m_wantedName + ".log";
        FILE* file = std::fopen(path.c_str(), "wb");
        if (file == nullptr)
        {
            detail::makeDirectories(m_config.userDir);
            file = std::fopen(path.c_str(), "wb");
        }
        if (file == nullptr)
        {
            return;
        }
        std::fwrite(text.data(), 1, text.size(), file);
        std::fclose(file);
    }

    ScriptBinder<Event, Command>& m_binder;
    HostConfig m_config;
    WorkScheduler* m_scheduler{nullptr};

    SpscQueue<StampedEvent, EventCapacity> m_events;
    SpscQueue<CoreEvent, 256> m_coreEvents;
    SpscQueue<TimerRequest, 64> m_timerRequests;
    SpscQueue<StampedCommand, CommandCapacity> m_commands;
    SpscQueue<SelectRequest, 8> m_selections;
    std::atomic<bool> m_wakePending{false};
    std::atomic<bool> m_pollDue{false};
    std::atomic<uint8_t> m_state{static_cast<uint8_t>(ScriptState::Idle)};
    std::atomic<uint32_t> m_hookFailures{0};
    std::atomic<uint32_t> m_generation{0};
    std::atomic<uint32_t> m_readyGeneration{0};
    std::atomic<uint32_t> m_wakeRefused{0};
    std::atomic<uint32_t> m_staleOrRejected{0};
    std::atomic<float> m_loadMillis{0.0f};
    std::atomic<uint32_t> m_memoryPeak{0};
    std::atomic<uint32_t> m_hookMask{0};
    std::atomic<uint32_t> m_coreHookMask{0};
    std::array<std::atomic<double>, c_maxGetters> m_values{};
    std::mutex m_workMutex;
    uint64_t m_pollFrames{24000};
    uint64_t m_framesSincePoll{0};

    // worker thread only
    std::unique_ptr<Runtime> m_runtime;
    const Runtime* m_runtimeLogSource{nullptr};
    std::vector<Command> m_stagedCommands;
    std::vector<TimerRequest> m_stagedTimers;
    uint64_t m_dispatchTime{0};
    bool m_staging{false};
    bool m_wanted{false};
    bool m_hookFailed{false};
    std::string m_hookError;
    uint64_t m_loggedCount{0};
    std::chrono::steady_clock::time_point m_lastHookLog{};
    Origin m_wantedOrigin{Origin::Factory};
    std::string m_wantedName;
    detail::FileStamp m_attempted;
    std::string m_lastLog;
};

}
