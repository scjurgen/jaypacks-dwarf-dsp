#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "scripting/Binding.h"

struct lua_State;
struct lua_Debug;

namespace jp::script
{

/// How a load or a call ended.
enum class Status : uint8_t
{
    Ok,
    CompileError,   ///< the text is not a valid Lua chunk (or is precompiled bytecode, which is refused)
    RuntimeError,   ///< the script raised an error
    BudgetExceeded, ///< more instructions or time than allowed
    OutOfMemory     ///< more memory than allowed
};

/// What one load or call may use. A script that goes over is stopped with an error, never the host.
struct Limits
{
    size_t memoryBytes{1024 * 1024};      ///< all memory of the Lua state
    uint32_t handlerInstructions{100000}; ///< VM instructions of one hook call (about 7 ms on the Dwarf's A35)
    uint32_t loadInstructions{2000000};   ///< VM instructions of loading a script and of its top-level code
    double handlerMillis{20.0};           ///< wall time of one hook call
    double loadMillis{250.0};             ///< wall time of a load
};

/// Receives the commands a script calls, after the runtime has checked them against their CommandSpec.
class CommandSink
{
  public:
    virtual ~CommandSink() = default;

    /// Returns false to reject the call; error then receives a short reason for the script log. Called in the frames
    /// of a Lua call: it must not throw.
    virtual bool onCommand(const CommandCall& call, char* error, size_t errorSize) noexcept = 0;

    /// The value of the getter with this index in the BindingSet (see GetterSpec). Called in the frames of a Lua call:
    /// it must not throw or wait.
    [[nodiscard]] virtual double value(size_t /*getter*/) noexcept
    {
        return 0.0;
    }
};

/// Where `import "name"` finds modules besides the built-in ones.
class ModuleSource
{
  public:
    virtual ~ModuleSource() = default;

    /// Fills source with the module's text and returns true, or returns false if there is no such module. name has
    /// passed the check for letters, digits, '-' and '_'.
    virtual bool find(const char* name, std::string& source) noexcept = 0;
};

/**
 * @brief One sandboxed Lua 5.4 state with the commands, hooks and constants of a BindingSet.
 *
 * Plain Lua C API, Lua compiled as C: an error leaves a script by longjmp, so no C++ frame between a Lua call and
 * the error holds an object with a destructor (the call arguments of a command are plain arrays). The state is
 * limited in memory (an allocator that refuses past the limit), in VM instructions and in wall time per call. When a
 * call runs out of budget the count hook raises an error for every following instruction, so a script cannot
 * swallow it with pcall in a loop. Scripts see base (without dofile, loadfile, load, collectgarbage, warn; print is
 * Log), math, table and string (without dump); no os, io, package, debug, coroutine, utf8 and no precompiled chunks.
 *
 * Two things the instruction count cannot stop are closed off. A __gc finalizer runs with the hooks switched off, so
 * setmetatable refuses a metatable that has one. The string pattern functions (find, match, gmatch, gsub) are C code
 * whose cost grows with the product of the lengths of subject and pattern: they take a subject of at most
 * c_maxPatternSubject and a pattern of at most c_maxPatternLength characters. Coroutines would run without the hook
 * and are not there.
 * math.random is seeded with the constructor's seed (xoshiro256**, the same sequence on every platform), and the
 * string hash seed of the library build is fixed, so a script behaves the same in every run.
 *
 * Globals every script has: Log(...), clamp(x, lo, hi), lerp(a, b, t), import "name" (a module found once per state:
 * the built-in "constants", then the ModuleSource; a name is letters, digits, '-' and '_' only, so it cannot name a
 * path), one function per command and per getter of the BindingSet and one table per enum of it. Single threaded: all
 * calls come from the thread that uses the runtime.
 */
class Runtime
{
  public:
    Runtime(const BindingSet& bindings, CommandSink& sink, ModuleSource* modules, const Limits& limits, uint32_t seed);
    ~Runtime();

    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    /// False if the Lua state could not be created.
    [[nodiscard]] bool valid() const noexcept
    {
        return m_state != nullptr;
    }

    /// Compiles text (chunk name = name, so errors read name:line: message) and runs its top-level code.
    Status load(const char* name, const std::string& text);

    /// Calls the global function hook with numeric arguments (whole numbers arrive as integers). A script that does
    /// not define it is fine (Ok).
    Status call(const char* hook, const double* arguments = nullptr, size_t count = 0);

    [[nodiscard]] bool hasFunction(const char* name);

    /// One incremental step of the garbage collector.
    void collectGarbage();

    /// The error of the last load or call that failed, empty after one that worked.
    [[nodiscard]] const std::string& error() const noexcept
    {
        return m_error;
    }

    /// Lines written by Log(...) and warnings about dropped commands, oldest first, at most c_logLines.
    [[nodiscard]] const std::vector<std::string>& log() const noexcept
    {
        return m_log;
    }

    void clearLog()
    {
        m_log.clear();
    }

    /// Lines added to the log since the runtime was made (the log itself keeps only the latest).
    [[nodiscard]] uint64_t logCount() const noexcept
    {
        return m_logTotal;
    }

    [[nodiscard]] size_t memoryUsed() const noexcept
    {
        return m_memory.used;
    }

    [[nodiscard]] size_t memoryPeak() const noexcept
    {
        return m_memory.peak;
    }

    /// VM instructions (to the 1000 of the count hook) and wall time of the last load or call.
    [[nodiscard]] uint32_t lastInstructions() const noexcept
    {
        return m_lastInstructions;
    }

    [[nodiscard]] double lastMillis() const noexcept
    {
        return m_lastMillis;
    }

    static constexpr size_t c_maxHookArguments{8};
    static constexpr size_t c_maxPatternSubject{8192};
    static constexpr size_t c_maxPatternLength{100};
    static constexpr size_t c_logLines{64};
    static constexpr size_t c_logLineLength{200};

  private:
    struct Memory
    {
        size_t used{0};
        size_t peak{0};
        size_t limit{0};
        bool limitHit{false};
    };

    static void* allocate(void* data, void* pointer, size_t oldSize, size_t newSize);
    static Runtime* owner(lua_State* state);
    static void hook(lua_State* state, lua_Debug* debug);
    static int commandFunction(lua_State* state);
    static int getterFunction(lua_State* state);
    static int importFunction(lua_State* state);
    static int logFunction(lua_State* state);
    static int limitedPatternFunction(lua_State* state);
    static int setMetatableFunction(lua_State* state);
    static int clampFunction(lua_State* state);
    static int lerpFunction(lua_State* state);

    static int bindFunction(lua_State* state);
    void bindAll();
    int runCommand(lua_State* state, size_t index);
    int runImport(lua_State* state);
    void onHook(lua_State* state);
    Status protectedCall(int argumentCount, uint32_t instructionLimit, double millisLimit);
    void warn(lua_State* state, const char* text);
    void addLog(const char* where, const char* text);
    [[nodiscard]] double elapsedMillis() const;

    const BindingSet& m_bindings;
    CommandSink& m_sink;
    ModuleSource* m_modules;
    Limits m_limits;
    uint32_t m_seed;
    Memory m_memory;
    lua_State* m_state{nullptr};
    std::string m_error;
    std::string m_moduleText; ///< scratch for import: a member, so no destructor waits in a frame Lua can leave
    std::vector<std::string> m_log;
    uint64_t m_logTotal{0};
    uint32_t m_instructions{0};
    uint32_t m_instructionLimit{0};
    uint32_t m_hookStep{1000};
    double m_millisLimit{0.0};
    bool m_tripped{false};
    char m_tripMessage[80]{};
    std::chrono::steady_clock::time_point m_start;
    uint32_t m_lastInstructions{0};
    double m_lastMillis{0.0};
};

}
