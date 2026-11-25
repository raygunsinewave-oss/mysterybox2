cmake_minimum_required(VERSION 3.22)

# Universal binary + deployment target must be set before project()
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
    if(NOT CMAKE_OSX_ARCHITECTURES)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary")
    endif()
endif()

project(MysteryBoxSynth VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(MBX_BUILD_PLUGIN "Build the JUCE plugin (VST3/AU/Standalone)" ON)
option(MBX_BUILD_TESTS  "Build Catch2 unit tests" ON)
option(MBX_BUILD_TOOLS  "Build the preset validator / render tools" ON)
option(MBX_REGENERATE_PRESETS "Re-run the preset generator + validator at build time" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(MbxDependencies)

if(MSVC)
    add_compile_options(/MP)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>") # static CRT: no runtime installs needed
endif()

#------------------------------------------------------------------------------
# Pure C++ sound engine (no JUCE): shared by the plugin, tests and tools
#------------------------------------------------------------------------------
add_library(mbx_engine STATIC
    Source/dsp/Wavetables.cpp
    Source/dsp/Oscillator.cpp
    Source/dsp/Effects.cpp
    Source/engine/SynthEngine.cpp
    Source/engine/SfxEngine.cpp
    Source/presets/PresetLibrary.cpp)
target_include_directories(mbx_engine PUBLIC Source)
target_compile_features(mbx_engine PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(mbx_engine PRIVATE /O2 /fp:fast /W3)
else()
    target_compile_options(mbx_engine PRIVATE $<$<CONFIG:Release,RelWithDebInfo>:-O3> -ffast-math -fno-finite-math-only -Wall -Wextra -Wno-unused-parameter)
endif()
find_package(Threads REQUIRED)
target_link_libraries(mbx_engine PUBLIC Threads::Threads)

#------------------------------------------------------------------------------
# Tools
#------------------------------------------------------------------------------
if(MBX_BUILD_TOOLS)
    add_executable(mbx_validate_presets tools/validate_presets.cpp)
    target_link_libraries(mbx_validate_presets PRIVATE mbx_engine)
    add_executable(mbx_render tools/render_preset.cpp)
    target_link_libraries(mbx_render PRIVATE mbx_engine)
endif()

set(MBX_PRESETS_BIN "${CMAKE_CURRENT_SOURCE_DIR}/Resources/presets.bin")
if(MBX_REGENERATE_PRESETS AND MBX_BUILD_TOOLS)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_custom_command(
        OUTPUT "${MBX_PRESETS_BIN}"
        COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_presets.py"
                --validator "$<TARGET_FILE:mbx_validate_presets>" --out "${MBX_PRESETS_BIN}"
        DEPENDS tools/generate_presets.py Source/engine/EngineParams.h mbx_validate_presets
        COMMENT "Generating + validating the 2,500-instrument library")
    add_custom_target(mbx_presets DEPENDS "${MBX_PRESETS_BIN}")
endif()

#------------------------------------------------------------------------------
# Tests
#------------------------------------------------------------------------------
if(MBX_BUILD_TESTS)
    enable_testing()
    add_executable(mbx_tests
        tests/test_dsp.cpp
        tests/test_engine.cpp
        tests/test_rng.cpp
        tests/test_presets.cpp)
    target_link_libraries(mbx_tests PRIVATE mbx_engine Catch2::Catch2WithMain)
    target_compile_definitions(mbx_tests PRIVATE MBX_PRESETS_PATH="${MBX_PRESETS_BIN}")
    add_test(NAME mbx_tests COMMAND mbx_tests)
endif()

#------------------------------------------------------------------------------
# The plugin
#------------------------------------------------------------------------------
if(MBX_BUILD_PLUGIN)
    include(MbxPlugin)
endif()
