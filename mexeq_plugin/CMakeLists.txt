cmake_minimum_required(VERSION 3.22)
project(MexEQ VERSION 0.1.0)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(MexEQ
    COMPANY_NAME "vocal-lab"
    PRODUCT_NAME "MEX-EQ"
    PLUGIN_MANUFACTURER_CODE Vlab
    PLUGIN_CODE Mxeq
    FORMATS VST3 Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx EQ)

target_sources(MexEQ PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_features(MexEQ PRIVATE cxx_std_17)
target_compile_definitions(MexEQ PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(MexEQ
    PRIVATE juce::juce_audio_utils juce::juce_audio_formats juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# test tool (not shipped)
juce_add_console_app(me_render PRODUCT_NAME "me_render")
target_sources(me_render PRIVATE Tools/render.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_compile_features(me_render PRIVATE cxx_std_17)
target_compile_definitions(me_render PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Name="MEX-EQ")
target_link_libraries(me_render PRIVATE juce::juce_audio_utils juce::juce_audio_formats juce::juce_dsp juce::juce_core
    PUBLIC juce::juce_recommended_config_flags)
