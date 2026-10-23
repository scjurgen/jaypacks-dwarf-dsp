# LV2 plugins. Per plugin: generator/jpgen.py turns plugins/<name>/<name>.json into
# gen/<name>/ports.h and the TTL files of the bundle, lv2/PluginEntry.cpp with that ports.h
# becomes jaypacks-<name>.so, and both land in <build>/lv2/jaypacks-<name>.lv2/.
# With JP_GRAPHICS, jpgen.py also writes the modgui text files and generator/graphics.py the
# modgui images.

set(JP_PLUGINS
    shipahoy
    probe
    siggen
    meter
    eigenghost
    jaystwin
    morphexsynth
    midiscript
)
# plugins with Lua scripts: they link the scripting core, ship plugins/<name>/scripts/ in the bundle and have a
# ScriptDoc.cpp that writes API.md and constants.lua there
set(JP_SCRIPTED_PLUGINS morphexsynth midiscript shipahoy)

find_package(Python3 3.6 REQUIRED COMPONENTS Interpreter)

set(JP_GENERATOR ${PROJECT_SOURCE_DIR}/generator/jpgen.py)
# recursive: the modgui templates live in generator/templates/modgui/
file(
    GLOB_RECURSE JP_GENERATOR_TEMPLATES
    CONFIGURE_DEPENDS
    ${PROJECT_SOURCE_DIR}/generator/templates/*
)
set(JP_LV2_DIR ${CMAKE_BINARY_DIR}/lv2)
set(JP_LV2_DIR ${JP_LV2_DIR} PARENT_SCOPE)

# modgui images need numpy and Pillow from the repository venv, so they are made on the Mac;
# the Dwarf build in docker takes them from there
if(APPLE)
    set(jp_graphics_default ON)
else()
    set(jp_graphics_default OFF)
endif()
option(
    JP_GRAPHICS
    "Generate the modgui (face, knob filmstrip, footswitch, screenshot, thumbnail; numpy and Pillow)"
    ${jp_graphics_default}
)
set(JP_GRAPHICS_PYTHON
    ""
    CACHE FILEPATH
    "Python with numpy and Pillow for generator/graphics.py; empty: .venv/bin/python3 (dev-scripts/dev-venv.sh)"
)
if(JP_GRAPHICS)
    if(JP_GRAPHICS_PYTHON)
        set(jp_graphics_python ${JP_GRAPHICS_PYTHON})
    else()
        set(jp_graphics_python ${PROJECT_SOURCE_DIR}/.venv/bin/python3)
    endif()
    execute_process(
        COMMAND ${jp_graphics_python} -c "import numpy, PIL"
        RESULT_VARIABLE jp_pil_missing
        OUTPUT_QUIET
        ERROR_QUIET
    )
    if(jp_pil_missing)
        message(
            FATAL_ERROR
            "JP_GRAPHICS: no numpy or Pillow for ${jp_graphics_python}. Run dev-scripts/dev-venv.sh, "
            "or pass -DJP_GRAPHICS_PYTHON=<python with numpy and Pillow>, or -DJP_GRAPHICS=OFF "
            "(bundles without modgui)."
        )
    endif()
    set(JP_GRAPHICS_PYTHON_EXE ${jp_graphics_python})
    set(JP_GRAPHICS_PYTHON_EXE ${jp_graphics_python} PARENT_SCOPE)
    set(JP_GRAPHICS_SCRIPT ${PROJECT_SOURCE_DIR}/generator/graphics.py)
    file(
        GLOB_RECURSE JP_GRAPHICS_ASSETS
        CONFIGURE_DEPENDS
        ${PROJECT_SOURCE_DIR}/generator/assets/*
    )
    list(APPEND JP_GRAPHICS_ASSETS ${PROJECT_SOURCE_DIR}/generator/style.py)
endif()

function(jp_add_plugin name)
    set(json ${CMAKE_CURRENT_SOURCE_DIR}/${name}/${name}.json)
    set(gen_dir ${CMAKE_BINARY_DIR}/gen/${name})
    set(bundle_dir ${JP_LV2_DIR}/jaypacks-${name}.lv2)
    set(target jaypacks-${name})

    file(READ ${json} json_text)
    string(
        JSON preset_count
        ERROR_VARIABLE no_presets
        LENGTH "${json_text}"
        presets
    )
    set(generated
        ${gen_dir}/ports.h
        ${bundle_dir}/manifest.ttl
        ${bundle_dir}/${name}.ttl
    )
    if(NOT no_presets AND preset_count GREATER 0)
        list(APPEND generated ${bundle_dir}/presets.ttl)
    endif()
    set(modgui_flag "")
    if(JP_GRAPHICS)
        set(modgui_flag --modgui)
        list(
            APPEND generated
            ${bundle_dir}/modgui.ttl
            ${bundle_dir}/modgui/jaypacks-${name}.html
            ${bundle_dir}/modgui/jaypacks-${name}.css
        )
    endif()

    # The generator rewrites a file only when its content changes, so a stamp file carries
    # the dependency; it re-runs when the JSON, the generator or a template changes.
    set(stamp ${gen_dir}/jpgen.stamp)
    add_custom_command(
        OUTPUT ${stamp}
        BYPRODUCTS ${generated}
        COMMAND
            Python3::Interpreter ${JP_GENERATOR} ${json} --header-dir ${gen_dir}
            --bundle-dir ${bundle_dir} ${modgui_flag}
        COMMAND ${CMAKE_COMMAND} -E touch ${stamp}
        DEPENDS ${json} ${JP_GENERATOR} ${JP_GENERATOR_TEMPLATES}
        COMMENT "jpgen ${name}"
        VERBATIM
    )
    set(gen_stamps ${stamp})
    if(JP_GRAPHICS)
        set(graphics_stamp ${gen_dir}/graphics.stamp)
        add_custom_command(
            OUTPUT ${graphics_stamp}
            BYPRODUCTS
                ${bundle_dir}/modgui/face.png
                ${bundle_dir}/modgui/screenshot.png
                ${bundle_dir}/modgui/thumbnail.png
                ${bundle_dir}/modgui/knobs/boxy/white.png
                ${bundle_dir}/modgui/pedals/onoff_white.png
            COMMAND
                ${JP_GRAPHICS_PYTHON_EXE} ${JP_GRAPHICS_SCRIPT} ${json}
                --bundle-dir ${bundle_dir}
            COMMAND ${CMAKE_COMMAND} -E touch ${graphics_stamp}
            DEPENDS
                ${json}
                ${JP_GRAPHICS_SCRIPT}
                ${JP_GENERATOR}
                ${JP_GRAPHICS_ASSETS}
            COMMENT "graphics ${name}"
            VERBATIM
        )
        list(APPEND gen_stamps ${graphics_stamp})
    endif()
    add_custom_target(${target}-gen DEPENDS ${gen_stamps})

    add_library(${target} MODULE ${PROJECT_SOURCE_DIR}/lv2/PluginEntry.cpp)
    add_dependencies(${target} ${target}-gen)
    target_include_directories(
        ${target}
        PRIVATE ${gen_dir} ${PROJECT_SOURCE_DIR}
    )
    target_include_directories(
        ${target}
        SYSTEM
        PRIVATE ${PROJECT_SOURCE_DIR}/third_party/lv2
    )
    set_target_properties(
        ${target}
        PROPERTIES
            PREFIX ""
            SUFFIX ".so"
            LIBRARY_OUTPUT_DIRECTORY ${bundle_dir}
            CXX_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN ON
    )
    if(NOT APPLE)
        # lv2/exports.map: only lv2_descriptor, also for std:: templates emitted out of line
        target_link_options(
            ${target}
            PRIVATE
                -Wl,--no-undefined
                -Wl,--version-script=${PROJECT_SOURCE_DIR}/lv2/exports.map
        )
        set_property(
            TARGET ${target}
            APPEND
            PROPERTY LINK_DEPENDS ${PROJECT_SOURCE_DIR}/lv2/exports.map
        )
    endif()
    set_property(GLOBAL APPEND PROPERTY JP_PLUGIN_TARGETS ${target})

    if(name IN_LIST JP_SCRIPTED_PLUGINS)
        target_link_libraries(${target} PRIVATE jp_scripting)
        # the scripts of the plugin and the documents about its script API go into the bundle (not part of the
        # plugin target: the Dwarf cross-build builds that target alone and takes both from the Mac bundle)
        file(
            GLOB_RECURSE script_files
            CONFIGURE_DEPENDS
            ${CMAKE_CURRENT_SOURCE_DIR}/${name}/scripts/*.lua
            ${CMAKE_CURRENT_SOURCE_DIR}/${name}/scripts/*.md
        )
        set(scripts_stamp ${gen_dir}/scripts.stamp)
        add_custom_command(
            OUTPUT ${scripts_stamp}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${bundle_dir}/scripts
            COMMAND
                ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_SOURCE_DIR}/${name}/scripts
                ${bundle_dir}/scripts
            COMMAND ${CMAKE_COMMAND} -E touch ${scripts_stamp}
            DEPENDS ${script_files}
            COMMENT "scripts of ${name}"
            VERBATIM
        )
        add_executable(
            ${target}-scriptdoc
            ${CMAKE_CURRENT_SOURCE_DIR}/${name}/ScriptDoc.cpp
        )
        target_include_directories(
            ${target}-scriptdoc
            PRIVATE ${PROJECT_SOURCE_DIR}
        )
        target_link_libraries(${target}-scriptdoc PRIVATE jp_scripting)
        set_target_properties(
            ${target}-scriptdoc
            PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tools
        )
        set(scriptdoc_stamp ${gen_dir}/scriptdoc.stamp)
        add_custom_command(
            OUTPUT ${scriptdoc_stamp}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${bundle_dir}/scripts
            COMMAND ${target}-scriptdoc ${bundle_dir}/scripts
            COMMAND ${CMAKE_COMMAND} -E touch ${scriptdoc_stamp}
            DEPENDS ${target}-scriptdoc
            COMMENT "script API of ${name}"
            VERBATIM
        )
        add_custom_target(
            ${target}-scripts
            ALL
            DEPENDS ${scripts_stamp} ${scriptdoc_stamp}
        )
        set_property(GLOBAL APPEND PROPERTY JP_SCRIPT_TARGETS ${target}-scripts)
    endif()

    # memory probe (tests/host/MemoryProbe.cpp): the same entry point in an executable of its
    # own that counts what the plugin allocates; ctest fails when run() allocates
    if(JP_BUILD_TESTS)
        add_executable(
            ${target}-memprobe
            ${PROJECT_SOURCE_DIR}/lv2/PluginEntry.cpp
            ${PROJECT_SOURCE_DIR}/tests/host/MemoryProbe.cpp
        )
        add_dependencies(${target}-memprobe ${target}-gen)
        target_include_directories(
            ${target}-memprobe
            PRIVATE ${gen_dir} ${PROJECT_SOURCE_DIR}
        )
        target_include_directories(
            ${target}-memprobe
            SYSTEM
            PRIVATE ${PROJECT_SOURCE_DIR}/third_party/lv2
        )
        if(name IN_LIST JP_SCRIPTED_PLUGINS)
            target_link_libraries(${target}-memprobe PRIVATE jp_scripting)
        endif()
        set_target_properties(
            ${target}-memprobe
            PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/memprobe
        )
        add_test(NAME host.memory.${name} COMMAND ${target}-memprobe --check)
    endif()
endfunction()

foreach(plugin IN LISTS JP_PLUGINS)
    jp_add_plugin(${plugin})
endforeach()

if(JP_BUILD_TESTS)
    add_test(
        NAME generator.unittest
        COMMAND
            Python3::Interpreter -m unittest discover -s
            ${PROJECT_SOURCE_DIR}/generator -p "test_jpgen.py"
    )
    set_tests_properties(
        generator.unittest
        PROPERTIES ENVIRONMENT "JP_SCRATCH_DIR=${CMAKE_BINARY_DIR}/scratch"
    )
    add_test(
        NAME tools.unittest
        COMMAND
            Python3::Interpreter -m unittest discover -s
            ${PROJECT_SOURCE_DIR}/tools -p "test_*.py"
    )
    set_tests_properties(
        tools.unittest
        PROPERTIES ENVIRONMENT "JP_SCRATCH_DIR=${CMAKE_BINARY_DIR}/scratch"
    )
    if(JP_GRAPHICS)
        add_test(
            NAME generator.graphics
            COMMAND
                ${JP_GRAPHICS_PYTHON_EXE} -m unittest discover -s
                ${PROJECT_SOURCE_DIR}/generator -p "test_graphics.py"
        )
        set_tests_properties(
            generator.graphics
            PROPERTIES ENVIRONMENT "JP_SCRATCH_DIR=${CMAKE_BINARY_DIR}/scratch"
        )
    endif()
endif()
