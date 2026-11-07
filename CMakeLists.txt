cmake_minimum_required(VERSION 3.22)
# must come before project(): the oldest macOS the plug-in runs on, and a universal binary
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
project(ChoirArranger VERSION 0.7.1 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(CHOIR_BUILD_PLUGIN "Build the JUCE plug-in (AU/VST3/Standalone)" ON)
set(CHOIR_JUCE_PATH "" CACHE PATH "Local JUCE checkout; empty = download JUCE 8.0.4")

# ---- engine: plain C++17, no JUCE --------------------------------------------
add_library(choir_core STATIC
    core/Analysis.cpp core/Chordal.cpp core/Gregorian.cpp core/Smf.cpp core/Notation.cpp core/Harmonize.cpp)
target_include_directories(choir_core PUBLIC core)
set_target_properties(choir_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
# identical floating-point results on every platform (the engine is validated against the prototype)
if(MSVC)
    target_compile_options(choir_core PRIVATE /fp:precise)
else()
    target_compile_options(choir_core PRIVATE -ffp-contract=off)
endif()

add_executable(choir_cli cli/main.cpp)
target_link_libraries(choir_cli PRIVATE choir_core)

# ---- plug-in ----------------------------------------------------------------
if(CHOIR_BUILD_PLUGIN)
    if(CHOIR_JUCE_PATH)
        add_subdirectory(${CHOIR_JUCE_PATH} juce)
    else()
        include(FetchContent)
        FetchContent_Declare(juce
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG 8.0.4
            GIT_SHALLOW ON
            # lets the plug-in window accept regions dragged from Logic (file promises)
            PATCH_COMMAND ${CMAKE_COMMAND} -DPATCH=${CMAKE_CURRENT_SOURCE_DIR}/patches/juce-8.0.4-file-promises.patch
                          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ApplyPatch.cmake
            UPDATE_DISCONNECTED ON)
        FetchContent_MakeAvailable(juce)
    endif()

    juce_add_plugin(ChoirArranger
        COMPANY_NAME "Teilormusic"
        BUNDLE_ID "com.teilormusic.choirarranger"
        PLUGIN_MANUFACTURER_CODE Tlrm
        PLUGIN_CODE Chor
        FORMATS AU VST3 Standalone
        PRODUCT_NAME "Choir Arranger"
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT TRUE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        AU_MAIN_TYPE kAudioUnitType_MusicDevice
        VST3_CATEGORIES Instrument Tools)

    target_sources(ChoirArranger PRIVATE
        plugin/PluginProcessor.cpp plugin/PluginEditor.cpp plugin/PianoRoll.cpp plugin/Look.cpp)
    target_compile_definitions(ChoirArranger PUBLIC
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)
    target_link_libraries(ChoirArranger
        PRIVATE choir_core juce::juce_audio_utils juce::juce_gui_extra
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
               juce::juce_recommended_warning_flags)

    # Choir Arranger Live: the MIDI FX (Logic: MIDI FX slot -> Audio Units -> Teilormusic)
    juce_add_plugin(ChoirArrangerLive
        COMPANY_NAME "Teilormusic"
        BUNDLE_ID "com.teilormusic.choirarrangerlive"
        PLUGIN_MANUFACTURER_CODE Tlrm
        PLUGIN_CODE ChLv
        FORMATS AU VST3
        PRODUCT_NAME "Choir Arranger Live"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT TRUE
        IS_MIDI_EFFECT TRUE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        AU_MAIN_TYPE kAudioUnitType_MIDIProcessor
        VST3_CATEGORIES Fx Tools)
    target_sources(ChoirArrangerLive PRIVATE
        plugin/LiveProcessor.cpp plugin/LiveEditor.cpp plugin/PianoRoll.cpp plugin/Look.cpp)
    target_compile_definitions(ChoirArrangerLive PUBLIC
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
    target_link_libraries(ChoirArrangerLive
        PRIVATE choir_core juce::juce_audio_utils juce::juce_gui_extra
        PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags
               juce::juce_recommended_warning_flags)
endif()

# ---- plug-in tests: capture, state, preview and editor snapshots (needs a display) -------
option(CHOIR_BUILD_TESTS "Build the plug-in test runner" OFF)
if(CHOIR_BUILD_PLUGIN AND CHOIR_BUILD_TESTS)
    juce_add_console_app(ChoirPluginTests PRODUCT_NAME "ChoirPluginTests")
    target_sources(ChoirPluginTests PRIVATE tests/plugin_test.cpp
        plugin/PluginProcessor.cpp plugin/PluginEditor.cpp plugin/PianoRoll.cpp plugin/Look.cpp
        plugin/LiveProcessor.cpp plugin/LiveEditor.cpp)
    target_include_directories(ChoirPluginTests PRIVATE plugin)
    target_compile_definitions(ChoirPluginTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        JucePlugin_Name="Choir Arranger" JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1
        CHOIR_NO_PLUGIN_ENTRY=1)
    target_link_libraries(ChoirPluginTests PRIVATE choir_core juce::juce_audio_utils juce::juce_gui_extra
        juce::juce_recommended_config_flags)
endif()
