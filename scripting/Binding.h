#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace jp::script
{

/// Most values (positional arguments plus table fields) one command can have.
inline constexpr size_t c_maxValues{12};

/// Longest text value, in characters.
inline constexpr size_t c_maxTextLength{31};

/// How a script value is read and checked.
enum class ValueKind : uint8_t
{
    Number,  ///< a finite number, clamped to [minimum, maximum]
    Integer, ///< a whole number inside [minimum, maximum]; a value outside drops the call
    Flag,    ///< true or false, stored as 1 or 0
    Text     ///< a string of at most c_maxTextLength characters
};

/**
 * @brief One positional argument or one field of the table argument of a command.
 *
 * A non-finite number and an integer outside its range drop the whole call with a warning in the script log (the
 * previous setting stays); a wrong Lua type is an error that stops the script. enumName names the table of constants
 * a script uses for the value (generated documentation only).
 */
struct ValueSpec
{
    const char* name;
    ValueKind kind;
    double minimum;
    double maximum;
    double defaultValue; ///< used when a table field is absent; positional arguments are required
    const char* doc;
    const char* enumName{nullptr};
};

/**
 * @brief A function a script can call: positional arguments, then optionally one table of named fields.
 *
 * SetOscillator(index, { waveform = ..., level = ... }) is one positional argument and a table. Both parts may be
 * empty. The value count (args + fields) is at most c_maxValues.
 */
struct CommandSpec
{
    const char* name;
    const char* doc;
    const ValueSpec* args;
    uint8_t argCount;
    const ValueSpec* fields;
    uint8_t fieldCount;
};

/// A function the script defines and the plugin calls (OnStart, OnNoteOn ...), with numeric arguments.
struct HookSpec
{
    const char* name;
    const char* doc;
    const char* const* argNames;
    uint8_t argCount;
};

struct EnumEntry
{
    const char* name;
    int value;
};

/// A table of named integer constants (global in every script, also available as `import "constants"`).
struct EnumSpec
{
    const char* name;
    const char* doc;
    const EnumEntry* entries;
    uint8_t count;
};

/// A function a script calls to read a number the plugin keeps for it (a knob, a state): GetX() returns the value the
/// plugin last stored under the getter's index (ScriptHost::setValue), 0 until it stores one. Nothing is sent to the
/// plugin; the read is the value as of the call.
struct GetterSpec
{
    const char* name;
    const char* doc;
    double minimum; ///< documentation only: the range the plugin stores
    double maximum;
};

/// Most getters a plugin can have.
inline constexpr size_t c_maxGetters{8};

/// Everything a plugin offers its scripts. The tables live as long as the runtimes that use them. appendix, if set,
/// returns Markdown that the generated API description ends with (lists too long for a table cell, such as the names a
/// text value accepts). getters (at most c_maxGetters) may be left out.
struct BindingSet
{
    const char* title;
    const CommandSpec* commands;
    size_t commandCount;
    const HookSpec* hooks;
    size_t hookCount;
    const EnumSpec* enums;
    size_t enumCount;
    std::string (*appendix)(){nullptr};
    const GetterSpec* getters{nullptr};
    size_t getterCount{0};
};

/**
 * @brief One call of a command as the script made it, checked against its CommandSpec.
 *
 * number[i] and text[i] belong to the i-th value of the spec: the positional arguments first, then the table fields in
 * the order of the spec. given[i] says whether the script supplied it (otherwise number[i] holds the default). Plain
 * arrays only: the object is built in frames that Lua may jump out of.
 */
struct CommandCall
{
    size_t command{0};
    size_t valueCount{0};
    std::array<double, c_maxValues> number{};
    std::array<bool, c_maxValues> given{};
    std::array<std::array<char, c_maxTextLength + 1>, c_maxValues> text{};

    [[nodiscard]] int integer(const size_t i) const noexcept
    {
        return static_cast<int>(number[i]);
    }

    [[nodiscard]] float real(const size_t i) const noexcept
    {
        return static_cast<float>(number[i]);
    }

    [[nodiscard]] bool flag(const size_t i) const noexcept
    {
        return number[i] != 0.0;
    }

    [[nodiscard]] const char* string(const size_t i) const noexcept
    {
        return text[i].data();
    }
};

}
