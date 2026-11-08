cmake_minimum_required(VERSION 3.22)
project(VocalLabHarmonizer VERSION 0.1.0)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(VocalLabHarmonizer
    COMPANY_NAME "vocal-lab"
    PRODUCT_NAME "vocal-lab Harmonizer"
    PLUGIN_MANUFACTURER_CODE Vlab
    PLUGIN_CODE Vhrm
    FORMATS VST3 Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Tools)

target_sources(VocalLabHarmonizer PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_features(VocalLabHarmonizer PRIVATE cxx_std_17)
target_compile_definitions(VocalLabHarmonizer PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(VocalLabHarmonizer
    PRIVATE juce::juce_audio_utils juce::juce_audio_formats
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
