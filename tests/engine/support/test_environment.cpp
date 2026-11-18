// SPDX-License-Identifier: GPL-3.0-or-later
#include "lina/engine/testing/test_environment.hpp"

#include <cstdio>
#include <cstdlib>
#include <print>
#include <stdexcept>

namespace lina::engine::testing {

AssertionCounter::AssertionCounter() { juce::Logger::setCurrentLogger(this); }

AssertionCounter::~AssertionCounter() {
  juce::Logger::setCurrentLogger(nullptr);
}

void AssertionCounter::logMessage(const juce::String &message) {
  if (message.startsWith("JUCE Assertion failure")) {
    ++count_;
  }
  std::println(stderr, "{}", message.toRawUTF8());
}

std::filesystem::path enterScratchFolder(std::string_view name) {
  const auto folder = std::filesystem::path(LINA_TEST_SCRATCH_DIR) / name;
  std::filesystem::remove_all(folder);
  const auto temporary = folder / "tmp";
  std::filesystem::create_directories(temporary);
  // NOLINTBEGIN(concurrency-mt-unsafe)
  if (::setenv("HOME", folder.c_str(), 1) != 0 ||
      ::setenv("TMPDIR", temporary.c_str(), 1) != 0 ||
      ::unsetenv("DISPLAY") != 0) {
    throw std::runtime_error("could not set up the test environment");
  }
  // NOLINTEND(concurrency-mt-unsafe)
  return folder;
}

} // namespace lina::engine::testing
