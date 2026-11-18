// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LINA_BRIDGE_EVENT_LOOP_BRIDGE_HPP
#define LINA_BRIDGE_EVENT_LOOP_BRIDGE_HPP

#include <cstdint>
#include <memory>

namespace lina::bridge {

/// Lets Qt's event loop serve as the engine framework's message loop
/// (docs/design/13-architecture.md, "Threads": one event loop, and Qt owns
/// it).
///
/// On Linux, JUCE's message loop is a poll() over a few file descriptors:
/// the wake-up end of its message queue, which carries everything posted to
/// the message thread (asynchronous calls, async updaters, change
/// broadcasters and every tick of a juce::Timer); its X11 connection once it
/// has one; and descriptors that hosted plugins register. While a bridge
/// lives, each of those descriptors is watched by a QSocketNotifier, and when
/// one becomes readable JUCE's own handler for it runs inside Qt's loop.
/// JUCE's dispatch loop is never run.
///
/// Order of construction on the main thread: the QCoreApplication, then
/// lina::engine::MessageThread, then the bridge, then the Engine. Destruction
/// runs in reverse; in particular the bridge goes before the MessageThread.
///
/// Descriptors are meant to be registered and unregistered on the main
/// thread, where the bridge follows at once. From another thread it is
/// tolerated, not supported:
///   - JUCE notifies the bridge through a listener list that it does not
///     synchronise, so such a call must not run while a bridge is being
///     created or destroyed.
///   - The bridge learns of the change only when Qt's loop next runs. A
///     descriptor that was unregistered and closed in between is polled by
///     Qt once more; Qt then reports an invalid socket and disables the
///     notifier itself. If the number has already been reused for another
///     open file, that file's readiness wakes the loop until the bridge
///     catches up. Nothing is delivered in either case, because JUCE ignores
///     a descriptor it no longer has registered.
///
/// The bridge needs a QCoreApplication and throws std::logic_error without
/// one, as it does when it is created off the message thread or before a
/// MessageThread exists.
///
/// The bridge relies on juce::LinuxEventLoopInternal, a JUCE header marked
/// internal, and has to be checked again on every JUCE upgrade.
class EventLoopBridge {
public:
  EventLoopBridge();
  ~EventLoopBridge();

  EventLoopBridge(const EventLoopBridge &) = delete;
  EventLoopBridge &operator=(const EventLoopBridge &) = delete;
  EventLoopBridge(EventLoopBridge &&) = delete;
  EventLoopBridge &operator=(EventLoopBridge &&) = delete;

  /// How many of the framework's file descriptors Qt is watching now.
  [[nodiscard]] int watchedDescriptorCount() const;

  /// How many times a descriptor became readable and was handed to the
  /// framework since the bridge was created.
  [[nodiscard]] std::uint64_t dispatchCount() const;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace lina::bridge

#endif // LINA_BRIDGE_EVENT_LOOP_BRIDGE_HPP
