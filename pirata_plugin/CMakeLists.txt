cmake_minimum_required(VERSION 3.22)
project(Pirata VERSION 0.1.0)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(Pirata
    COMPANY_NAME "vocal-lab"
    PRODUCT_NAME "PIRATA"
    PLUGIN_MANUFACTURER_CODE Vlab
    PLUGIN_CODE Prta
    FORMATS VST3 Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Tools)

target_sources(Pirata PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_features(Pirata PRIVATE cxx_std_17)
target_compile_definitions(Pirata PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Pirata
    PRIVATE juce::juce_audio_utils juce::juce_audio_formats
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
