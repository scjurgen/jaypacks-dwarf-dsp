# The scripting core: the sandboxed Lua runtime and the generated API description. The queues and the script host
# are header-only templates. Plugins and tests link jp_scripting.
add_library(jp_scripting STATIC LuaRuntime.cpp ApiDescription.cpp)
set_target_properties(jp_scripting PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(jp_scripting PUBLIC ${PROJECT_SOURCE_DIR})
target_include_directories(jp_scripting SYSTEM PUBLIC ${PROJECT_SOURCE_DIR}/third_party/lv2)
target_link_libraries(jp_scripting PUBLIC jp_lua)
