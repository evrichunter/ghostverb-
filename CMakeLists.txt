cmake_minimum_required(VERSION 3.22)

project(GhostVerb VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Static MSVC runtime: the plugin does not need the VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

# JUCE 8 (AGPLv3 / commercial). Use a local checkout with -DGHOSTVERB_JUCE_PATH=<dir>,
# otherwise the pinned release is downloaded automatically.
set(GHOSTVERB_JUCE_PATH "" CACHE PATH "Path to a local JUCE 8 checkout (optional)")
if(GHOSTVERB_JUCE_PATH)
    add_subdirectory(${GHOSTVERB_JUCE_PATH} JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.6
        GIT_SHALLOW    ON)
    FetchContent_MakeAvailable(JUCE)
endif()

option(GHOSTVERB_BUILD_TESTS "Build the GhostVerbTests console app" ON)
option(GHOSTVERB_SANITIZE_THREAD "Build the capture stress test with ThreadSanitizer (Linux/macOS, Clang or GCC)" OFF)

# ---------------------------------------------------------------------------
# Embedded fonts (Inter, SIL OFL 1.1 - see THIRD_PARTY_NOTICES.md)
# ---------------------------------------------------------------------------
juce_add_binary_data(GhostVerbAssets SOURCES
    Assets/Fonts/Inter-Regular.ttf
    Assets/Fonts/Inter-SemiBold.ttf)
set_target_properties(GhostVerbAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(GV_SOURCES
    Source/AudioCaptureEngine.cpp
    Source/AudioRenderer.cpp
    Source/PreviewPlayer.cpp
    Source/ReverseReverbEngine.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

set(GV_COMPILE_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_MODAL_LOOPS_PERMITTED=0)

set(GV_LINK_LIBS
    GhostVerbAssets
    juce::juce_audio_utils
    juce::juce_audio_formats
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Plugin
# ---------------------------------------------------------------------------
juce_add_plugin(GhostVerb
    COMPANY_NAME                "Evric Hunter"
    PLUGIN_MANUFACTURER_CODE    Evhu
    PLUGIN_CODE                 GHVB
    PRODUCT_NAME                "GHOST VERB"
    VERSION                     ${PROJECT_VERSION}
    FORMATS                     VST3
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES             Fx Reverb
    COPY_PLUGIN_AFTER_BUILD     FALSE)

target_sources(GhostVerb PRIVATE ${GV_SOURCES})
target_include_directories(GhostVerb PRIVATE Source)
target_compile_definitions(GhostVerb PUBLIC ${GV_COMPILE_DEFS})
target_link_libraries(GhostVerb PRIVATE ${GV_LINK_LIBS})

# ---------------------------------------------------------------------------
# Tests (algorithm, capture, renderer, processor, WAV export)
# ---------------------------------------------------------------------------
if(GHOSTVERB_BUILD_TESTS)
    juce_add_console_app(GhostVerbTests PRODUCT_NAME "GhostVerbTests")
    target_sources(GhostVerbTests PRIVATE ${GV_SOURCES} Tests/Tests.cpp)
    target_include_directories(GhostVerbTests PRIVATE Source)
    target_compile_definitions(GhostVerbTests PUBLIC ${GV_COMPILE_DEFS})
    target_link_libraries(GhostVerbTests PRIVATE GhostVerbAssets juce::juce_audio_utils juce::juce_audio_formats
                                                 juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    enable_testing()
    add_test(NAME GhostVerbTests COMMAND GhostVerbTests)
    if(GHOSTVERB_SANITIZE_THREAD)
        target_compile_options(GhostVerbTests PRIVATE -fsanitize=thread -O1 -g)
        target_link_options(GhostVerbTests PRIVATE -fsanitize=thread)
    endif()

    # Capture engine stress test (audio / UI / message threads). Run it with -DGHOSTVERB_SANITIZE_THREAD=ON for TSan.
    juce_add_console_app(GhostVerbCaptureStress PRODUCT_NAME "GhostVerbCaptureStress")
    target_sources(GhostVerbCaptureStress PRIVATE Source/AudioCaptureEngine.cpp Tests/CaptureStress.cpp)
    target_include_directories(GhostVerbCaptureStress PRIVATE Source)
    target_compile_definitions(GhostVerbCaptureStress PUBLIC ${GV_COMPILE_DEFS})
    target_link_libraries(GhostVerbCaptureStress PRIVATE juce::juce_audio_basics juce::juce_core juce::juce_recommended_config_flags)
    if(GHOSTVERB_SANITIZE_THREAD)
        target_compile_options(GhostVerbCaptureStress PRIVATE -fsanitize=thread -O1 -g)
        target_link_options(GhostVerbCaptureStress PRIVATE -fsanitize=thread)
    endif()
    add_test(NAME GhostVerbCaptureStress COMMAND GhostVerbCaptureStress)
endif()
