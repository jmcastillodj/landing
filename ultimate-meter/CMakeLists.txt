cmake_minimum_required(VERSION 3.22)

project(UltimateMeter VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(ULTIMATEMETER_COPY_AFTER_BUILD "Install the built plugins into the user plugin folders" OFF)

set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/JUCE" CACHE PATH "Path to a JUCE checkout")
add_subdirectory(${JUCE_DIR} JUCE)

# ULTIMATE METER by Jm Castillo is a fork of Grisey by Yulania (GPLv3). It has its own plugin
# and manufacturer codes, so that hosts never confuse it with Grisey or MultiMeter. A host knows
# a plugin by these four-character codes, so do not change them once sessions have been saved.
juce_add_plugin(UltimateMeter
    COMPANY_NAME "Jm Castillo"
    BUNDLE_ID "es.jmcastillo.UltimateMeter"
    PLUGIN_MANUFACTURER_CODE JmCa
    PLUGIN_CODE UMtr
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "ULTIMATE METER"
    DESCRIPTION "ULTIMATE METER by Jm Castillo"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Assets/icon.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/Assets/icon.png"
    # The standalone reads an audio interface. Without this, macOS gives it silence and never asks
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "ULTIMATE METER needs your audio interface input to measure the sound."
    VST3_CATEGORIES Fx Analyzer
    AU_MAIN_TYPE kAudioUnitType_Effect
    AU_EXPORT_PREFIX UltimateMeterAU
    COPY_PLUGIN_AFTER_BUILD ${ULTIMATEMETER_COPY_AFTER_BUILD})

juce_generate_juce_header(UltimateMeter)

target_sources(UltimateMeter PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Views/GoniometerView.cpp
    Source/Views/HistoryView.cpp
    Source/Views/LevelMeters.cpp
    Source/Views/LoudnessSummary.cpp
    Source/Views/LoudnessView.cpp
    Source/Views/SpectrogramView.cpp
    Source/Views/SpectrumView.cpp
    Source/Views/TonalBalanceView.cpp
    Source/Views/LoudnessRadarView.cpp
    Source/Views/StereoPanel.cpp
    Source/Views/ReferenceView.cpp
    Source/Views/CorrelometerView.cpp
    Source/Views/VuMeterView.cpp
    Source/Views/BpmView.cpp
    Source/Views/WaveformView.cpp)

target_compile_definitions(UltimateMeter PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(UltimateMeter
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

#==============================================================================
option(ULTIMATEMETER_BUILD_TESTS "Build the unit tests" ON)

if(ULTIMATEMETER_BUILD_TESTS)
    enable_testing()

    # Adds a console tool that uses the plugin's code. It links against the plugin's
    # shared-code library, which already contains the JUCE modules, so it only borrows
    # that library's include paths and definitions rather than compiling the modules again.
    function(grisey_add_tool name)
        juce_add_console_app(${name} PRODUCT_NAME "${name}")
        target_sources(${name} PRIVATE ${ARGN})
        target_include_directories(${name} PRIVATE
            $<TARGET_PROPERTY:UltimateMeter,INCLUDE_DIRECTORIES>)
        target_compile_definitions(${name} PRIVATE
            $<TARGET_PROPERTY:UltimateMeter,COMPILE_DEFINITIONS>)
        target_link_libraries(${name} PRIVATE UltimateMeter)
    endfunction()

    grisey_add_tool(UltimateMeterTests
        Tests/Main.cpp
        Tests/ProcessorTests.cpp
        Tests/LoudnessTests.cpp
        Tests/SpectrumTests.cpp
        Tests/TimelineTests.cpp
        Tests/TonalTests.cpp)

    add_test(NAME UltimateMeterTests COMMAND UltimateMeterTests)

    # A development tool that saves a picture of the editor running a test signal
    grisey_add_tool(UltimateMeterSnapshot Tests/Snapshot.cpp)
endif()
