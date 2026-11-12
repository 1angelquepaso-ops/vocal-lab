cmake_minimum_required(VERSION 3.22)
project(MwCapture VERSION 0.1.0)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_console_app(mw_capture PRODUCT_NAME "mw_capture")
target_sources(mw_capture PRIVATE Source/Main.cpp)
target_compile_features(mw_capture PRIVATE cxx_std_17)
target_compile_definitions(mw_capture PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)
target_link_libraries(mw_capture
    PRIVATE juce::juce_audio_processors juce::juce_audio_formats juce::juce_gui_basics
    PUBLIC juce::juce_recommended_config_flags)
