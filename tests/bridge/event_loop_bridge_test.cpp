// SPDX-License-Identifier: GPL-3.0-or-later
//
// The JUCE/Qt event-loop bridge, in a Qt application without a display.
//
// Only Qt's event loop runs in these tests; JUCE's dispatch loop is never
// entered. Where a test has to wait for something that another thread or a
// timer delivers, it blocks in Qt's loop until the thing has happened, with a
// deadline that fails the test (spinUntil). The deadline is the only use of
// the clock: no test sleeps for a fixed time or depends on how long anything
// took.

#include <catch2/catch_test_macros.hpp>

#include "lina/bridge/event_loop_bridge.hpp"
#include "lina/engine/engine.hpp"
#include "lina/engine/message_thread.hpp"
#include "lina/engine/testing/edit_building.hpp"
#include "lina/engine/testing/test_environment.hpp"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QThread>
#include <QTimer>

#include <juce_events/juce_events.h>

#include <unistd.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string_view>
#include <thread>

using lina::bridge::EventLoopBridge;
using lina::engine::MessageThread;
using namespace lina::engine::testing;

namespace {

/// The test case's scratch folder (see enterScratchFolder), with Qt told to
/// use its offscreen platform, so that a QGuiApplication opens no window and
/// needs no display server. Like enterScratchFolder it changes the
/// environment and must come first in a test case.
std::filesystem::path enterHeadlessQt(std::string_view name) {
  auto scratch = enterScratchFolder(name);
  // NOLINTNEXTLINE(concurrency-mt-unsafe)
  if (::setenv("QT_QPA_PLATFORM", "offscreen", 1) != 0) {
    throw std::runtime_error("could not set QT_QPA_PLATFORM");
  }
  return scratch;
}

/// A Qt application object together with the argument vector it refers to.
template <typename Application> struct QtApplication {
  int argc = 1;
  std::array<char, 12> name{"bridge-test"};
  std::array<char *, 2> argv{name.data(), nullptr};
  Application app{argc, argv.data()};
};

/// What every test case here starts with, in the start-up order of
/// docs/design/13-architecture.md: Qt, then JUCE's message thread. The bridge
/// comes next and is left to the test.
template <typename Application = QCoreApplication> struct Environment {
  explicit Environment(std::string_view name)
      : scratch(enterHeadlessQt(name)) {}

  AssertionCounter assertions;
  std::filesystem::path scratch;
  QtApplication<Application> qt;
  MessageThread messageThread;
};

/// Blocks in Qt's event loop until `done` returns true, and returns whether
/// it did before the deadline. The timeout is far longer than anything
/// waited for here takes, so it is reached only by a test that is failing.
bool spinUntil(const std::function<bool()> &done,
               std::chrono::milliseconds timeout = std::chrono::seconds(20)) {
  const QDeadlineTimer deadline(timeout);
  // Wakes the loop so that the deadline is looked at even if nothing else
  // arrives.
  QTimer wake;
  wake.start(std::chrono::milliseconds(20));
  while (!done() && !deadline.hasExpired()) {
    QCoreApplication::processEvents(QEventLoop::WaitForMoreEvents);
  }
  return done();
}

/// Lets Qt handle everything that is ready now, without waiting.
void drainQtEvents() {
  for (int round = 0; round < 10; ++round) {
    QCoreApplication::processEvents(QEventLoop::AllEvents);
  }
}

bool onMainThread(const MessageThread &messageThread) {
  return QThread::currentThread() == QCoreApplication::instance()->thread() &&
         messageThread.isCurrentThread();
}

struct TickCounter final : juce::Timer {
  explicit TickCounter(const MessageThread &thread) : messageThread(thread) {}
  ~TickCounter() override { stopTimer(); }

  TickCounter(const TickCounter &) = delete;
  TickCounter &operator=(const TickCounter &) = delete;
  TickCounter(TickCounter &&) = delete;
  TickCounter &operator=(TickCounter &&) = delete;

  void timerCallback() override {
    ++ticks;
    allOnMainThread = allOnMainThread && onMainThread(messageThread);
  }

  const MessageThread &messageThread;
  int ticks = 0;
  bool allOnMainThread = true;
};

/// A pipe whose ends are closed when the object goes.
class Pipe {
public:
  Pipe() {
    std::array<int, 2> ends{};
    if (::pipe(ends.data()) != 0) {
      throw std::runtime_error("pipe() failed");
    }
    readEnd_ = std::get<0>(ends);
    writeEnd_ = std::get<1>(ends);
  }
  ~Pipe() {
    ::close(readEnd_);
    ::close(writeEnd_);
  }

  Pipe(const Pipe &) = delete;
  Pipe &operator=(const Pipe &) = delete;
  Pipe(Pipe &&) = delete;
  Pipe &operator=(Pipe &&) = delete;

  [[nodiscard]] int readEnd() const { return readEnd_; }

  void writeByte() const {
    const char byte = 'x';
    REQUIRE(::write(writeEnd_, &byte, 1) == 1);
  }

  void readByte() const {
    char byte = 0;
    REQUIRE(::read(readEnd_, &byte, 1) == 1);
  }

private:
  int readEnd_ = -1;
  int writeEnd_ = -1;
};

/// A pipe and a count of how often its read end was reported readable.
struct PipeReader {
  /// What to register with JUCE for the read end: takes the byte that made
  /// it readable and counts the call.
  [[nodiscard]] auto callback() {
    return [this](int fd) {
      CHECK(fd == pipe.readEnd());
      pipe.readByte();
      ++calls;
    };
  }

  Pipe pipe;
  int calls = 0;
};

} // namespace

TEST_CASE("an asynchronous call to the message thread runs in Qt's loop",
          "[bridge]") {
  const Environment environment("bridge-call-async");
  {
    const EventLoopBridge bridge;
    // JUCE's message queue is watched from the start.
    CHECK(bridge.watchedDescriptorCount() >= 1);
    CHECK(bridge.dispatchCount() == 0);

    bool ran = false;
    bool ranOnMainThread = false;
    juce::MessageManager::callAsync([&] {
      ran = true;
      ranOnMainThread = onMainThread(environment.messageThread);
    });
    // Posted, not called: it waits for the loop.
    CHECK_FALSE(ran);
    REQUIRE(spinUntil([&] { return ran; }));
    CHECK(ranOnMainThread);
    CHECK(bridge.dispatchCount() >= 1);
  }
  CHECK(environment.assertions.count() == 0);
}

TEST_CASE("a call posted from another thread arrives on the main thread",
          "[bridge]") {
  const Environment environment("bridge-call-from-thread");
  {
    const EventLoopBridge bridge;
    std::atomic<bool> ran{false};
    std::atomic<bool> ranOnMainThread{false};
    std::thread poster([&] {
      juce::MessageManager::callAsync([&] {
        ranOnMainThread = onMainThread(environment.messageThread);
        ran = true;
      });
    });
    poster.join();
    REQUIRE(spinUntil([&] { return ran.load(); }));
    CHECK(ranOnMainThread.load());
  }
  CHECK(environment.assertions.count() == 0);
}

TEST_CASE("a JUCE timer and a Qt timer tick side by side in Qt's loop",
          "[bridge]") {
  const Environment environment("bridge-timers");
  {
    const EventLoopBridge bridge;
    TickCounter juceTimer(environment.messageThread);
    juceTimer.startTimer(1);

    int qtTicks = 0;
    QTimer qtTimer;
    QObject::connect(&qtTimer, &QTimer::timeout, &qtTimer,
                     [&qtTicks] { ++qtTicks; });
    qtTimer.start(std::chrono::milliseconds(1));

    // Both timers run on real time, so this waits for them: a few
    // milliseconds when all is well, the deadline when a timer is dead.
    REQUIRE(spinUntil([&] { return juceTimer.ticks >= 5 && qtTicks >= 5; }));
    CHECK(juceTimer.allOnMainThread);

    // A stopped JUCE timer stays stopped while Qt's goes on.
    juceTimer.stopTimer();
    drainQtEvents();
    const int ticksWhenStopped = juceTimer.ticks;
    const int qtTicksThen = qtTicks;
    REQUIRE(spinUntil([&] { return qtTicks >= qtTicksThen + 5; }));
    CHECK(juceTimer.ticks == ticksWhenStopped);
  }
  CHECK(environment.assertions.count() == 0);
}

TEST_CASE("another thread is granted the message-manager lock by Qt's loop",
          "[bridge]") {
  // The lock is how engine code on a worker thread touches message-thread
  // state. Granting it takes a blocking message that the message thread has
  // to dispatch, which here means Qt.
  const Environment environment("bridge-message-lock");
  {
    const EventLoopBridge bridge;
    std::atomic<bool> finished{false};
    std::atomic<bool> gained{false};
    std::thread worker([&] {
      {
        const juce::MessageManagerLock lock;
        gained = lock.lockWasGained();
      }
      finished = true;
    });
    // If the lock is not granted the worker stays blocked and cannot be
    // joined. Leaving this scope with it still running ends the process,
    // which fails the test as well.
    REQUIRE(spinUntil([&] { return finished.load(); }));
    worker.join();
    CHECK(gained.load());
  }
  CHECK(environment.assertions.count() == 0);
}

TEST_CASE("a descriptor registered on the main thread is watched at once",
          "[bridge]") {
  const Environment environment("bridge-descriptor-main-thread");
  {
    const EventLoopBridge bridge;
    const int before = bridge.watchedDescriptorCount();
    PipeReader reader;

    juce::LinuxEventLoop::registerFdCallback(reader.pipe.readEnd(),
                                             reader.callback());
    CHECK(bridge.watchedDescriptorCount() == before + 1);
    reader.pipe.writeByte();
    REQUIRE(spinUntil([&] { return reader.calls == 1; }));

    juce::LinuxEventLoop::unregisterFdCallback(reader.pipe.readEnd());
    // Dropped at once, before the descriptor could be closed.
    CHECK(bridge.watchedDescriptorCount() == before);
    reader.pipe.writeByte();
    drainQtEvents();
    CHECK(reader.calls == 1);
  }
  CHECK(environment.assertions.count() == 0);
}

TEST_CASE("a descriptor can be unregistered from inside its own callback",
          "[bridge]") {
  const Environment environment("bridge-descriptor-own-callback");
  {
    const EventLoopBridge bridge;
    const int before = bridge.watchedDescriptorCount();
    PipeReader reader;

    juce::LinuxEventLoop::registerFdCallback(
        reader.pipe.readEnd(), [read = reader.callback()](int fd) {
          read(fd);
          juce::LinuxEventLoop::unregisterFdCallback(fd);
        });
    reader.pipe.writeByte();
    REQUIRE(spinUntil([&] { return reader.calls == 1; }));
    CHECK(bridge.watchedDescriptorCount() == before);
    reader.pipe.writeByte();
    drainQtEvents();
    CHECK(reader.calls == 1);
  }
  CHECK(environment.assertions.count() == 0);
}

TEST_CASE("a descriptor registered from another thread is followed when "
          "Qt's loop next runs",
          "[bridge]") {
  // Tolerated, not supported: see the bridge's header.
  const Environment environment("bridge-descriptor-other-thread");
  {
    const EventLoopBridge bridge;
    const int before = bridge.watchedDescriptorCount();
    PipeReader reader;

    std::thread registrar([&] {
      juce::LinuxEventLoop::registerFdCallback(reader.pipe.readEnd(),
                                               reader.callback());
    });
    registrar.join();
    // The notifier is made on the main thread, when Qt's loop gets to it.
    CHECK(bridge.watchedDescriptorCount() == before);
    REQUIRE(spinUntil(
        [&] { return bridge.watchedDescriptorCount() == before + 1; }));
    reader.pipe.writeByte();
    REQUIRE(spinUntil([&] { return reader.calls == 1; }));

    std::thread unregistrar([&] {
      juce::LinuxEventLoop::unregisterFdCallback(reader.pipe.readEnd());
    });
    unregistrar.join();
    REQUIRE(
        spinUntil([&] { return bridge.watchedDescriptorCount() == before; }));
    reader.pipe.writeByte();
    drainQtEvents();
    CHECK(reader.calls == 1);
  }
  CHECK(environment.assertions.count() == 0);
}

TEST_CASE("messages wait while there is no bridge and bridges come and go",
          "[bridge]") {
  const Environment environment("bridge-come-and-go");
  int delivered = 0;

  // No bridge: Qt's loop runs, and JUCE's message stays where it is.
  juce::MessageManager::callAsync([&] { ++delivered; });
  drainQtEvents();
  CHECK(delivered == 0);

  {
    // A bridge finds what was posted before it existed.
    const EventLoopBridge bridge;
    REQUIRE(spinUntil([&] { return delivered == 1; }));
  }

  juce::MessageManager::callAsync([&] { ++delivered; });
  drainQtEvents();
  CHECK(delivered == 1);

  {
    const EventLoopBridge bridge;
    REQUIRE(spinUntil([&] { return delivered == 2; }));
    CHECK(bridge.dispatchCount() >= 1);
  }
  CHECK(environment.assertions.count() == 0);
}

TEST_CASE("a bridge needs the message thread", "[bridge]") {
  const AssertionCounter assertions;
  enterHeadlessQt("bridge-preconditions");
  const QtApplication<QCoreApplication> qt;

  // No message thread yet.
  CHECK_THROWS_AS(EventLoopBridge(), std::logic_error);

  {
    const MessageThread messageThread;
    // The message thread exists, but this is another thread.
    bool refused = false;
    std::thread other([&] {
      try {
        const EventLoopBridge bridge;
      } catch (const std::logic_error &) {
        refused = true;
      }
    });
    other.join();
    CHECK(refused);
    CHECK_NOTHROW(EventLoopBridge());
  }
  CHECK(assertions.count() == 0);
}

TEST_CASE("the engine starts, plays and shuts down inside a Qt application",
          "[bridge]") {
  // Start-up and shutdown in the order of docs/design/13-architecture.md,
  // with a QGuiApplication on Qt's offscreen platform, twice over: Qt, the
  // message thread, the bridge, the engine; and back.
  const AssertionCounter assertions;
  const auto scratch = enterHeadlessQt("bridge-engine");
  const QtApplication<QGuiApplication> qt;
  CHECK(QGuiApplication::platformName() == "offscreen");

  for (const char *run : {"first", "second"}) {
    CAPTURE(run);
    const MessageThread messageThread;
    const EventLoopBridge bridge;
    lina::engine::Engine engine({.storageFolder = scratch / run});
    lina::engine::testing::registerTestPlugins(engine);

    setTempo(engine, 120.0);
    auto &track = addTrack(engine);
    addToneInstrument(track);
    const std::array notes{
        Note{
            .pitch = 69,
            .startBeat = 0.0,
            .lengthBeats = 1.0,
            .velocity = 127,
        },
    };
    addMidiClip(track, 0.0, 1.0, notes);
    engine.openManualDevice({.sampleRate = 48000.0, .blockSize = 512});
    engine.play();
    const auto audio = engine.process(1024);
    CHECK(audio.channels.at(0).at(0) == 1.0F);

    // With the engine up, both loops' traffic still gets through: a JUCE
    // message, and a Qt timer.
    bool juceRan = false;
    bool qtRan = false;
    juce::MessageManager::callAsync([&] { juceRan = true; });
    QTimer::singleShot(std::chrono::milliseconds(0), &qt.app,
                       [&] { qtRan = true; });
    REQUIRE(spinUntil([&] { return juceRan && qtRan; }));
    CHECK(bridge.dispatchCount() >= 1);
  }
  CHECK(assertions.count() == 0);
}

TEST_CASE("a bridge needs a Qt application", "[bridge]") {
  const AssertionCounter assertions;
  enterHeadlessQt("bridge-no-qt-application");
  const MessageThread messageThread;

  // The message thread is there; Qt is not.
  CHECK_THROWS_AS(EventLoopBridge(), std::logic_error);
  CHECK(assertions.count() == 0);
}
