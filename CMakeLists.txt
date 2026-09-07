cmake_minimum_required(VERSION 3.22)
project(MusicLearnTrainer VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE and the WebView2 SDK live outside the vault (see build.cmd / README.md).
set(JUCE_DIR "$ENV{USERPROFILE}/Documents/git/JUCE" CACHE PATH "Path to a JUCE 8 checkout")
set(JUCE_WEBVIEW2_PACKAGE_LOCATION "$ENV{USERPROFILE}/Documents/git/_deps/webview2"
    CACHE PATH "Folder that contains the Microsoft.Web.WebView2.* package directory")

if(NOT EXISTS "${JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "JUCE not found at ${JUCE_DIR}. Clone it: git clone --depth 1 --branch 8.0.8 https://github.com/juce-framework/JUCE.git")
endif()

add_subdirectory(${JUCE_DIR} JUCE EXCLUDE_FROM_ALL)

juce_add_plugin(MusicLearnTrainer
    COMPANY_NAME "MusicLearn"
    PRODUCT_NAME "MusicLearn Trainer"
    PLUGIN_MANUFACTURER_CODE Mlrn
    PLUGIN_CODE Mltr
    FORMATS VST3 Standalone
    IS_SYNTH TRUE                  # loads on a MIDI track in Ableton; outputs silence
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE         # MIDI passes through, so other tracks can take it from this plugin
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES Instrument Tools
    NEEDS_WEB_BROWSER TRUE
    NEEDS_WEBVIEW2 TRUE            # statically link the WebView2 loader
    COPY_PLUGIN_AFTER_BUILD FALSE)

# The whole UI is one HTML file plus the Bravura music font, embedded into the binary.
juce_add_binary_data(TrainerAssets
    HEADER_NAME TrainerAssets.h
    NAMESPACE TrainerAssets
    SOURCES ui/index.html ui/Bravura.woff2)

target_sources(MusicLearnTrainer PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(MusicLearnTrainer PUBLIC
    JUCE_WEB_BROWSER=1
    JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)  # permitted under the AGPLv3 option of the JUCE licence, which this project uses

target_link_libraries(MusicLearnTrainer
    PRIVATE
        TrainerAssets
        juce::juce_audio_utils
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
