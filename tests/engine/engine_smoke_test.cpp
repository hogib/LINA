// SPDX-License-Identifier: GPL-3.0-or-later
//
// Smoke test of the third-party stack: a Tracktion engine and an empty edit are
// created and destroyed headless. It proves that JUCE and Tracktion Engine
// compile, link and start under the preset being built. It opens no audio
// device and no window, and keeps its settings and temporary files inside the
// build directory.

#include <catch2/catch_test_macros.hpp>

#include <tracktion_engine/tracktion_engine.h>

#include <atomic>
#include <cstdlib>
#include <memory>
#include <utility>

namespace {

namespace te = tracktion;

/// Keeps the engine's settings and cache out of the user's home directory.
class ScratchPropertyStorage final : public te::PropertyStorage {
public:
  explicit ScratchPropertyStorage(juce::File folder)
      : PropertyStorage("lina-engine-smoke-test"), folder_(std::move(folder)) {}

  juce::File getAppCacheFolder() override { return folder_; }
  juce::File getAppPrefsFolder() override { return folder_; }
  juce::String getUserName() override { return "lina-test"; }

private:
  juce::File folder_;
};

/// Stops the engine from opening the machine's default audio device on
/// construction.
class HeadlessEngineBehaviour final : public te::EngineBehaviour {
public:
  bool autoInitialiseDeviceManager() override { return false; }
};

/// Counts failed JUCE assertions, which JUCE_LOG_ASSERTIONS routes through
/// juce::Logger. Optimised builds compile JUCE assertions out, so there the
/// count is always zero and the check on it proves nothing.
class AssertionCounter final : public juce::Logger {
public:
  AssertionCounter() { juce::Logger::setCurrentLogger(this); }
  ~AssertionCounter() override { juce::Logger::setCurrentLogger(nullptr); }

  AssertionCounter(const AssertionCounter &) = delete;
  AssertionCounter &operator=(const AssertionCounter &) = delete;
  AssertionCounter(AssertionCounter &&) = delete;
  AssertionCounter &operator=(AssertionCounter &&) = delete;

  [[nodiscard]] int count() const { return count_.load(); }

protected:
  void logMessage(const juce::String &message) override {
    if (message.startsWith("JUCE Assertion failure")) {
      ++count_;
    }
  }

private:
  std::atomic<int> count_{0};
};

juce::File freshScratchFolder(const char *name) {
  auto folder = juce::File(LINA_TEST_SCRATCH_DIR).getChildFile(name);
  folder.deleteRecursively();
  REQUIRE(folder.createDirectory().wasOk());
  return folder;
}

} // namespace

TEST_CASE("an engine and an empty edit start and stop headless",
          "[engine][smoke]") {
  const AssertionCounter assertions;
  const auto scratch = freshScratchFolder("engine-smoke");
  // JUCE's LV2 host loads every bundle under ~/.lv2 when an engine starts. A
  // home directory inside the scratch folder keeps the user's plugins out of
  // the test. The system-wide LV2 folders cannot be redirected. setenv is
  // safe here: the test case runs in its own process and no other thread
  // exists before JUCE is initialised below.
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  REQUIRE(::setenv("HOME", scratch.getFullPathName().toRawUTF8(), 1) == 0);

  {
    const juce::ScopedJuceInitialiser_GUI juce;
    te::Engine engine(std::make_unique<ScratchPropertyStorage>(scratch),
                      nullptr, std::make_unique<HeadlessEngineBehaviour>());

    CHECK(engine.getDeviceManager().deviceManager.getCurrentAudioDevice() ==
          nullptr);
    CHECK(engine.getTemporaryFileManager().getTempDirectory().isAChildOf(
        scratch));

    const auto edit = te::createEmptyEdit(
        engine, scratch.getChildFile("empty.tracktionedit"));
    REQUIRE(edit != nullptr);
    CHECK(&edit->engine == &engine);
  }

  CHECK(assertions.count() == 0);
}
