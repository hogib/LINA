# SPDX-License-Identifier: GPL-3.0-or-later
#
# Catch2 v3: the system package if there is one, otherwise a pinned fetch. Never vendored.

find_package(Catch2 3 QUIET)
if(Catch2_FOUND)
  message(STATUS "Catch2 ${Catch2_VERSION}: system package")
else()
  include(FetchContent)
  # v3.16.1. The tag is pinned by its commit hash so that it cannot move.
  FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG 08092139210881f01a34e60c5f2fd6afa0ea4024
    GIT_SHALLOW FALSE
    SYSTEM
    EXCLUDE_FROM_ALL)
  FetchContent_MakeAvailable(Catch2)
  list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
  message(STATUS "Catch2 v3.16.1: fetched")
endif()

include(Catch)
