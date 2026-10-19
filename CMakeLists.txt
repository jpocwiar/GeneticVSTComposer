cmake_minimum_required(VERSION 3.22)
if(CMAKE_HOST_APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version" FORCE)
endif()
project(GeneticComposer VERSION 2.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CTest)
set(SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/GeneticVSTComposer-JUCE/Source")
add_library(composer_engine STATIC "${SOURCE_DIR}/ComposerEngine.cpp")
target_include_directories(composer_engine PUBLIC "${SOURCE_DIR}")
set_target_properties(composer_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(BUILD_TESTING)
    add_executable(engine_tests tests/EngineTests.cpp)
    target_link_libraries(engine_tests PRIVATE composer_engine)
    add_test(NAME engine COMMAND engine_tests)
endif()

option(COMPOSER_BUILD_PLUGIN "Build JUCE plugins and standalone" ON)
if(NOT COMPOSER_BUILD_PLUGIN)
    return()
endif()
set(JUCE_SOURCE_DIR "" CACHE PATH "Existing JUCE 8.0.13 source checkout")
if(JUCE_SOURCE_DIR)
    add_subdirectory("${JUCE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/juce")
else()
    include(FetchContent)
    FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.13 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(COMPOSER_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND COMPOSER_FORMATS AU)
endif()
juce_add_plugin(GeneticComposer
    COMPANY_NAME "Genetic Composer"
    BUNDLE_ID "com.geneticcomposer.plugin"
    PLUGIN_MANUFACTURER_CODE Gnco
    PLUGIN_CODE Gnc2
    PRODUCT_NAME "Genetic Composer"
    DESCRIPTION "Evolutionary MIDI composition instrument"
    IS_SYNTH TRUE IS_MIDI_EFFECT FALSE
    NEEDS_MIDI_INPUT TRUE NEEDS_MIDI_OUTPUT TRUE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Tools
    FORMATS ${COMPOSER_FORMATS})
juce_generate_juce_header(GeneticComposer)
target_sources(GeneticComposer PRIVATE
    "${SOURCE_DIR}/PluginProcessor.cpp" "${SOURCE_DIR}/PluginProcessor.h"
    "${SOURCE_DIR}/PluginEditor.cpp" "${SOURCE_DIR}/PluginEditor.h")
target_compile_definitions(GeneticComposer PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(GeneticComposer PRIVATE composer_engine juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags)

if(APPLE)
    foreach(format VST3 AU Standalone)
        # Sign after bundle resources/manifests are generated. This is a local
        # ad-hoc signature; release distribution needs a Developer ID identity.
        add_custom_command(TARGET GeneticComposer_${format} POST_BUILD
            COMMAND /usr/bin/codesign --force --sign - "$<TARGET_BUNDLE_DIR:GeneticComposer_${format}>"
            VERBATIM)
    endforeach()
endif()

if(BUILD_TESTING)
    add_executable(plugin_tests)
    set_target_properties(plugin_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugin_tests_artefacts/$<CONFIG>")
    target_sources(plugin_tests PRIVATE tests/PluginTests.cpp tests/WorkflowTests.cpp)
    target_link_libraries(plugin_tests PRIVATE GeneticComposer juce::juce_audio_utils)
    target_include_directories(plugin_tests PRIVATE "${SOURCE_DIR}" "${CMAKE_BINARY_DIR}/GeneticComposer_artefacts/JuceLibraryCode")
    add_test(NAME plugin COMMAND plugin_tests)
endif()
