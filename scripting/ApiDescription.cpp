#include "scripting/ApiDescription.h"

#include <cstdio>

namespace jp::script
{

namespace
{

std::string number(const double value)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%g", value);
    return buffer;
}

const char* kindName(const ValueKind kind)
{
    switch (kind)
    {
        case ValueKind::Number:
            return "number";
        case ValueKind::Integer:
            return "whole number";
        case ValueKind::Flag:
            return "true or false";
        case ValueKind::Text:
            return "string";
    }
    return "";
}

std::string range(const ValueSpec& spec)
{
    switch (spec.kind)
    {
        case ValueKind::Number:
        case ValueKind::Integer:
            return number(spec.minimum) + " to " + number(spec.maximum);
        case ValueKind::Text:
            return "up to " + std::to_string(c_maxTextLength) + " characters";
        case ValueKind::Flag:
            return "";
    }
    return "";
}

std::string meaning(const ValueSpec& spec)
{
    std::string text = spec.doc;
    if (spec.enumName != nullptr)
    {
        text += std::string(" (a ") + spec.enumName + " constant)";
    }
    return text;
}

std::string signature(const CommandSpec& command)
{
    std::string text = std::string(command.name) + "(";
    for (size_t i = 0; i < command.argCount; ++i)
    {
        text += (i == 0 ? "" : ", ") + std::string(command.args[i].name);
    }
    if (command.fieldCount > 0)
    {
        text += std::string(command.argCount == 0 ? "" : ", ") + "{ fields }";
    }
    return text + ")";
}

}

std::string apiMarkdown(const BindingSet& bindings, const Limits& limits)
{
    std::string out;
    out += std::string("# ") + bindings.title + " scripting API\n\n";
    out += "Generated from the plugin's binding table; do not edit.\n\n";
    out += "## How scripts run\n\n";
    out +=
        "- A script is a Lua 5.4 text file. Its top-level code runs once when the script is loaded; the plugin then\n"
        "  calls the hooks below when events arrive. Commands may be called at the top level and from hooks.\n";
    out += "- Hooks run in a worker thread of the plugin, a few milliseconds after the event; they are never part of\n"
           "  the audio thread, and the sound goes on while a script thinks.\n";
    out +=
        "- A script that fails to load (a syntax error, an error at the top level or in OnStart) is rejected and the\n"
        "  previous one keeps running. An error in a hook ends that call only. The reason is in the log file next to\n"
        "  the script.\n";
    out += "- Limits: " + std::to_string(limits.memoryBytes / 1024) + " kB of memory; a hook call may use " +
           std::to_string(limits.handlerInstructions) + " VM instructions and " + number(limits.handlerMillis) +
           " ms; loading a script (top-level code and OnStart) " + std::to_string(limits.loadInstructions) +
           " instructions and " + number(limits.loadMillis) + " ms. A script over a limit is stopped with an error.\n";
    out += "- Libraries: base (without dofile, loadfile, load, collectgarbage, warn), math, table and string (without\n"
           "  dump). No os, io, package, debug, coroutine, utf8 and no precompiled chunks. `setmetatable` refuses a\n"
           "  metatable with `__gc`. `string.find`, `match`, `gmatch` and `gsub` take a subject of at most 8192 and a\n"
           "  pattern of at most 100 characters.\n";
    out +=
        "- Globals: `Log(...)` writes a line to the log, `clamp(x, lo, hi)`, `lerp(a, b, t)`, and `import \"name\"`\n"
        "  runs another script file once (letters, digits, - and _ only). `math.random` gives the same numbers in\n"
        "  every run.\n";
    out += "- A command that gets an unusable value (not finite, not a whole number, out of range) is ignored and says "
           "so\n"
           "  in the log; a wrong type is an error. Fields the command does not know are reported and ignored.\n\n";

    out += "## Commands\n\n";
    for (size_t i = 0; i < bindings.commandCount; ++i)
    {
        const CommandSpec& command = bindings.commands[i];
        out += "### " + signature(command) + "\n\n" + command.doc + "\n\n";
        if (command.argCount > 0)
        {
            out += "| Argument | Type | Range | Meaning |\n|---|---|---|---|\n";
            for (size_t k = 0; k < command.argCount; ++k)
            {
                const ValueSpec& spec = command.args[k];
                out += std::string("| `") + spec.name + "` | " + kindName(spec.kind) + " | " + range(spec) + " | " +
                       meaning(spec) + " |\n";
            }
            out += "\n";
        }
        if (command.fieldCount > 0)
        {
            out += "Fields of the table (all optional; a missing field takes its default):\n\n";
            out += "| Field | Type | Range | Default | Meaning |\n|---|---|---|---|---|\n";
            for (size_t k = 0; k < command.fieldCount; ++k)
            {
                const ValueSpec& spec = command.fields[k];
                out += std::string("| `") + spec.name + "` | " + kindName(spec.kind) + " | " + range(spec) + " | " +
                       (spec.kind == ValueKind::Text ? std::string("") : number(spec.defaultValue)) + " | " +
                       meaning(spec) + " |\n";
            }
            out += "\n";
        }
    }

    if (bindings.getterCount > 0)
    {
        out += "## Getters\n\nFunctions without arguments that return a number the plugin keeps for scripts, as of "
               "the call.\n\n";
        for (size_t i = 0; i < bindings.getterCount; ++i)
        {
            const GetterSpec& getter = bindings.getters[i];
            out += std::string("### ") + getter.name + "()\n\n" + getter.doc + " Range " + number(getter.minimum) +
                   " to " + number(getter.maximum) + ".\n\n";
        }
    }

    out += "## Hooks\n\nDefine a global function of this name and the plugin calls it. A hook you do not define is "
           "simply not called.\n\n";
    for (size_t i = 0; i < bindings.hookCount; ++i)
    {
        const HookSpec& hook = bindings.hooks[i];
        out += std::string("### ") + hook.name + "(";
        for (size_t k = 0; k < hook.argCount; ++k)
        {
            out += (k == 0 ? "" : ", ") + std::string(hook.argNames[k]);
        }
        out += std::string(")\n\n") + hook.doc + "\n\n";
    }

    out += "## Constants\n\nTables of named whole numbers, global in every script (`import \"constants\"` is accepted "
           "and does nothing).\n\n";
    for (size_t i = 0; i < bindings.enumCount; ++i)
    {
        const EnumSpec& spec = bindings.enums[i];
        out += std::string("### ") + spec.name + "\n\n" + spec.doc + "\n\n";
        for (size_t k = 0; k < spec.count; ++k)
        {
            out += std::string("- `") + spec.name + "." + spec.entries[k].name +
                   "` = " + std::to_string(spec.entries[k].value) + "\n";
        }
        out += "\n";
    }
    if (bindings.appendix != nullptr)
    {
        out += bindings.appendix();
    }
    return out;
}

std::string constantsLua(const BindingSet& bindings)
{
    std::string out = std::string("-- ") + bindings.title +
                      ": named constants. Generated from the plugin's binding table.\n"
                      "-- They are global in every script already; `import \"constants\"` does nothing.\n";
    for (size_t i = 0; i < bindings.enumCount; ++i)
    {
        const EnumSpec& spec = bindings.enums[i];
        out += std::string("\n-- ") + spec.doc + "\n" + spec.name + " = {\n";
        for (size_t k = 0; k < spec.count; ++k)
        {
            out += std::string("    ") + spec.entries[k].name + " = " + std::to_string(spec.entries[k].value) + ",\n";
        }
        out += "}\n";
    }
    return out;
}

}
