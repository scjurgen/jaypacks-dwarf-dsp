#pragma once

#include <cstdlib>
#include <string>

namespace jp::script
{

/**
 * @brief The folder of the user's scripts for a plugin: $JP_SCRIPT_DIR/<plugin> if that variable is set, otherwise
 * ~/Library/Application Support/jaypacks/scripts/<plugin> on a Mac and /data/jaypacks/scripts/<plugin> on the Dwarf
 * (the partition that holds the user's data; reachable by ssh over USB). The plugin creates it when it first writes a
 * log there.
 */
inline std::string userScriptDirectory(const std::string& plugin)
{
    const char* configured = std::getenv("JP_SCRIPT_DIR");
    if (configured != nullptr && *configured != '\0')
    {
        return std::string(configured) + "/" + plugin;
    }
#if defined(__APPLE__)
    const char* home = std::getenv("HOME");
    return std::string(home != nullptr ? home : ".") + "/Library/Application Support/jaypacks/scripts/" + plugin;
#else
    return "/data/jaypacks/scripts/" + plugin;
#endif
}

}
