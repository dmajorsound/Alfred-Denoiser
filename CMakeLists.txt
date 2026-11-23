cmake_minimum_required(VERSION 3.22)

project(Alfred VERSION 0.29.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")

option(ALFRED_CLAP "Build the CLAP format as well (clap-juce-extensions)" ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.6
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

if(ALFRED_CLAP)
    FetchContent_Declare(clap-juce-extensions
        GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
        GIT_TAG 3837f4a6ed2ab0ba46aba69ae5b7041c7c64ce74
        GIT_SUBMODULES_RECURSE ON)
    FetchContent_MakeAvailable(clap-juce-extensions)
endif()

set(ALFRED_FORMATS VST3)
if(APPLE)
    list(APPEND ALFRED_FORMATS AU)
endif()

juce_add_plugin(Alfred
    PRODUCT_NAME "Alfred"
    COMPANY_NAME "dmajorsound"
    COMPANY_WEBSITE "https://dmajorsound.com"
    BUNDLE_ID "com.dmajorsound.alfred"
    PLUGIN_MANUFACTURER_CODE Dmjs
    PLUGIN_CODE Alfd
    DESCRIPTION "A simple Denoiser at your Service"
    FORMATS ${ALFRED_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Restoration
    AU_MAIN_TYPE kAudioUnitType_Effect)

target_sources(Alfred PRIVATE
    src/plugin/PluginProcessor.cpp
    src/plugin/PluginEditor.cpp
    src/plugin/JuceGfx.cpp
    src/core/JsfxRuntime.cpp
    src/core/fft.c
    src/gen/AlfredCore.cpp)

target_include_directories(Alfred PRIVATE src/core src/gen src/plugin)

target_compile_definitions(Alfred PUBLIC
    WDL_FFT_REALSIZE=8
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

# The arithmetic must stay exactly as written (no fused multiply-add), so the plug-in sounds like the JSFX.
if(MSVC)
    set_source_files_properties(src/core/JsfxRuntime.cpp src/core/fft.c src/gen/AlfredCore.cpp
        PROPERTIES COMPILE_OPTIONS "/fp:precise;/bigobj")
else()
    set_source_files_properties(src/core/JsfxRuntime.cpp src/core/fft.c src/gen/AlfredCore.cpp
        PROPERTIES COMPILE_OPTIONS "-ffp-contract=off")
endif()

# Arial-compatible font (Liberation Sans, SIL OFL 1.1) built into the plug-in, so the text looks and
# measures the same on macOS and Windows, whatever fonts the system has
juce_add_binary_data(AlfredFonts SOURCES
    resources/fonts/LiberationSans-Regular.ttf
    resources/fonts/LiberationSans-Bold.ttf
    resources/fonts/LiberationSans-Italic.ttf
    resources/fonts/LiberationSans-BoldItalic.ttf)

target_link_libraries(Alfred
    PRIVATE
        AlfredFonts
        juce::juce_audio_processors
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(ALFRED_CLAP)
    clap_juce_extensions_plugin(TARGET Alfred
        CLAP_ID "com.dmajorsound.alfred"
        CLAP_FEATURES audio-effect restoration stereo mono)
endif()

# ---- tests (Linux/macOS, not needed for the plug-in): headless interface rendering
option(ALFRED_TESTS "Build the headless interface test" OFF)
if(ALFRED_TESTS)
    juce_add_console_app(AlfredGfxTest PRODUCT_NAME "AlfredGfxTest")
    target_sources(AlfredGfxTest PRIVATE tests/gfxtest.cpp src/plugin/JuceGfx.cpp src/core/JsfxRuntime.cpp src/core/fft.c src/gen/AlfredCore.cpp)
    target_include_directories(AlfredGfxTest PRIVATE src/core src/gen src/plugin)
    target_compile_definitions(AlfredGfxTest PRIVATE WDL_FFT_REALSIZE=8 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(AlfredGfxTest PRIVATE AlfredFonts juce::juce_graphics juce::juce_recommended_config_flags)
endif()
