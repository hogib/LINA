#pragma once

// Event loop glue: Qt owns the main loop, JUCE's message thread machinery is driven from it.
//
// On Linux, JUCE's "message loop" is nothing more than poll() over a small set of file
// descriptors held by a private singleton (InternalRunLoop in juce_Messaging_linux.cpp):
//   - one end of a socketpair that MessageManager::postMessageToSystemQueue writes a byte to
//     (this carries callAsync, AsyncUpdater, ChangeBroadcaster, and juce::Timer, whose timer
//     thread posts a message per tick),
//   - the X11 connection fd, once juce_gui_basics opens a display,
//   - any fds a hosted VST3 plugin registers through Steinberg::Linux::IRunLoop.
//
// JUCE exposes exactly the hooks a foreign host loop needs, because JUCE-built VST3 plugins
// have the same problem inside a host: juce::LinuxEventLoopInternal (list fds, be told when
// the set changes, dispatch the callback of one fd). That struct is marked @internal and its
// header is NOT pulled in by <juce_events/juce_events.h>; it has to be included by path, the
// way juce_audio_plugin_client_VST3.cpp does.
//
// Mode::FdNotifiers (primary): one QSocketNotifier per JUCE fd.
// Mode::PollTimer  (alternative, kept for comparison): a QTimer that calls the public
//   MessageManager::runDispatchLoopUntil(0); needs JUCE_MODAL_LOOPS_PERMITTED=1.

#include <QHash>
#include <QObject>

#include <juce_events/juce_events.h>
#include <juce_events/native/juce_EventLoopInternal_linux.h> // @internal JUCE header

class QSocketNotifier;
class QTimer;

class JuceQtEventLoop final : public QObject,
                              private juce::LinuxEventLoopInternal::Listener
{
    Q_OBJECT

public:
    enum class Mode { FdNotifiers, PollTimer };

    /** Must be created on the thread that constructed juce::MessageManager (the Qt main
        thread), after ScopedJuceInitialiser_GUI and after QCoreApplication. */
    explicit JuceQtEventLoop (Mode, QObject* parent = nullptr);

    /** Must be destroyed before ScopedJuceInitialiser_GUI (the listener list lives in a
        JUCE singleton that MessageManager shutdown deletes). */
    ~JuceQtEventLoop() override;

    Mode mode() const noexcept            { return loopMode; }
    int watchedFdCount() const noexcept   { return static_cast<int> (notifiers.size()); }
    quint64 dispatchCount() const noexcept { return numDispatches; }

private:
    void fdCallbacksChanged() override; // may be called from any thread
    void syncNotifiers();
    void dispatchFd (int fd);

    const Mode loopMode;
    QHash<int, QSocketNotifier*> notifiers;
    QTimer* pollTimer = nullptr;
    quint64 numDispatches = 0;
};
