# Host tests: a minimal LV2 host (Lv2Host.h) loads every built bundle from <build>/lv2 with
# dlopen and runs the generic tests, plus plugin-specific integration tests. Every
# *_test.cpp here is part of the single host_tests executable.

file(
    GLOB HOST_TEST_SOURCES
    CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp
)

add_executable(host_tests ${HOST_TEST_SOURCES})
target_include_directories(
    host_tests
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}
)
target_include_directories(
    host_tests
    SYSTEM
    PRIVATE ${PROJECT_SOURCE_DIR}/third_party/lv2
)
target_compile_definitions(host_tests PRIVATE JP_LV2_DIR="${JP_LV2_DIR}")
target_link_libraries(host_tests PRIVATE GTest::gtest_main ${CMAKE_DL_LIBS})

get_property(plugin_targets GLOBAL PROPERTY JP_PLUGIN_TARGETS)
get_property(script_targets GLOBAL PROPERTY JP_SCRIPT_TARGETS)
add_dependencies(host_tests ${plugin_targets} ${script_targets})

# PRE_TEST: the generic tests are instantiated per bundle found in JP_LV2_DIR, so they are
# listed when ctest runs, not when host_tests links (a new plugin alone does not relink it)
gtest_discover_tests(host_tests DISCOVERY_TIMEOUT 30 TEST_PREFIX "host." DISCOVERY_MODE PRE_TEST)

# modgui of every bundle (files, face controls, image sizes); needs only the standard library
if(JP_GRAPHICS)
    find_package(Python3 3.6 REQUIRED COMPONENTS Interpreter)
    add_test(
        NAME host.modgui
        COMMAND
            Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/modgui_check.py ${JP_LV2_DIR}
            --require
    )
endif()

# expected values for the shipahoy device test (tools/device_tests/test_shipahoy.py)
add_executable(shipahoy_expected ${CMAKE_CURRENT_SOURCE_DIR}/ShipAhoyExpected.cpp)
target_include_directories(shipahoy_expected PRIVATE ${PROJECT_SOURCE_DIR})

# expected values for the eigenghost device test (tools/device_tests/test_eigenghost.py)
add_executable(eigenghost_expected ${CMAKE_CURRENT_SOURCE_DIR}/EigenghostExpected.cpp)
target_include_directories(eigenghost_expected PRIVATE ${PROJECT_SOURCE_DIR})

# expected values for the jaystwin device test (tools/device_tests/test_jaystwin.py)
add_executable(jaystwin_expected ${CMAKE_CURRENT_SOURCE_DIR}/JaysTwinExpected.cpp)
target_include_directories(jaystwin_expected PRIVATE ${PROJECT_SOURCE_DIR})

# expected values for the morphexsynth device test (tools/device_tests/test_morphexsynth.py)
add_executable(morphexsynth_expected ${CMAKE_CURRENT_SOURCE_DIR}/MorphexsynthExpected.cpp)
target_include_directories(morphexsynth_expected PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(morphexsynth_expected PRIVATE jp_scripting)
target_compile_definitions(morphexsynth_expected PRIVATE JP_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
