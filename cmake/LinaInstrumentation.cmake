# SPDX-License-Identifier: GPL-3.0-or-later
#
# Sanitizers and coverage, selected by the presets.
#
# LINA_SANITIZER applies to every target in the build, third-party code included: ThreadSanitizer
# reports false races through uninstrumented code, and AddressSanitizer sees more with it.
#
# LINA_COVERAGE instruments LINA's own targets only (through lina_target_defaults), so the
# third-party library is built without it.

set(LINA_SANITIZER "" CACHE STRING
  "Sanitizer for the whole build: empty, address+undefined, thread or realtime")
set_property(CACHE LINA_SANITIZER PROPERTY STRINGS "" "address+undefined" "thread" "realtime")
option(LINA_COVERAGE "Instrument LINA's own targets for llvm-cov source-based coverage" OFF)

if(LINA_SANITIZER OR LINA_COVERAGE)
  if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "LINA_SANITIZER and LINA_COVERAGE need Clang.")
  endif()
endif()
if(LINA_SANITIZER AND LINA_COVERAGE)
  message(FATAL_ERROR "LINA_SANITIZER and LINA_COVERAGE are separate presets.")
endif()

if(LINA_SANITIZER STREQUAL "")
  set(lina_sanitizer_flags "")
elseif(LINA_SANITIZER STREQUAL "address+undefined")
  # UBSan findings abort instead of printing and carrying on, so that a test fails on them.
  set(lina_sanitizer_flags -fsanitize=address,undefined -fno-sanitize-recover=undefined)
elseif(LINA_SANITIZER STREQUAL "thread")
  set(lina_sanitizer_flags -fsanitize=thread)
elseif(LINA_SANITIZER STREQUAL "realtime")
  if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 20)
    message(FATAL_ERROR "RealtimeSanitizer needs Clang 20 or later.")
  endif()
  set(lina_sanitizer_flags -fsanitize=realtime)
else()
  message(FATAL_ERROR "Unknown LINA_SANITIZER value: ${LINA_SANITIZER}")
endif()

if(lina_sanitizer_flags)
  add_compile_options(${lina_sanitizer_flags} -fno-omit-frame-pointer)
  add_link_options(${lina_sanitizer_flags})
endif()

add_library(lina_coverage INTERFACE)
add_library(lina::coverage ALIAS lina_coverage)
if(LINA_COVERAGE)
  target_compile_options(lina_coverage INTERFACE -fprofile-instr-generate -fcoverage-mapping)
  target_link_options(lina_coverage INTERFACE -fprofile-instr-generate)
endif()
