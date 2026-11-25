# Fetches JUCE and Catch2.
# Offline / pre-downloaded builds: pass -DMBX_JUCE_DIR=/path/to/JUCE and/or -DMBX_CATCH2_DIR=/path/to/Catch2
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

set(MBX_JUCE_TAG "8.0.17-security" CACHE STRING "JUCE git tag")
set(MBX_JUCE_DIR "" CACHE PATH "Local JUCE checkout (skips download)")
set(MBX_CATCH2_DIR "" CACHE PATH "Local Catch2 checkout (skips download)")

if(MBX_BUILD_PLUGIN)
    if(MBX_JUCE_DIR)
        add_subdirectory("${MBX_JUCE_DIR}" "${CMAKE_BINARY_DIR}/_deps/juce-build" EXCLUDE_FROM_ALL)
    else()
        FetchContent_Declare(juce
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG ${MBX_JUCE_TAG}
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(juce)
    endif()
endif()

if(MBX_BUILD_TESTS)
    if(MBX_CATCH2_DIR)
        add_subdirectory("${MBX_CATCH2_DIR}" "${CMAKE_BINARY_DIR}/_deps/catch2-build" EXCLUDE_FROM_ALL)
    else()
        FetchContent_Declare(Catch2
            GIT_REPOSITORY https://github.com/catchorg/Catch2.git
            GIT_TAG v3.7.1
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(Catch2)
    endif()
endif()
