# LINA feasibility spike: Tracktion Engine (JUCE) + Qt 6 Quick in one process.
# Throwaway code. See NOTES in the hand-back report, not in this tree.
cmake_minimum_required(VERSION 3.25)
project(lina_engine_qt_spike VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# ---------------------------------------------------------------- JUCE + Tracktion
# JUCE is the submodule pinned by the Tracktion Engine checkout, not a separate choice.
set(TE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/tracktion_engine")
add_subdirectory("${TE_DIR}/modules/juce" juce EXCLUDE_FROM_ALL)
add_subdirectory("${TE_DIR}/modules" tracktion EXCLUDE_FROM_ALL)

# All JUCE/Tracktion module sources are compiled once into this static library, so that
# Qt's AUTOMOC never sees them and the application target only sees headers + defines.
# (Pattern from JUCE's docs/CMake API.md, "juce_add_module ... static library".)
add_library(lina_juce STATIC)
set_target_properties(lina_juce PROPERTIES
    AUTOMOC OFF
    POSITION_INDEPENDENT_CODE TRUE)

target_link_libraries(lina_juce
    PRIVATE
        tracktion::tracktion_core
        tracktion::tracktion_graph
        tracktion::tracktion_engine
        juce::juce_audio_devices
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags)

target_compile_definitions(lina_juce
    PUBLIC
        JUCE_WEB_BROWSER=0              # webkit2gtk-4.1 is not installed
        JUCE_USE_CURL=0                 # not needed
        JUCE_JACK=0                     # ALSA only; keeps the dependency list short
        JUCE_PLUGINHOST_VST3=1
        JUCE_PLUGINHOST_LV2=1
        JUCE_PLUGINHOST_LADSPA=0        # ladspa.h is not installed
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        JUCE_LOG_ASSERTIONS=1           # jasserts go through juce::Logger so --selftest can count them
        # Only needed for the *alternative* event loop (--loop=poll), which calls
        # MessageManager::runDispatchLoopUntil. The primary fd bridge does not use it.
        JUCE_MODAL_LOOPS_PERMITTED=1
    INTERFACE
        $<TARGET_PROPERTY:lina_juce,COMPILE_DEFINITIONS>)

target_include_directories(lina_juce
    INTERFACE
        $<TARGET_PROPERTY:lina_juce,INCLUDE_DIRECTORIES>)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(lina_juce PUBLIC atomic)
endif()

# Third-party code: do not drown our own warnings in theirs.
target_compile_options(lina_juce PRIVATE -w)

# Tracktion v3.2.0 vendors nanorange, which has a typo (`x.value` for `x.value_`) inside a
# template that is never instantiated. GCC >= 14 diagnoses uninstantiated template bodies as
# an error by default; this turns that check off for the third-party library only, instead
# of patching the checkout. Not related to the C++ language level.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 14)
    target_compile_options(lina_juce PRIVATE -Wno-template-body)
endif()

# ---------------------------------------------------------------- Qt
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick)
qt_policy(SET QTP0001 NEW)
if(QT_KNOWN_POLICY_QTP0004)
    qt_policy(SET QTP0004 NEW)
endif()

qt_add_executable(lina_spike
    src/main.cpp
    src/JuceQtEventLoop.h   src/JuceQtEventLoop.cpp
    src/EngineHost.h        src/EngineHost.cpp
    src/EditModels.cpp
    src/SelfTest.h          src/SelfTest.cpp)

set_target_properties(lina_spike PROPERTIES AUTOMOC ON)

# QML module. EditModels.h is listed here so qmltyperegistrar picks up QML_ELEMENT.
qt_add_qml_module(lina_spike
    URI Lina.Spike
    VERSION 1.0
    QML_FILES qml/Main.qml
    SOURCES src/EditModels.h)

target_include_directories(lina_spike PRIVATE src)

target_compile_definitions(lina_spike PRIVATE
    # Qt's lowercase `signals` / `slots` / `emit` macros would rewrite identifiers inside
    # JUCE and Tracktion headers (Tracktion has members called `slots`).
    QT_NO_KEYWORDS)

target_compile_options(lina_spike PRIVATE -Wall -Wextra)

target_link_libraries(lina_spike PRIVATE
    lina_juce
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
