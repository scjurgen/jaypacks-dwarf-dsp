#pragma once

#include <string>

#include "scripting/Binding.h"
#include "scripting/LuaRuntime.h"

namespace jp::script
{

/**
 * @brief The script API of a plugin as Markdown: how scripts run and what they may use, every command with its
 * arguments, ranges and defaults, every hook, every table of constants.
 *
 * Written from the BindingSet (the same table that checks the calls), so it cannot drift from what the plugin does.
 * It is what an author, or an LLM writing a script on the author's machine, reads before writing one.
 */
std::string apiMarkdown(const BindingSet& bindings, const Limits& limits);

/// The tables of constants as a Lua file (`Name = { Entry = 0, ... }`), for an author's editor and for tests; scripts
/// get the same tables as globals.
std::string constantsLua(const BindingSet& bindings);

}
