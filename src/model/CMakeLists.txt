# SPDX-License-Identifier: GPL-3.0-or-later
#
# lina_model: LINA's own document (docs/design/13-architecture.md). Standard library only: no
# Qt, JUCE or Tracktion include path reaches this target.
lina_add_library(lina_model
  SOURCES
    apply.cpp
    apply_automation.cpp
    apply_clips.cpp
    apply_devices.cpp
    apply_project.cpp
    change_set.cpp
    document.cpp
    project.cpp
    rules.cpp
    time.cpp)
