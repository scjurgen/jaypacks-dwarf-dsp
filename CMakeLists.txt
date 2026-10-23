cmake_minimum_required(VERSION 3.21)

# One project for everything: DSP unit tests, the Mac host tests, the LV2 plugins
# (Mac and, inside the docker toolchain, Dwarf) and the documentation explore programs.
# The device firmware toolchain is GCC 9.4 (moddwarf-new), so the whole code base is C++17.

project(jaypacks-dwarf-dsp C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(JP_BUILD_TESTS "Build the unit and host tests" ON)
option(JP_BUILD_PLUGINS "Build the LV2 plugins" ON)
option(
    JP_BUILD_DOCS
    "Build the documentation explore programs (documentation/)"
    OFF
)

add_compile_options(-Wall -Wextra)

# Lua 5.4 (C) and the scripting core that plugins and tests link
add_subdirectory(third_party/lua)
add_subdirectory(scripting)

if(JP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests/unit)
endif()

if(JP_BUILD_PLUGINS)
    add_subdirectory(plugins)
endif()

if(JP_BUILD_TESTS AND JP_BUILD_PLUGINS)
    add_subdirectory(tests/host)
endif()

if(JP_BUILD_DOCS)
    add_subdirectory(documentation)
endif()
