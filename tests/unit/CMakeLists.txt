# DSP and pedal unit tests (GoogleTest). Every *_test.cpp below this directory (one folder
# per dsp/ category, plus Plugins/ and Lv2/) is part of the single unit_tests executable;
# ctest sees each TEST as its own test case.

include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
    GIT_SHALLOW TRUE
)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

file(
    GLOB_RECURSE UNIT_TEST_SOURCES
    CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp
)

add_executable(unit_tests ${UNIT_TEST_SOURCES})
target_include_directories(unit_tests PRIVATE ${PROJECT_SOURCE_DIR})
target_include_directories(unit_tests SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party/lv2)
# tests that compare with data files of the repository (documentation/*/*.csv) read them from here
target_compile_definitions(unit_tests PRIVATE JP_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(unit_tests PRIVATE GTest::gtest_main jp_scripting)
gtest_discover_tests(unit_tests DISCOVERY_TIMEOUT 30)
