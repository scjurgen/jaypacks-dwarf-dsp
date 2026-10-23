// Writes the documents about the script API of Ship Ahoy into a folder: API.md (everything a script can do, for
// authors and for an LLM that writes scripts) and constants.lua (the named constants as a Lua file). The build puts
// them into the bundle's scripts folder.
#include <cstdio>
#include <fstream>
#include <string>

#include "plugins/shipahoy/ShipAhoyScript.h"
#include "scripting/ApiDescription.h"

int main(const int argc, const char* const* argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "usage: %s <folder>\n", argv[0]);
        return 2;
    }
    const std::string folder = argv[1];
    const jp::script::BindingSet& bindings = jp::ship_ahoy::bindings();
    std::ofstream api(folder + "/API.md", std::ios::binary | std::ios::trunc);
    api << jp::script::apiMarkdown(bindings, jp::script::Limits{});
    std::ofstream constants(folder + "/constants.lua", std::ios::binary | std::ios::trunc);
    constants << jp::script::constantsLua(bindings);
    if (!api || !constants)
    {
        std::fprintf(stderr, "cannot write into %s\n", folder.c_str());
        return 1;
    }
    return 0;
}
