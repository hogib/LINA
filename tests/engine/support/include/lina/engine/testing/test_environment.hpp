// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LINA_ENGINE_TESTING_TEST_ENVIRONMENT_HPP
#define LINA_ENGINE_TESTING_TEST_ENVIRONMENT_HPP

#include <juce_core/juce_core.h>

#include <atomic>
#include <filesystem>
#include <string_view>

namespace lina::engine::testing {

/// Counts failed JUCE assertions, which JUCE_LOG_ASSERTIONS routes through
/// juce::Logger, and passes every log line on to stderr, where CTest's
/// FAIL_REGULAR_EXPRESSION also looks for them. Optimised builds compile JUCE
/// assertions out, so there the count is always zero and proves nothing.
class AssertionCounter final : public juce::Logger {
public:
  AssertionCounter();
  ~AssertionCounter() override;

  AssertionCounter(const AssertionCounter &) = delete;
  AssertionCounter &operator=(const AssertionCounter &) = delete;
  AssertionCounter(AssertionCounter &&) = delete;
  AssertionCounter &operator=(AssertionCounter &&) = delete;

  [[nodiscard]] int count() const { return count_.load(); }

protected:
  void logMessage(const juce::String &message) override;

private:
  std::atomic<int> count_{0};
};

/// An empty folder of the given name inside the build directory, and an
/// environment that keeps the test to it, also when the test binary is run
/// by hand and not by CTest:
///   - HOME is that folder. JUCE's LV2 host loads every bundle under ~/.lv2
///     when an engine starts, and this keeps the user's plugins out.
///   - TMPDIR is its subfolder `tmp`, where the same host unpacks its LV2
///     specification files.
///   - DISPLAY is unset, so that JUCE does not connect to an X server.
///
/// Call it first in a test case, before JUCE is initialised: it changes the
/// environment, which is safe only while the process has a single thread.
/// Each test case needs a name of its own, since test cases run as separate
/// processes that may run at the same time.
std::filesystem::path enterScratchFolder(std::string_view name);

} // namespace lina::engine::testing

#endif // LINA_ENGINE_TESTING_TEST_ENVIRONMENT_HPP
