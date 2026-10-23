#pragma once

#include <array>
#include <cstddef>
#include <cstdlib>
#include <string>

#include "scripting/ScriptDirectory.h"

namespace jp
{

/// The patches that ship with the plugin, as the files of the bundle's scripts folder (<name>.lua), in the order of the
/// Patch port. Their settings are what the scripts make (tests/unit/Plugins/MorphexScript_test.cpp compares them with
/// the patches AbacDsp's morphexsynth was ported with).
inline constexpr size_t c_factoryPatchCount{10};
inline constexpr size_t c_defaultFactoryPatch{7};
inline constexpr std::array<const char*, c_factoryPatchCount> c_factoryPatchNames{
    "4-ever-mini", "airy-pad",   "crystal-bells", "cutting-lead",  "grand-stage",
    "harp-pluck",  "kick-punch", "retro-poly",    "section-brass", "warm-bass"};

/// The Script port selects the user script user-1.lua to user-8.lua of the user's scripts folder (0: the Patch port).
inline constexpr size_t c_userScriptCount{8};

/// The folder of the user's scripts for this plugin (scripting/ScriptDirectory.h).
inline std::string defaultUserScriptDirectory()
{
    return script::userScriptDirectory("morphexsynth");
}

}
