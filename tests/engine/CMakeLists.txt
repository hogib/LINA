# SPDX-License-Identifier: GPL-3.0-or-later

add_subdirectory(support)
get_target_property(lina_engine_test_scratch lina_engine_testing LINA_TEST_SCRATCH_DIR)

# lina_add_engine_test(<name> <source>...)
#
# A Catch2 test of the engine tier. It runs headless: no display, temporary files inside the
# build directory, and it fails when JUCE reports a failed assertion or a leaked object.
function(lina_add_engine_test name)
  lina_add_catch_test(${name}
    SOURCES ${ARGN}
    LIBRARIES lina::engine_testing
    PROPERTIES
      # JUCE's leak detector reports at static destruction, after the test body has returned.
      FAIL_REGULAR_EXPRESSION "JUCE Assertion failure|Leaked objects detected"
      ENVIRONMENT "TMPDIR=${lina_engine_test_scratch}/tmp"
      # No X server: JUCE must come up without a display, as it does in CI.
      ENVIRONMENT_MODIFICATION "DISPLAY=unset:")
endfunction()

lina_add_engine_test(lina_engine_smoke_test engine_smoke_test.cpp)
target_compile_definitions(lina_engine_smoke_test PRIVATE
  LINA_TEST_SCRATCH_DIR="${lina_engine_test_scratch}")

lina_add_engine_test(lina_engine_playback_test engine_playback_test.cpp)
lina_add_engine_test(lina_engine_render_test engine_render_test.cpp)
