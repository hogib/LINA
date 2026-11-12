#include "JuceQtEventLoop.h"

#include <QSocketNotifier>
#include <QThread>
#include <QTimer>

#include <unordered_set>

JuceQtEventLoop::JuceQtEventLoop (Mode m, QObject* parent)
    : QObject (parent), loopMode (m)
{
    // The thread that created the MessageManager singleton is "the message thread" as far
    // as every JUCE_ASSERT_MESSAGE_THREAD is concerned. It has to be this (Qt main) thread.
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());

    if (loopMode == Mode::FdNotifiers)
    {
        juce::LinuxEventLoopInternal::registerLinuxEventLoopListener (*this);
        syncNotifiers();
    }
    else
    {
        pollTimer = new QTimer (this);
        pollTimer->setTimerType (Qt::PreciseTimer);
        pollTimer->setInterval (5);
        connect (pollTimer, &QTimer::timeout, this, [this]
        {
            // Dispatches whatever is pending, and sleeps 1 ms if nothing was (JUCE's choice).
            juce::MessageManager::getInstance()->runDispatchLoopUntil (0);
            ++numDispatches;
        });
        pollTimer->start();
    }
}

JuceQtEventLoop::~JuceQtEventLoop()
{
    if (loopMode == Mode::FdNotifiers)
        juce::LinuxEventLoopInternal::deregisterLinuxEventLoopListener (*this);

    qDeleteAll (notifiers);
}

void JuceQtEventLoop::fdCallbacksChanged()
{
    // JUCE calls this synchronously from registerFdCallback / unregisterFdCallback, i.e.
    // *before* the caller close()s an fd it has just unregistered. When that happens on the
    // main thread we must drop the notifier right now, otherwise Qt keeps polling a dead fd.
    if (QThread::currentThread() == thread())
        syncNotifiers();
    else
        QMetaObject::invokeMethod (this, [this] { syncNotifiers(); }, Qt::QueuedConnection);
}

void JuceQtEventLoop::syncNotifiers()
{
    const auto fds = juce::LinuxEventLoopInternal::getRegisteredFds();
    const std::unordered_set<int> wanted (fds.begin(), fds.end());

    for (auto it = notifiers.begin(); it != notifiers.end();)
    {
        if (wanted.contains (it.key()))
        {
            ++it;
            continue;
        }

        // May be running inside this notifier's own activated() signal.
        it.value()->setEnabled (false);
        it.value()->deleteLater();
        it = notifiers.erase (it);
    }

    for (const int fd : fds)
    {
        if (notifiers.contains (fd))
            continue;

        // getRegisteredFds() does not report the poll event mask the fd was registered
        // with. Every registration in this JUCE version uses the default (POLLIN).
        auto* n = new QSocketNotifier (fd, QSocketNotifier::Read, this);
        connect (n, &QSocketNotifier::activated, this, [this, fd] { dispatchFd (fd); });
        notifiers.insert (fd, n);
    }
}

void JuceQtEventLoop::dispatchFd (int fd)
{
    ++numDispatches;

    // For the message-queue fd this drains every pending juce message in one go.
    juce::LinuxEventLoopInternal::invokeEventLoopCallbackForFd (fd);
}
