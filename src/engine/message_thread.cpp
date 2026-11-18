// SPDX-License-Identifier: GPL-3.0-or-later
#include "lina/engine/message_thread.hpp"

#include <juce_events/juce_events.h>

namespace lina::engine {

struct MessageThread::Impl {
  juce::ScopedJuceInitialiser_GUI initialiser;
};

MessageThread::MessageThread() : impl_(std::make_unique<Impl>()) {}

MessageThread::~MessageThread() = default;

// The object is the proof that a message manager exists, so this stays a
// member although it reads no member.
// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
bool MessageThread::isCurrentThread() const {
  return juce::MessageManager::getInstance()->isThisTheMessageThread();
}

} // namespace lina::engine
