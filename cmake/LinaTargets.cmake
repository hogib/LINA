# SPDX-License-Identifier: GPL-3.0-or-later
#
# What every target of LINA's own gets: warnings as errors, coverage instrumentation when the
# preset asks for it, and test registration.

add_library(lina_warnings INTERFACE)
add_library(lina::warnings ALIAS lina_warnings)
target_compile_options(lina_warnings INTERFACE -Wall -Wextra -Wpedantic -Werror)

# Call on every library and executable of LINA's own. Never on third-party targets.
function(lina_target_defaults target)
  target_link_libraries(${target} PRIVATE lina::warnings lina::coverage)
endfunction()

# lina_add_catch_test(<name> SOURCES <file>... [LIBRARIES <target>...]
#                     [PROPERTIES <CTest property> <value>...])
#
# Adds a Catch2 test executable and registers each of its test cases with CTest. Every test
# case runs in a process of its own.
function(lina_add_catch_test name)
  cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "SOURCES;LIBRARIES;PROPERTIES")
  if(NOT arg_SOURCES)
    message(FATAL_ERROR "lina_add_catch_test(${name}): SOURCES is required")
  endif()

  add_executable(${name} ${arg_SOURCES})
  lina_target_defaults(${name})
  target_link_libraries(${name} PRIVATE Catch2::Catch2WithMain ${arg_LIBRARIES})
  set_property(GLOBAL APPEND PROPERTY LINA_COVERAGE_BINARIES "$<TARGET_FILE:${name}>")

  catch_discover_tests(${name} PROPERTIES ${arg_PROPERTIES})
endfunction()

# Lists the instrumented test executables for tools/pawl.sh, one per line.
function(lina_write_coverage_binaries)
  if(NOT LINA_COVERAGE)
    return()
  endif()
  get_property(binaries GLOBAL PROPERTY LINA_COVERAGE_BINARIES)
  list(JOIN binaries "\n" content)
  file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/coverage-binaries.txt" CONTENT "${content}\n")
endfunction()
