# SPDX-License-Identifier: GPL-3.0-or-later

# JUCE's LV2 host unpacks its bundled LV2 specification files into the system temporary
# directory whenever an engine is created; TMPDIR keeps that inside the build directory.
set(lina_engine_test_scratch "${CMAKE_CURRENT_BINARY_DIR}/scratch")
file(MAKE_DIRECTORY "${lina_engine_test_scratch}/tmp")

lina_add_catch_test(lina_engine_smoke_test
  SOURCES engine_smoke_test.cpp
  LIBRARIES lina::tracktion
  PROPERTIES
    # JUCE's leak detector reports at static destruction, after the test body has returned.
    FAIL_REGULAR_EXPRESSION "JUCE Assertion failure|Leaked objects detected"
    ENVIRONMENT "TMPDIR=${lina_engine_test_scratch}/tmp"
    # No X server: JUCE must come up without a display, as it does in CI.
    ENVIRONMENT_MODIFICATION "DISPLAY=unset:")

target_compile_definitions(lina_engine_smoke_test PRIVATE
  LINA_TEST_SCRATCH_DIR="${lina_engine_test_scratch}")
