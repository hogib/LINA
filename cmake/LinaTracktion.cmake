# SPDX-License-Identifier: GPL-3.0-or-later
#
# Tracktion Engine and JUCE, compiled once into the static library lina::tracktion.
#
# Consumers see the headers as system headers and inherit the compile definitions, which must
# be identical in every translation unit that includes a JUCE header. Third-party source is
# never patched (CLAUDE.md); anything it needs is done with flags on this target.

set(lina_tracktion_dir "${PROJECT_SOURCE_DIR}/third_party/tracktion_engine")
if(NOT EXISTS "${lina_tracktion_dir}/modules/juce/CMakeLists.txt")
  message(FATAL_ERROR
    "The Tracktion Engine submodule or its JUCE submodule is missing. Run:\n"
    "  git submodule update --init --recursive")
endif()

# Module targets only: no juceaide host tool, no plugin or application wrappers.
set(JUCE_MODULES_ONLY ON CACHE BOOL "Only configure the JUCE modules" FORCE)
add_subdirectory("${lina_tracktion_dir}/modules/juce" third_party/juce EXCLUDE_FROM_ALL SYSTEM)
add_subdirectory("${lina_tracktion_dir}/modules" third_party/tracktion EXCLUDE_FROM_ALL SYSTEM)

add_library(lina_tracktion STATIC)
add_library(lina::tracktion ALIAS lina_tracktion)

# JUCE modules are interface targets that carry their sources; linking them privately compiles
# those sources into this library exactly once.
target_link_libraries(lina_tracktion PRIVATE
  tracktion::tracktion_core
  tracktion::tracktion_graph
  tracktion::tracktion_engine
  juce::juce_audio_devices
  juce::juce_audio_formats
  juce::juce_audio_processors
  juce::juce_audio_utils
  juce::juce_dsp
  juce::juce_osc
  juce::juce_gui_extra)

target_compile_definitions(lina_tracktion
  PUBLIC
    # docs/design/14-build-test-and-ci.md, "Dependencies".
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_JACK=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_LV2=1
    JUCE_PLUGINHOST_LADSPA=0
    TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1
    # Failed JUCE assertions are written to juce::Logger, so that tests can fail on them.
    # Debug only: optimised builds compile assertions out, and turning them on there does not
    # build (Tracktion v3.2.0 asserts on a variable that exists only in debug builds,
    # tracktion_CombiningNode.cpp:90).
    $<$<CONFIG:Debug>:JUCE_LOG_ASSERTIONS=1>
  INTERFACE
    # What the module targets themselves define (JUCE_MODULE_AVAILABLE_*, ...).
    $<TARGET_PROPERTY:lina_tracktion,COMPILE_DEFINITIONS>)

target_include_directories(lina_tracktion SYSTEM INTERFACE
  $<TARGET_PROPERTY:lina_tracktion,INCLUDE_DIRECTORIES>)

# Not our code: no warnings from it.
target_compile_options(lina_tracktion PRIVATE -w)

# Tracktion v3.2.0 vendors nanorange.hpp, which has a typo (`x.value` for `x.value_`) in a
# template that is never instantiated. GCC 14 and later reject that by default.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 14)
  target_compile_options(lina_tracktion PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wno-template-body>)
endif()

# JUCE 8.0.6's juce_AudioPluginInstance.h has a constructor template that is never instantiated
# and whose body cannot compile (it passes an array pointer to a base class that has no such
# constructor). GCC accepts it; Clang 23 rejects it at the definition, at every language level.
# The header reaches every translation unit that includes juce_audio_processors, so the
# workaround is public: Clang parses function template bodies only when they are instantiated.
# Clang deprecates that mode after C++20 and says so with a warning, silenced here.
# Cost for LINA code that includes JUCE headers: under Clang, an error in a template body of
# ours is reported only if the template is instantiated (GCC still checks the definition).
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  target_compile_options(lina_tracktion PUBLIC
    $<$<COMPILE_LANGUAGE:CXX>:-fdelayed-template-parsing>
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-delayed-template-parsing-in-cxx20>)
endif()

# UBSan's function-type check fires in the LV2 host libraries bundled with JUCE (serd calls
# sord's callbacks through a pointer type with `void*` in place of the real handle type, the
# usual C idiom; first seen at serd/src/n3.c:1471 while the engine reads the LV2 bundles
# installed on the machine). That one check is off for third-party code only; every other
# UBSan check and ASan stay on for it.
if(LINA_SANITIZER STREQUAL "address+undefined")
  target_compile_options(lina_tracktion PRIVATE -fno-sanitize=function)
endif()
