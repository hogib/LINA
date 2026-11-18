// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LINA_ENGINE_MESSAGE_THREAD_HPP
#define LINA_ENGINE_MESSAGE_THREAD_HPP

#include <memory>

namespace lina::engine {

/// Makes the calling thread the engine framework's message thread for as long
/// as the object lives.
///
/// Create it on the main thread before any Engine and before the event-loop
/// bridge, and destroy it after both (docs/design/13-architecture.md,
/// "Start-up and shutdown"). It starts no event loop of its own: messages
/// posted to the message thread are delivered only when something services
/// them, which in the application is the bridge inside Qt's event loop.
class MessageThread {
public:
  MessageThread();
  ~MessageThread();

  MessageThread(const MessageThread &) = delete;
  MessageThread &operator=(const MessageThread &) = delete;
  MessageThread(MessageThread &&) = delete;
  MessageThread &operator=(MessageThread &&) = delete;

  /// True when the calling thread is the message thread.
  [[nodiscard]] bool isCurrentThread() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace lina::engine

#endif // LINA_ENGINE_MESSAGE_THREAD_HPP
