cmake_minimum_required(VERSION 3.22)
project(Wobbled VERSION 0.1.3)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(Wobbled
    COMPANY_NAME "vocal-lab"
    PRODUCT_NAME "WOBBLE-D"
    PLUGIN_MANUFACTURER_CODE Vlab
    PLUGIN_CODE Wbld
    FORMATS VST3 Standalone
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Synth)

target_sources(Wobbled PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_features(Wobbled PRIVATE cxx_std_17)
target_compile_definitions(Wobbled PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Wobbled
    PRIVATE juce::juce_audio_utils juce::juce_audio_formats juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# offline test tool (not shipped): renders a song through the real processor and checks clicks / determinism
juce_add_console_app(wb_render PRODUCT_NAME "wb_render")
target_sources(wb_render PRIVATE Tools/render.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_compile_features(wb_render PRIVATE cxx_std_17)
target_compile_definitions(wb_render PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1 JucePlugin_Name="WOBBLE-D")
target_link_libraries(wb_render PRIVATE juce::juce_audio_utils juce::juce_audio_formats juce::juce_dsp juce::juce_core
    PUBLIC juce::juce_recommended_config_flags)
