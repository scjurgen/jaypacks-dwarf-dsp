# Lua 5.4.8 as released (docs/references.md), compiled as C into a static library. Nothing of it is modified; the
# fixed string-hash seed comes from jp_lua_config.h, which is included in front of every source file.
file(GLOB JP_LUA_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.c)

add_library(jp_lua STATIC ${JP_LUA_SOURCES})
set_target_properties(jp_lua PROPERTIES C_STANDARD 99 C_EXTENSIONS ON POSITION_INDEPENDENT_CODE ON)
target_include_directories(jp_lua SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# the released sources are not ours to clean up: no warnings; -include fixes the hash seed
target_compile_options(jp_lua PRIVATE -w -include ${CMAKE_CURRENT_SOURCE_DIR}/jp_lua_config.h)
