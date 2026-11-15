cmake_minimum_required(VERSION 3.22)
project(HyperTune VERSION 0.1.0)

option(COPY_AFTER_BUILD "Copier le plugin apres compilation" ON)
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.6
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(HyperTune
    COMPANY_NAME "SoleilBleu"
    PLUGIN_MANUFACTURER_CODE Sole
    PLUGIN_CODE Hytn
    FORMATS VST3 Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    PRODUCT_NAME "HyperTune"
    COPY_PLUGIN_AFTER_BUILD ${COPY_AFTER_BUILD})

target_sources(HyperTune PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h)

target_compile_definitions(HyperTune PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(HyperTune PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
