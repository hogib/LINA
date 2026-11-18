// SPDX-License-Identifier: GPL-3.0-or-later
//
// The one file in which Qt and JUCE meet (docs/design/13-architecture.md).
#include "lina/bridge/event_loop_bridge.hpp"

#include <QCoreApplication>
#include <QObject>
#include <QSocketNotifier>
#include <QThread>

#include <juce_events/juce_events.h>
// Marked internal by JUCE and not included by juce_events.h; JUCE's own VST3
// plugin wrapper includes it by path in the same way.
#include <juce_events/native/juce_EventLoopInternal_linux.h>

#include <algorithm>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace lina::bridge {

class EventLoopBridge::Impl final
    : public QObject,
      private juce::LinuxEventLoopInternal::Listener {
public:
  Impl() {
    if (QCoreApplication::instance() == nullptr) {
      throw std::logic_error(
          "EventLoopBridge needs a QCoreApplication: socket notifiers do "
          "nothing without one");
    }
    // The thread that created JUCE's message manager is its message thread,
    // and it has to be the thread whose Qt event loop does the dispatching.
    const auto *messages = juce::MessageManager::getInstanceWithoutCreating();
    if (messages == nullptr || !messages->isThisTheMessageThread()) {
      throw std::logic_error(
          "EventLoopBridge must be created on the message thread, after "
          "lina::engine::MessageThread");
    }
    juce::LinuxEventLoopInternal::registerLinuxEventLoopListener(*this);
    syncNotifiers();
  }

  ~Impl() override {
    juce::LinuxEventLoopInternal::deregisterLinuxEventLoopListener(*this);
  }

  Impl(const Impl &) = delete;
  Impl &operator=(const Impl &) = delete;
  Impl(Impl &&) = delete;
  Impl &operator=(Impl &&) = delete;

  [[nodiscard]] int watchedDescriptorCount() const {
    return static_cast<int>(notifiers_.size());
  }

  [[nodiscard]] std::uint64_t dispatchCount() const { return dispatchCount_; }

private:
  /// JUCE calls this from whichever thread registers or unregisters a
  /// descriptor, and before the caller closes a descriptor it has just
  /// unregistered. On the main thread the notifier therefore has to go now,
  /// or Qt would keep polling a closed descriptor. From another thread the
  /// work is queued to the main thread: a notifier can be created, disabled
  /// and deleted only on the thread it belongs to (QSocketNotifier::setEnabled
  /// called from elsewhere queues itself too), so there is no way to stop it
  /// from here. See the header for what that leaves open.
  void fdCallbacksChanged() override {
    if (QThread::currentThread() == thread()) {
      syncNotifiers();
    } else {
      QMetaObject::invokeMethod(
          this, [this] { syncNotifiers(); }, Qt::QueuedConnection);
    }
  }

  /// Makes the set of notifiers equal to the set of descriptors JUCE has
  /// registered at this moment.
  void syncNotifiers() {
    const std::vector<int> wanted =
        juce::LinuxEventLoopInternal::getRegisteredFds();

    std::erase_if(notifiers_, [&wanted](const auto &entry) {
      if (std::ranges::find(wanted, entry.first) != wanted.end()) {
        return false;
      }
      // This may run inside the notifier's own activated() signal, where it
      // cannot be deleted yet. It stays a child of this object until Qt
      // deletes it, from the event loop or with this object.
      entry.second->setEnabled(false);
      entry.second->deleteLater();
      return true;
    });

    for (const int fd : wanted) {
      if (notifiers_.contains(fd)) {
        continue;
      }
      // JUCE does not report the poll events a descriptor was registered
      // for; every registration in JUCE 8.0.6 is for reading.
      //
      // Owned by this QObject as its child, which is Qt's ownership idiom:
      // hence the bare `new`.
      // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
      auto *notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
      connect(notifier, &QSocketNotifier::activated, this,
              [this, fd] { dispatch(fd); });
      notifiers_.emplace(fd, notifier);
    }
  }

  void dispatch(int fd) {
    ++dispatchCount_;
    // For the message queue's descriptor this delivers every pending message.
    juce::LinuxEventLoopInternal::invokeEventLoopCallbackForFd(fd);
  }

  /// Not owning: every notifier is a child of this QObject.
  std::unordered_map<int, QSocketNotifier *> notifiers_;
  std::uint64_t dispatchCount_ = 0;
};

EventLoopBridge::EventLoopBridge() : impl_(std::make_unique<Impl>()) {}

EventLoopBridge::~EventLoopBridge() = default;

int EventLoopBridge::watchedDescriptorCount() const {
  return impl_->watchedDescriptorCount();
}

std::uint64_t EventLoopBridge::dispatchCount() const {
  return impl_->dispatchCount();
}

} // namespace lina::bridge
