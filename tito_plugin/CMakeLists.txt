cmake_minimum_required(VERSION 3.22)
project(TitoPuente VERSION 0.1.0)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(TitoPuente
    COMPANY_NAME "vocal-lab"
    PRODUCT_NAME "TITO PUENTE"
    PLUGIN_MANUFACTURER_CODE Vlab
    PLUGIN_CODE Tito
    FORMATS VST3 Standalone
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Drum Sampler)

target_sources(TitoPuente PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_features(TitoPuente PRIVATE cxx_std_17)
target_compile_definitions(TitoPuente PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(TitoPuente
    PRIVATE juce::juce_audio_utils juce::juce_audio_formats
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# Offline render check (Linux dev loop / CI tests only - not shipped)
if (TITO_RENDER_TOOL)
    juce_add_console_app(tito_render PRODUCT_NAME "tito_render")
    target_sources(tito_render PRIVATE Source/RenderTool.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
    target_compile_features(tito_render PRIVATE cxx_std_17)
    target_compile_definitions(tito_render PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Name="TITO PUENTE"
        JucePlugin_IsSynth=1 JucePlugin_WantsMidiInput=1 JucePlugin_ProducesMidiOutput=0 JucePlugin_IsMidiEffect=0)
    target_link_libraries(tito_render PRIVATE juce::juce_audio_utils juce::juce_audio_formats juce::juce_audio_processors
        PUBLIC juce::juce_recommended_config_flags)
endif()
