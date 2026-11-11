cmake_minimum_required(VERSION 3.22)

# macOS: universal binary (Apple Silicon + Intel), macOS 11 or newer.
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")
# Windows: link the runtime statically so no Visual C++ redistributable is needed.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(AMVScope VERSION 0.8.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(AMVSCOPE_BUILD_PLUGIN "Build the VST3 plugin (downloads JUCE)" ON)
option(AMVSCOPE_BUILD_TESTS "Build the unit tests" ON)
option(AMVSCOPE_COPY_AFTER_BUILD "Copy the VST3 into the system plugin folder after building" OFF)

# DSP core: plain C++, no JUCE, so it can be tested without a DAW.
add_library(amvscope_dsp INTERFACE)
target_include_directories(amvscope_dsp INTERFACE src)

if(AMVSCOPE_BUILD_PLUGIN)
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.15
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)

    juce_add_plugin(AMVScope
        PRODUCT_NAME "AMVScope"
        COMPANY_NAME "AMV"
        COMPANY_WEBSITE "https://github.com/airamvlad/amv-scope"
        COMPANY_COPYRIGHT "AGPL-3.0"
        BUNDLE_ID "com.amv.amvscope"
        PLUGIN_MANUFACTURER_CODE Amvs
        PLUGIN_CODE Amv1
        FORMATS VST3
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        VST3_CATEGORIES Fx Analyzer
        COPY_PLUGIN_AFTER_BUILD ${AMVSCOPE_COPY_AFTER_BUILD})

    target_sources(AMVScope PRIVATE
        src/plugin/PluginProcessor.cpp
        src/plugin/PluginEditor.cpp
        src/plugin/SharedMemory.cpp)

    # Shared memory (shm_open) lives in librt on older Linux systems.
    if(UNIX AND NOT APPLE)
        target_link_libraries(AMVScope PRIVATE rt)
    endif()

    target_compile_definitions(AMVScope PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    target_link_libraries(AMVScope
        PRIVATE
            amvscope_dsp
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endif()

if(AMVSCOPE_BUILD_TESTS)
    enable_testing()
    add_executable(amvscope_dsp_tests tests/DspTests.cpp)
    target_link_libraries(amvscope_dsp_tests PRIVATE amvscope_dsp)
    add_test(NAME amvscope_dsp_tests COMMAND amvscope_dsp_tests)

    if(AMVSCOPE_BUILD_PLUGIN)
        # Runs the real plugin processor offline, without a DAW.
        add_executable(amvscope_plugin_tests tests/PluginTests.cpp)
        target_include_directories(amvscope_plugin_tests PRIVATE src
            $<TARGET_PROPERTY:AMVScope,INCLUDE_DIRECTORIES>)
        target_compile_definitions(amvscope_plugin_tests PRIVATE
            $<TARGET_PROPERTY:AMVScope,COMPILE_DEFINITIONS>)
        target_link_libraries(amvscope_plugin_tests PRIVATE AMVScope)
        add_test(NAME amvscope_plugin_tests COMMAND amvscope_plugin_tests)
    endif()
endif()
