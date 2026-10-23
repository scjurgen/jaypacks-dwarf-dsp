#include "scripting/LuaRuntime.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C"
{
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

namespace jp::script
{

namespace
{

constexpr const char* c_importedKey{"jp_imported"};

bool validModuleName(const char* name, const size_t length)
{
    if (length == 0 || length > 40)
    {
        return false;
    }
    for (size_t i = 0; i < length; ++i)
    {
        const char c = name[i];
        const bool ok =
            (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok)
        {
            return false;
        }
    }
    return true;
}

enum class Read
{
    Ok,
    Dropped
};

/// Reads the Lua value at index into number (and text) as the spec says. A wrong Lua type raises a Lua error; a value
/// that is not usable (not finite, not whole, out of range, too long) returns Dropped with the reason. Only plain
/// arrays are alive here, because luaL_error leaves by longjmp.
Read readValue(lua_State* state, const int index, const ValueSpec& spec, const char* command, double& number,
               char* text, char* reason, const size_t reasonSize)
{
    const int type = lua_type(state, index);
    const char* expected = nullptr;
    switch (spec.kind)
    {
        case ValueKind::Number:
        case ValueKind::Integer:
            expected = type == LUA_TNUMBER ? nullptr : "a number";
            break;
        case ValueKind::Flag:
            expected = type == LUA_TBOOLEAN ? nullptr : "true or false";
            break;
        case ValueKind::Text:
            expected = type == LUA_TSTRING ? nullptr : "a string";
            break;
    }
    if (expected != nullptr)
    {
        luaL_error(state, "%s: '%s' must be %s, not %s", command, spec.name, expected, luaL_typename(state, index));
    }
    if (spec.kind == ValueKind::Flag)
    {
        number = lua_toboolean(state, index) != 0 ? 1.0 : 0.0;
        return Read::Ok;
    }
    if (spec.kind == ValueKind::Text)
    {
        size_t length = 0;
        const char* string = lua_tolstring(state, index, &length);
        if (length > c_maxTextLength)
        {
            std::snprintf(reason, reasonSize, "%s: '%s' is longer than %zu characters", command, spec.name,
                          c_maxTextLength);
            return Read::Dropped;
        }
        std::memcpy(text, string, length);
        text[length] = '\0';
        number = static_cast<double>(length);
        return Read::Ok;
    }
    const double value = lua_tonumber(state, index);
    if (!std::isfinite(value))
    {
        std::snprintf(reason, reasonSize, "%s: '%s' is not a finite number", command, spec.name);
        return Read::Dropped;
    }
    if (spec.kind == ValueKind::Number)
    {
        number = std::min(std::max(value, spec.minimum), spec.maximum);
        return Read::Ok;
    }
    if (value != std::floor(value))
    {
        std::snprintf(reason, reasonSize, "%s: '%s' = %g is not a whole number", command, spec.name, value);
        return Read::Dropped;
    }
    if (value < spec.minimum || value > spec.maximum)
    {
        std::snprintf(reason, reasonSize, "%s: '%s' = %g is outside %g..%g", command, spec.name, value, spec.minimum,
                      spec.maximum);
        return Read::Dropped;
    }
    number = value;
    return Read::Ok;
}

}

Runtime::Runtime(const BindingSet& bindings, CommandSink& sink, ModuleSource* modules, const Limits& limits,
                 const uint32_t seed)
    : m_bindings(bindings)
    , m_sink(sink)
    , m_modules(modules)
    , m_limits(limits)
    , m_seed(seed)
{
    m_memory.limit = limits.memoryBytes;
    m_state = lua_newstate(&Runtime::allocate, &m_memory);
    if (m_state == nullptr)
    {
        return;
    }
    *static_cast<Runtime**>(lua_getextraspace(m_state)) = this;
    // binding allocates, so it runs protected: a memory limit that is too small fails the constructor instead of
    // reaching Lua's panic function
    lua_pushcfunction(m_state, &Runtime::bindFunction);
    if (lua_pcall(m_state, 0, 0, 0) != LUA_OK)
    {
        lua_close(m_state);
        m_state = nullptr;
    }
}

Runtime::~Runtime()
{
    if (m_state == nullptr)
    {
        return;
    }
    lua_close(m_state);
}

void* Runtime::allocate(void* data, void* pointer, const size_t oldSize, const size_t newSize)
{
    auto* memory = static_cast<Memory*>(data);
    if (newSize == 0)
    {
        if (pointer != nullptr)
        {
            memory->used -= oldSize;
            std::free(pointer);
        }
        return nullptr;
    }
    // for a new block oldSize is a type tag, not a size
    const size_t old = pointer == nullptr ? 0 : oldSize;
    if (memory->used - old + newSize > memory->limit)
    {
        memory->limitHit = true;
        return nullptr;
    }
    void* grown = std::realloc(pointer, newSize);
    if (grown != nullptr)
    {
        memory->used = memory->used - old + newSize;
        memory->peak = std::max(memory->peak, memory->used);
    }
    return grown;
}

Runtime* Runtime::owner(lua_State* state)
{
    return *static_cast<Runtime**>(lua_getextraspace(state));
}

int Runtime::bindFunction(lua_State* state)
{
    owner(state)->bindAll();
    return 0;
}

void Runtime::bindAll()
{
    lua_State* state = m_state;
    luaL_requiref(state, "_G", luaopen_base, 1);
    lua_pop(state, 1);
    luaL_requiref(state, LUA_MATHLIBNAME, luaopen_math, 1);
    lua_pop(state, 1);
    luaL_requiref(state, LUA_TABLIBNAME, luaopen_table, 1);
    lua_pop(state, 1);
    luaL_requiref(state, LUA_STRLIBNAME, luaopen_string, 1);
    lua_pop(state, 1);
    for (const char* name : {"dofile", "loadfile", "load", "collectgarbage", "warn", "print"})
    {
        lua_pushnil(state);
        lua_setglobal(state, name);
    }
    lua_getglobal(state, "string");
    lua_pushnil(state);
    lua_setfield(state, -2, "dump");
    // the pattern matcher is C code that no instruction count can interrupt: bound what it is given
    for (const char* name : {"find", "match", "gmatch", "gsub"})
    {
        lua_getfield(state, -1, name);
        lua_pushcclosure(state, &Runtime::limitedPatternFunction, 1);
        lua_setfield(state, -2, name);
    }
    lua_pop(state, 1);
    // a __gc finalizer runs with the hooks switched off, so a loop in it could not be stopped
    lua_getglobal(state, "setmetatable");
    lua_pushcclosure(state, &Runtime::setMetatableFunction, 1);
    lua_setglobal(state, "setmetatable");
    lua_getglobal(state, "math");
    lua_getfield(state, -1, "randomseed");
    lua_pushinteger(state, static_cast<lua_Integer>(m_seed));
    lua_call(state, 1, 0);
    lua_pop(state, 1);

    lua_newtable(state);
    lua_setfield(state, LUA_REGISTRYINDEX, c_importedKey);

    lua_pushcfunction(state, &Runtime::logFunction);
    lua_setglobal(state, "Log");
    lua_pushcfunction(state, &Runtime::logFunction);
    lua_setglobal(state, "print");
    lua_pushcfunction(state, &Runtime::clampFunction);
    lua_setglobal(state, "clamp");
    lua_pushcfunction(state, &Runtime::lerpFunction);
    lua_setglobal(state, "lerp");
    lua_pushcfunction(state, &Runtime::importFunction);
    lua_setglobal(state, "import");

    for (size_t i = 0; i < m_bindings.commandCount; ++i)
    {
        lua_pushinteger(state, static_cast<lua_Integer>(i));
        lua_pushcclosure(state, &Runtime::commandFunction, 1);
        lua_setglobal(state, m_bindings.commands[i].name);
    }
    for (size_t i = 0; i < m_bindings.getterCount && i < c_maxGetters; ++i)
    {
        lua_pushinteger(state, static_cast<lua_Integer>(i));
        lua_pushcclosure(state, &Runtime::getterFunction, 1);
        lua_setglobal(state, m_bindings.getters[i].name);
    }
    for (size_t i = 0; i < m_bindings.enumCount; ++i)
    {
        const EnumSpec& spec = m_bindings.enums[i];
        lua_createtable(state, 0, spec.count);
        for (size_t k = 0; k < spec.count; ++k)
        {
            lua_pushinteger(state, spec.entries[k].value);
            lua_setfield(state, -2, spec.entries[k].name);
        }
        lua_setglobal(state, spec.name);
    }
}

double Runtime::elapsedMillis() const
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - m_start).count();
}

void Runtime::hook(lua_State* state, lua_Debug* /*debug*/)
{
    owner(state)->onHook(state);
}

void Runtime::onHook(lua_State* state)
{
    m_instructions += m_hookStep;
    if (!m_tripped)
    {
        const bool tooManyInstructions = m_instructions > m_instructionLimit;
        const bool tooLong = elapsedMillis() > m_millisLimit;
        if (!tooManyInstructions && !tooLong)
        {
            return;
        }
        m_tripped = true;
        if (tooManyInstructions)
        {
            std::snprintf(m_tripMessage, sizeof(m_tripMessage), "script used more than %u instructions",
                          m_instructionLimit);
        }
        else
        {
            std::snprintf(m_tripMessage, sizeof(m_tripMessage), "script ran longer than %g ms", m_millisLimit);
        }
        // from here on every instruction raises the error, so a pcall in a loop cannot keep the script alive
        m_hookStep = 1;
        lua_sethook(state, &Runtime::hook, LUA_MASKCOUNT, 1);
    }
    luaL_error(state, "%s", m_tripMessage);
}

Status Runtime::protectedCall(const int argumentCount, const uint32_t instructionLimit, const double millisLimit)
{
    m_instructions = 0;
    m_instructionLimit = instructionLimit;
    m_millisLimit = millisLimit;
    m_hookStep = 1000;
    m_tripped = false;
    m_memory.limitHit = false;
    m_start = std::chrono::steady_clock::now();
    lua_sethook(m_state, &Runtime::hook, LUA_MASKCOUNT, 1000);
    const int code = lua_pcall(m_state, argumentCount, 0, 0);
    lua_sethook(m_state, nullptr, 0, 0);
    m_lastInstructions = m_instructions;
    m_lastMillis = elapsedMillis();
    if (code == LUA_OK)
    {
        m_error.clear();
        return Status::Ok;
    }
    const char* message = lua_tostring(m_state, -1);
    m_error = message != nullptr ? message : "(the error value is not a string)";
    lua_pop(m_state, 1);
    if (m_tripped)
    {
        return Status::BudgetExceeded;
    }
    return (code == LUA_ERRMEM || m_memory.limitHit) ? Status::OutOfMemory : Status::RuntimeError;
}

Status Runtime::load(const char* name, const std::string& text)
{
    m_error.clear();
    if (m_state == nullptr)
    {
        m_error = "the Lua state could not be created";
        return Status::OutOfMemory;
    }
    lua_settop(m_state, 0);
    const std::string chunk = std::string("=") + name;
    // "t": text only. Precompiled chunks can crash the interpreter and are never loaded.
    const int code = luaL_loadbufferx(m_state, text.data(), text.size(), chunk.c_str(), "t");
    if (code != LUA_OK)
    {
        const char* message = lua_tostring(m_state, -1);
        m_error = message != nullptr ? message : "(no message)";
        lua_pop(m_state, 1);
        return code == LUA_ERRMEM ? Status::OutOfMemory : Status::CompileError;
    }
    return protectedCall(0, m_limits.loadInstructions, m_limits.loadMillis);
}

bool Runtime::hasFunction(const char* name)
{
    if (m_state == nullptr)
    {
        return false;
    }
    lua_getglobal(m_state, name);
    const bool found = lua_isfunction(m_state, -1);
    lua_pop(m_state, 1);
    return found;
}

Status Runtime::call(const char* hookName, const double* arguments, const size_t requested)
{
    if (m_state == nullptr)
    {
        m_error = "the Lua state could not be created";
        return Status::OutOfMemory;
    }
    lua_settop(m_state, 0);
    lua_getglobal(m_state, hookName);
    if (!lua_isfunction(m_state, -1))
    {
        lua_pop(m_state, 1);
        m_error.clear();
        return Status::Ok;
    }
    const size_t count = std::min<size_t>(requested, c_maxHookArguments);
    for (size_t i = 0; i < count; ++i)
    {
        const double value = arguments[i];
        if (value == std::floor(value) && std::fabs(value) < 9.0e15)
        {
            lua_pushinteger(m_state, static_cast<lua_Integer>(value));
        }
        else
        {
            lua_pushnumber(m_state, value);
        }
    }
    return protectedCall(static_cast<int>(count), m_limits.handlerInstructions, m_limits.handlerMillis);
}

void Runtime::collectGarbage()
{
    if (m_state == nullptr)
    {
        return;
    }
    lua_gc(m_state, LUA_GCSTEP, 0);
}

void Runtime::addLog(const char* where, const char* text)
{
    std::string line(where);
    line += text;
    if (line.size() > c_logLineLength)
    {
        line.resize(c_logLineLength);
    }
    if (m_log.size() >= c_logLines)
    {
        m_log.erase(m_log.begin());
    }
    m_log.push_back(std::move(line));
    ++m_logTotal;
}

void Runtime::warn(lua_State* state, const char* text)
{
    char where[96]{};
    luaL_where(state, 1);
    const char* position = lua_tostring(state, -1);
    if (position != nullptr)
    {
        std::snprintf(where, sizeof(where), "%s ", position);
    }
    lua_pop(state, 1);
    addLog(where, text);
}

int Runtime::logFunction(lua_State* state)
{
    char line[c_logLineLength + 1];
    size_t used = 0;
    const int count = lua_gettop(state);
    for (int i = 1; i <= count; ++i)
    {
        size_t length = 0;
        const char* text = luaL_tolstring(state, i, &length);
        if (i > 1 && used < c_logLineLength)
        {
            line[used++] = ' ';
        }
        const size_t room = c_logLineLength - used;
        const size_t copied = std::min(length, room);
        std::memcpy(line + used, text, copied);
        used += copied;
        lua_pop(state, 1);
    }
    line[used] = '\0';
    owner(state)->addLog("", line);
    return 0;
}

int Runtime::limitedPatternFunction(lua_State* state)
{
    size_t subject = 0;
    size_t pattern = 0;
    luaL_checklstring(state, 1, &subject);
    luaL_checklstring(state, 2, &pattern);
    if (subject > c_maxPatternSubject || pattern > c_maxPatternLength)
    {
        return luaL_error(state,
                          "string patterns work on a subject of at most %d and a pattern of at most %d characters",
                          static_cast<int>(c_maxPatternSubject), static_cast<int>(c_maxPatternLength));
    }
    const int count = lua_gettop(state);
    lua_pushvalue(state, lua_upvalueindex(1));
    lua_insert(state, 1);
    lua_call(state, count, LUA_MULTRET);
    return lua_gettop(state);
}

int Runtime::setMetatableFunction(lua_State* state)
{
    if (lua_istable(state, 2))
    {
        lua_pushliteral(state, "__gc");
        lua_rawget(state, 2);
        if (!lua_isnil(state, -1))
        {
            return luaL_error(state, "setmetatable: finalizers (__gc) are not available to scripts");
        }
        lua_pop(state, 1);
    }
    const int count = lua_gettop(state);
    lua_pushvalue(state, lua_upvalueindex(1));
    lua_insert(state, 1);
    lua_call(state, count, 1);
    return 1;
}

int Runtime::clampFunction(lua_State* state)
{
    const double value = luaL_checknumber(state, 1);
    const double low = luaL_checknumber(state, 2);
    const double high = luaL_checknumber(state, 3);
    lua_pushnumber(state, std::min(std::max(value, low), high));
    return 1;
}

int Runtime::lerpFunction(lua_State* state)
{
    const double a = luaL_checknumber(state, 1);
    const double b = luaL_checknumber(state, 2);
    const double t = luaL_checknumber(state, 3);
    lua_pushnumber(state, a + (b - a) * t);
    return 1;
}

int Runtime::getterFunction(lua_State* state)
{
    const auto index = static_cast<size_t>(lua_tointeger(state, lua_upvalueindex(1)));
    lua_pushnumber(state, owner(state)->m_sink.value(index));
    return 1;
}

int Runtime::commandFunction(lua_State* state)
{
    const auto index = static_cast<size_t>(lua_tointeger(state, lua_upvalueindex(1)));
    return owner(state)->runCommand(state, index);
}

int Runtime::runCommand(lua_State* state, const size_t index)
{
    const CommandSpec& spec = m_bindings.commands[index];
    CommandCall call;
    call.command = index;
    call.valueCount = static_cast<size_t>(spec.argCount) + spec.fieldCount;
    char reason[160]{};
    bool dropped = false;

    const int top = lua_gettop(state);
    const int maxArguments = spec.argCount + (spec.fieldCount > 0 ? 1 : 0);
    if (top < spec.argCount)
    {
        return luaL_error(state, "%s expects %d argument(s) and got %d", spec.name, static_cast<int>(spec.argCount),
                          top);
    }
    if (top > maxArguments)
    {
        return luaL_error(state, "%s takes at most %d argument(s) and got %d", spec.name, maxArguments, top);
    }
    int table = 0;
    if (spec.fieldCount > 0 && top == spec.argCount + 1 && !lua_isnil(state, top))
    {
        if (!lua_istable(state, top))
        {
            return luaL_error(state, "%s: the last argument must be a table of fields, not %s", spec.name,
                              luaL_typename(state, top));
        }
        table = top;
    }

    for (size_t i = 0; i < spec.argCount && !dropped; ++i)
    {
        call.given[i] = true;
        dropped = readValue(state, static_cast<int>(i) + 1, spec.args[i], spec.name, call.number[i],
                            call.text[i].data(), reason, sizeof(reason)) == Read::Dropped;
    }
    for (size_t k = 0; k < spec.fieldCount && !dropped; ++k)
    {
        const size_t i = spec.argCount + k;
        const ValueSpec& field = spec.fields[k];
        call.number[i] = field.defaultValue;
        if (table == 0)
        {
            continue;
        }
        lua_getfield(state, table, field.name);
        if (!lua_isnil(state, -1))
        {
            call.given[i] = true;
            dropped = readValue(state, -1, field, spec.name, call.number[i], call.text[i].data(), reason,
                                sizeof(reason)) == Read::Dropped;
        }
        lua_pop(state, 1);
    }
    if (table != 0)
    {
        // a misspelled field would silently keep its default: say so
        lua_pushnil(state);
        while (lua_next(state, table) != 0)
        {
            char message[120]{};
            if (lua_type(state, -2) != LUA_TSTRING)
            {
                std::snprintf(message, sizeof(message), "%s: entries without a name are ignored", spec.name);
                warn(state, message);
            }
            else
            {
                const char* key = lua_tostring(state, -2);
                bool known = false;
                for (size_t k = 0; k < spec.fieldCount; ++k)
                {
                    known = known || std::strcmp(key, spec.fields[k].name) == 0;
                }
                if (!known)
                {
                    std::snprintf(message, sizeof(message), "%s: unknown field '%s' is ignored", spec.name, key);
                    warn(state, message);
                }
            }
            lua_pop(state, 1);
        }
    }
    if (dropped)
    {
        char message[200]{};
        std::snprintf(message, sizeof(message), "%s, call ignored", reason);
        warn(state, message);
        return 0;
    }
    char error[120]{};
    if (!m_sink.onCommand(call, error, sizeof(error)))
    {
        char message[200]{};
        std::snprintf(message, sizeof(message), "%s: %s, call ignored", spec.name, error);
        warn(state, message);
    }
    return 0;
}

int Runtime::importFunction(lua_State* state)
{
    return owner(state)->runImport(state);
}

int Runtime::runImport(lua_State* state)
{
    size_t length = 0;
    const char* name = luaL_checklstring(state, 1, &length);
    if (!validModuleName(name, length))
    {
        return luaL_error(state, "import: '%s' is not a module name (letters, digits, - and _, at most 40)", name);
    }
    lua_getfield(state, LUA_REGISTRYINDEX, c_importedKey);
    lua_getfield(state, -1, name);
    if (lua_toboolean(state, -1) != 0)
    {
        return 0; // imported before (or being imported: a cycle ends here)
    }
    lua_pop(state, 1);
    lua_pushboolean(state, 1);
    lua_setfield(state, -2, name);
    lua_pop(state, 1);

    bool found = false;
    m_moduleText.clear();
    if (std::strcmp(name, "constants") == 0)
    {
        found = true; // the constants are globals already
    }
    else if (m_modules != nullptr)
    {
        found = m_modules->find(name, m_moduleText);
    }
    if (!found)
    {
        return luaL_error(state, "import: no module '%s'", name);
    }
    lua_pushlstring(state, m_moduleText.data(), m_moduleText.size());
    size_t textLength = 0;
    const char* text = lua_tolstring(state, -1, &textLength);
    char chunk[64];
    std::snprintf(chunk, sizeof(chunk), "=%s", name);
    if (luaL_loadbufferx(state, text, textLength, chunk, "t") != LUA_OK)
    {
        return lua_error(state);
    }
    lua_call(state, 0, 0);
    return 0;
}

}
