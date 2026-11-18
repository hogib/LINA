# SPDX-License-Identifier: GPL-3.0-or-later
#
# lina_bridge: where Qt meets the engine (docs/design/13-architecture.md). For now it holds the
# event-loop integration only, which is the one place that includes both Qt and JUCE headers.

lina_add_library(lina_bridge
  SOURCES
    event_loop_bridge.cpp
  PUBLIC_LIBRARIES
    Qt6::Core
  PRIVATE_LIBRARIES
    lina::tracktion)

# Tracktion has identifiers named `slots` (docs/design/14-build-test-and-ci.md).
target_compile_definitions(lina_bridge PUBLIC QT_NO_KEYWORDS)
