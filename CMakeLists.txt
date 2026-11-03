cmake_minimum_required(VERSION 3.22)
project(MusicLearnTrainer VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE and the WebView2 SDK live outside the vault (see build.cmd / README.md).
set(JUCE_DIR "$ENV{USERPROFILE}/Documents/git/JUCE" CACHE PATH "Path to a JUCE 8 checkout")
set(JUCE_WEBVIEW2_PACKAGE_LOCATION "$ENV{USERPROFILE}/Documents/git/_deps/webview2"
    CACHE PATH "Folder that contains the Microsoft.Web.WebView2.* package directory")

# The JUCE release this builds against (its git tag), checked against the version the checkout's own CMakeLists.txt
# gives, so a build never quietly uses another JUCE.
set(JUCE_TAG 8.0.17-security)
string(REGEX MATCH "^[0-9]+\\.[0-9]+\\.[0-9]+" JUCE_REQUIRED_VERSION "${JUCE_TAG}")
set(juce_clone_hint "git clone --depth 1 --branch ${JUCE_TAG} https://github.com/juce-framework/JUCE.git")

if(NOT EXISTS "${JUCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "JUCE not found at ${JUCE_DIR}. Clone it: ${juce_clone_hint}")
endif()

file(STRINGS "${JUCE_DIR}/CMakeLists.txt" juce_project_line REGEX "^project\\(JUCE VERSION ")
string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" juce_found_version "${juce_project_line}")
if(NOT juce_found_version VERSION_EQUAL JUCE_REQUIRED_VERSION)
    message(FATAL_ERROR "JUCE ${JUCE_REQUIRED_VERSION} needed, but ${JUCE_DIR} is JUCE '${juce_found_version}'. "
                        "Clone the right one there: ${juce_clone_hint}")
endif()

# The WebView2 SDK version this builds against. JUCE's FindWebView2 caches the first package folder it finds,
# so a build folder would keep using an older SDK after a newer one is unpacked; pinning the folder here stops that.
set(WEBVIEW2_SDK_VERSION 1.0.4314.35)
if(WIN32)
    set(webview2_sdk_dir "${JUCE_WEBVIEW2_PACKAGE_LOCATION}/Microsoft.Web.WebView2.${WEBVIEW2_SDK_VERSION}")
    if(NOT EXISTS "${webview2_sdk_dir}/build/native/include/WebView2.h")
        message(FATAL_ERROR "WebView2 SDK ${WEBVIEW2_SDK_VERSION} not found at ${webview2_sdk_dir}. Download "
                            "https://www.nuget.org/packages/Microsoft.Web.WebView2/${WEBVIEW2_SDK_VERSION} and unzip the .nupkg into that folder.")
    endif()
    set(WebView2_root_dir "${webview2_sdk_dir}" CACHE PATH "WebView2 SDK package folder (from WEBVIEW2_SDK_VERSION)" FORCE)
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
