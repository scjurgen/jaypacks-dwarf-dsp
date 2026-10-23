#pragma once

#include <cctype>
#include <cstring>
#include <set>
#include <string>

#include "scripting/Binding.h"

namespace jp::script
{

/// Whether a name is a reserved word of Lua 5.4: `{ repeat = true }` is a syntax error, so no field may be called that.
inline bool isLuaKeyword(const char* name)
{
    for (const char* keyword :
         {"and", "break", "do",  "else", "elseif", "end",    "false",  "for",  "function", "goto",  "if",
          "in",  "local", "nil", "not",  "or",     "repeat", "return", "then", "true",     "until", "while"})
    {
        if (std::strcmp(name, keyword) == 0)
        {
            return true;
        }
    }
    return false;
}

/// Whether a name can be written in a script as a variable, a function or a table field.
inline bool isLuaName(const char* name)
{
    if (name == nullptr || *name == '\0' || std::isdigit(static_cast<unsigned char>(*name)) != 0 || isLuaKeyword(name))
    {
        return false;
    }
    for (const char* c = name; *c != '\0'; ++c)
    {
        if (std::isalnum(static_cast<unsigned char>(*c)) == 0 && *c != '_')
        {
            return false;
        }
    }
    return true;
}

/**
 * @brief Looks for what is wrong with a plugin's binding tables: names a script cannot write (a Lua reserved word, a
 * character that is not a letter, a digit or an underscore), the same name twice, more values in a command than
 * c_maxValues, a range turned round, a default outside it, an enum name that no table has. Returns the first problem as
 * text, empty if there is none. For the tests of every plugin that has scripts.
 */
inline std::string checkBindings(const BindingSet& bindings)
{
    std::set<std::string> globals;
    const auto global = [&](const char* name, const std::string& what) -> std::string
    {
        if (!isLuaName(name))
        {
            return what + " '" + (name != nullptr ? name : "(null)") + "' is not a name a script can write";
        }
        if (!globals.insert(name).second)
        {
            return what + " '" + name + "' is used twice";
        }
        return {};
    };
    std::set<std::string> enums;
    for (size_t i = 0; i < bindings.enumCount; ++i)
    {
        const EnumSpec& spec = bindings.enums[i];
        if (const std::string problem = global(spec.name, "constants table"); !problem.empty())
        {
            return problem;
        }
        enums.insert(spec.name);
        std::set<std::string> entries;
        for (size_t k = 0; k < spec.count; ++k)
        {
            if (!isLuaName(spec.entries[k].name) || !entries.insert(spec.entries[k].name).second)
            {
                return std::string("constant '") + spec.name + "." + spec.entries[k].name +
                       "' is not a name or is used twice";
            }
        }
    }
    for (size_t i = 0; i < bindings.commandCount; ++i)
    {
        const CommandSpec& command = bindings.commands[i];
        if (const std::string problem = global(command.name, "command"); !problem.empty())
        {
            return problem;
        }
        if (static_cast<size_t>(command.argCount) + command.fieldCount > c_maxValues)
        {
            return std::string("command ") + command.name + " has more than " + std::to_string(c_maxValues) + " values";
        }
        std::set<std::string> names;
        for (size_t k = 0; k < static_cast<size_t>(command.argCount) + command.fieldCount; ++k)
        {
            const ValueSpec& value = k < command.argCount ? command.args[k] : command.fields[k - command.argCount];
            const std::string where = std::string("command ") + command.name + ", value ";
            if (!isLuaName(value.name) || !names.insert(value.name).second)
            {
                return where + "'" + (value.name != nullptr ? value.name : "(null)") +
                       "' is not a name or is used twice";
            }
            if ((value.kind == ValueKind::Number || value.kind == ValueKind::Integer) &&
                (value.minimum > value.maximum ||
                 (k >= command.argCount && (value.defaultValue < value.minimum || value.defaultValue > value.maximum))))
            {
                return where + "'" + value.name + "' has a range that is turned round or a default outside it";
            }
            if (value.enumName != nullptr && enums.count(value.enumName) == 0)
            {
                return where + "'" + value.name + "' names the constants table '" + value.enumName +
                       "', which does not exist";
            }
        }
    }
    if (bindings.getterCount > c_maxGetters)
    {
        return "more than " + std::to_string(c_maxGetters) + " getters";
    }
    for (size_t i = 0; i < bindings.getterCount; ++i)
    {
        if (const std::string problem = global(bindings.getters[i].name, "getter"); !problem.empty())
        {
            return problem;
        }
    }
    for (size_t i = 0; i < bindings.hookCount; ++i)
    {
        const HookSpec& hook = bindings.hooks[i];
        if (const std::string problem = global(hook.name, "hook"); !problem.empty())
        {
            return problem;
        }
        for (size_t k = 0; k < hook.argCount; ++k)
        {
            if (!isLuaName(hook.argNames[k]))
            {
                return std::string("hook ") + hook.name + ", argument '" + hook.argNames[k] + "' is not a name";
            }
        }
    }
    return {};
}

}
