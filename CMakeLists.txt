# SPDX-License-Identifier: GPL-3.0-or-later
#
# LINA: build entry point. Configure through the presets in CMakePresets.json
# (docs/design/14-build-test-and-ci.md).
cmake_minimum_required(VERSION 3.28)
project(LINA VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
# Headers, not modules (docs/design/14-build-test-and-ci.md): no module dependency scan.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Sanitizer flags are set first because they apply to everything that follows, third-party
# code included.
include(LinaInstrumentation)
include(LinaTargets)
option(LINA_ENABLE_ENGINE
  "Build the engine, the bridge and everything above them. Off builds the model tier only."
  ON)
if(LINA_ENABLE_ENGINE)
  include(LinaTracktion)
  include(LinaQt)
endif()

add_subdirectory(src)

include(CTest)
if(BUILD_TESTING)
  include(LinaCatch2)
  add_subdirectory(tests)
endif()

lina_write_coverage_binaries()

add_custom_target(grid
  COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tools/grid.sh"
  COMMENT "Checking clang-format on LINA sources"
  VERBATIM USES_TERMINAL)

# clang-tidy replays the compile database, so the target exists only where Clang wrote it.
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  add_custom_target(flat
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tools/flat.sh" "${CMAKE_BINARY_DIR}"
    COMMENT "Running clang-tidy on LINA sources"
    VERBATIM USES_TERMINAL)
endif()
