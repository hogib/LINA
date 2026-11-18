// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LINA_ENGINE_TESTING_ENGINE_FIXTURE_HPP
#define LINA_ENGINE_TESTING_ENGINE_FIXTURE_HPP

#include "lina/engine/engine.hpp"
#include "lina/engine/message_thread.hpp"
#include "lina/engine/testing/edit_building.hpp"
#include "lina/engine/testing/test_environment.hpp"

#include <filesystem>
#include <string_view>

namespace lina::engine::testing {

/// A headless Engine with an empty edit and the test plugins registered,
/// ready for a test case to build on. `name` names the test case's scratch
/// folder and must be unique among all test cases.
struct EngineFixture {
  explicit EngineFixture(std::string_view name)
      : scratch(enterScratchFolder(name)),
        engine({.storageFolder = scratch / "engine"}) {
    registerTestPlugins(engine);
  }

  AssertionCounter assertions;
  std::filesystem::path scratch;
  MessageThread messageThread;
  Engine engine;
};

} // namespace lina::engine::testing

#endif // LINA_ENGINE_TESTING_ENGINE_FIXTURE_HPP
