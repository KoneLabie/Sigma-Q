cmake_minimum_required(VERSION 3.22)
project(SigmaQ VERSION 0.1.0)

# Static runtime: users do NOT need to install the Visual C++ redistributable
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

set(ME_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND ME_FORMATS AU)
endif()

juce_add_plugin(SigmaQ
    COMPANY_NAME "Kone Labie"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    PLUGIN_MANUFACTURER_CODE Ynam
    PLUGIN_CODE Sgmq
    FORMATS ${ME_FORMATS}
    VST3_CATEGORIES Fx EQ
    COPY_PLUGIN_AFTER_BUILD FALSE
    PRODUCT_NAME "Sigma Q")

juce_generate_juce_header(SigmaQ)

target_sources(SigmaQ PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/EQDisplay.h
    Source/BandCard.h
    Source/UI.h
    Source/Dsp.h)

target_compile_definitions(SigmaQ PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(SigmaQ
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
