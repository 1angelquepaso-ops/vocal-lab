cmake_minimum_required(VERSION 3.22)
project(Dreammic VERSION 2.0.0)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(Dreammic
    COMPANY_NAME "vocal-lab"
    PRODUCT_NAME "DREAMMIC"
    PLUGIN_MANUFACTURER_CODE Vlab
    PLUGIN_CODE Drmc
    FORMATS VST3 Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Tools)

target_sources(Dreammic PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_features(Dreammic PRIVATE cxx_std_17)
# v2: shared DSP from MIRRORWALL 2.0 (MEX-EQ matched-magnitude design, fitted comps, de-esser)
set(MEXCORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../mirrorwall_plugin/Source)
target_include_directories(Dreammic PRIVATE ${MEXCORE_DIR})
target_compile_definitions(Dreammic PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Dreammic
    PRIVATE juce::juce_audio_utils juce::juce_audio_formats juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# parity / test tool (not shipped)
juce_add_console_app(dm_render PRODUCT_NAME "dm_render")
target_sources(dm_render PRIVATE Tools/render.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_compile_features(dm_render PRIVATE cxx_std_17)
target_include_directories(dm_render PRIVATE ${MEXCORE_DIR})
target_compile_definitions(dm_render PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Name="DREAMMIC")
target_link_libraries(dm_render PRIVATE juce::juce_audio_utils juce::juce_audio_formats juce::juce_dsp juce::juce_core
    PUBLIC juce::juce_recommended_config_flags)

# numeric test tool (not shipped)
juce_add_console_app(dm_test PRODUCT_NAME "dm_test")
target_sources(dm_test PRIVATE Tools/test.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_compile_features(dm_test PRIVATE cxx_std_17)
target_include_directories(dm_test PRIVATE ${MEXCORE_DIR})
target_compile_definitions(dm_test PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Name="DREAMMIC")
target_link_libraries(dm_test PRIVATE juce::juce_audio_utils juce::juce_audio_formats juce::juce_dsp juce::juce_core
    PUBLIC juce::juce_recommended_config_flags)
