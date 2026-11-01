cmake_minimum_required(VERSION 3.22)
if(CMAKE_HOST_APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version" FORCE)
endif()
project(GeneticComposer VERSION 2.8.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CTest)
set(SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/GeneticVSTComposer-JUCE/Source")
add_library(composer_engine STATIC "${SOURCE_DIR}/ComposerEngine.cpp"
    "${SOURCE_DIR}/ChordGenerator.cpp" "${SOURCE_DIR}/ChordGenerator.h"
    "${SOURCE_DIR}/MelodyAdaptation.cpp"
    "${SOURCE_DIR}/HarmonicSearch.h" "${SOURCE_DIR}/HarmonyModelData.h")
target_include_directories(composer_engine PUBLIC "${SOURCE_DIR}")
set_target_properties(composer_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)
find_package(Threads REQUIRED)
target_link_libraries(composer_engine PUBLIC Threads::Threads)
if(BUILD_TESTING)
    add_executable(engine_tests tests/EngineTests.cpp tests/SearchTests.cpp tests/ChordGeneratorTests.cpp
        tests/MusicalArchitectureTests.cpp tests/MelodyAdaptationTests.cpp)
    target_link_libraries(engine_tests PRIVATE composer_engine)
    # Tests keep Clips/Banks and whole Processors on the main-thread stack; match the
    # 8 MB macOS default instead of the 1 MB Windows one.
    if(MSVC)
        target_link_options(engine_tests PRIVATE /STACK:8388608)
    endif()
    add_test(NAME engine COMMAND engine_tests engine)
    add_test(NAME harmony COMMAND engine_tests harmony)
    add_test(NAME search COMMAND engine_tests search)
    add_test(NAME architecture COMMAND engine_tests architecture)
endif()
# Measurements only. Not part of the default build or CTest.
add_executable(composer_probe EXCLUDE_FROM_ALL "${CMAKE_CURRENT_SOURCE_DIR}/scripts/probe-musical-engine.cpp")
target_link_libraries(composer_probe PRIVATE composer_engine)

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
    "${SOURCE_DIR}/ProcessorPlayback.cpp" "${SOURCE_DIR}/ProcessorState.cpp"
    "${SOURCE_DIR}/PluginEditor.cpp" "${SOURCE_DIR}/PluginEditor.h"
    "${SOURCE_DIR}/PianoRoll.cpp" "${SOURCE_DIR}/PianoRoll.h"
    "${SOURCE_DIR}/ComposerGuide.cpp" "${SOURCE_DIR}/ComposerGuide.h"
    "${SOURCE_DIR}/ComposerTheme.cpp" "${SOURCE_DIR}/ComposerTheme.h"
    "${SOURCE_DIR}/ChordComposerPanel.cpp" "${SOURCE_DIR}/ChordComposerPanel.h"
    "${SOURCE_DIR}/ActivityDisplay.cpp" "${SOURCE_DIR}/ActivityDisplay.h"
    "${SOURCE_DIR}/InterfaceText.cpp" "${SOURCE_DIR}/InterfaceText.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/reverb-core/src/Fdn.cpp")
target_include_directories(GeneticComposer PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/reverb-core/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/libs/dsp-kit/include")
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
    target_sources(plugin_tests PRIVATE tests/PluginTests.cpp tests/WorkflowTests.cpp tests/VisualTelemetryTests.cpp
        tests/MicrotonalTests.cpp tests/UsabilityTests.cpp tests/ChordPluginTests.cpp tests/ChordPanelTests.cpp tests/PlaybackArchitectureTests.cpp tests/RenderMusicalExamples.cpp
        tests/RealtimeTests.cpp tests/AllocationGuard.cpp)
    target_link_libraries(plugin_tests PRIVATE GeneticComposer juce::juce_audio_utils)
    if(MSVC)
        target_link_options(plugin_tests PRIVATE /STACK:8388608)
    endif()
    target_include_directories(plugin_tests PRIVATE "${SOURCE_DIR}" "${CMAKE_BINARY_DIR}/GeneticComposer_artefacts/JuceLibraryCode"
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/reverb-core/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/libs/dsp-kit/include")
    add_test(NAME plugin COMMAND plugin_tests plugin)
    add_test(NAME midi COMMAND plugin_tests midi)
    add_test(NAME playback COMMAND plugin_tests playback)
    add_test(NAME realtime COMMAND plugin_tests realtime)
endif()
