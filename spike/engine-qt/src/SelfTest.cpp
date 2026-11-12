#include "SelfTest.h"

#include "EditModels.h"
#include "EngineHost.h"
#include "JuceQtEventLoop.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QQuickItem>
#include <QQuickWindow>
#include <QThread>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <functional>
#include <thread>
#include <vector>

using namespace tracktion::literals;

namespace
{
struct Report
{
    void check (const char* name, bool ok, const QString& detail = {})
    {
        std::printf ("%s  %s%s%s\n", ok ? "PASS" : "FAIL", name,
                     detail.isEmpty() ? "" : "  -- ", detail.toUtf8().constData());
        std::fflush (stdout);
        failures += ok ? 0 : 1;
    }

    void info (const QString& text)
    {
        std::printf ("info  %s\n", text.toUtf8().constData());
        std::fflush (stdout);
    }

    int failures = 0;
};

/** Blocks in the Qt event loop (not in JUCE's) until pred() or the timeout. Whatever JUCE
    delivers during this time is delivered by Qt waking up on a JUCE fd. */
bool spinUntil (const std::function<bool()>& pred, int timeoutMs)
{
    QEventLoop loop;
    QTimer wake; // guarantees the deadline is re-checked even if nothing else happens
    wake.start (20);
    QDeadlineTimer deadline (timeoutMs);

    while (! pred() && ! deadline.hasExpired())
        loop.processEvents (QEventLoop::WaitForMoreEvents);

    return pred();
}

void spinFor (int ms)   { spinUntil ([] { return false; }, ms); }

bool onMainThread()
{
    return QThread::currentThread() == QCoreApplication::instance()->thread()
        && juce::MessageManager::getInstance()->isThisTheMessageThread();
}

QQuickItem* findItem (QQuickItem* root, const QString& objectName)
{
    if (root == nullptr)
        return nullptr;

    if (root->objectName() == objectName)
        return root;

    for (auto* child : root->childItems())
        if (auto* found = findItem (child, objectName))
            return found;

    return nullptr;
}

struct TickCounter final : juce::Timer
{
    void timerCallback() override   { ++ticks; allOnMain = allOnMain && onMainThread(); }
    int ticks = 0;
    bool allOnMain = true;
};

struct Updater final : juce::AsyncUpdater
{
    ~Updater() override                 { cancelPendingUpdate(); }
    void handleAsyncUpdate() override   { fired = true; onMain = onMainThread(); }
    bool fired = false, onMain = false;
};

struct ModelSpy
{
    explicit ModelSpy (QAbstractItemModel& m)
    {
        QObject::connect (&m, &QAbstractItemModel::dataChanged, &guard,
                          [this] (const QModelIndex& tl, const QModelIndex&, const QList<int>& roles)
                          { ++dataChanged; lastRow = tl.row(); lastRoles = roles; });
        QObject::connect (&m, &QAbstractItemModel::rowsInserted, &guard,
                          [this] (const QModelIndex&, int first, int) { ++inserted; lastRow = first; });
        QObject::connect (&m, &QAbstractItemModel::rowsRemoved, &guard,
                          [this] (const QModelIndex&, int first, int) { ++removed; lastRow = first; });
        QObject::connect (&m, &QAbstractItemModel::rowsMoved, &guard, [this] { ++moved; });
        QObject::connect (&m, &QAbstractItemModel::modelReset, &guard, [this] { ++resets; });
    }

    void clear()   { dataChanged = inserted = removed = moved = resets = 0; lastRow = -1; lastRoles.clear(); }

    QObject guard;
    int dataChanged = 0, inserted = 0, removed = 0, moved = 0, resets = 0, lastRow = -1;
    QList<int> lastRoles;
};
} // namespace

//==============================================================================
int runSelfTest (SelfTestContext& ctx)
{
    Report r;
    auto& edit = ctx.host.edit();
    auto& um = edit.getUndoManager();

    r.info (QStringLiteral ("event loop mode: %1, JUCE fds watched by Qt: %2")
                .arg (ctx.loop.mode() == JuceQtEventLoop::Mode::FdNotifiers ? "fd (QSocketNotifier)" : "poll (QTimer)")
                .arg (ctx.loop.watchedFdCount()));

    //========================================================================== event loop
    {
        bool fired = false, onMain = false;
        juce::MessageManager::callAsync ([&] { fired = true; onMain = onMainThread(); });
        const bool wasDeferred = ! fired;
        spinUntil ([&] { return fired; }, 1000);
        r.check ("juce::MessageManager::callAsync fires under the Qt loop", fired && onMain && wasDeferred,
                 QStringLiteral ("fired=%1 onQtMainThread=%2 deferred=%3").arg (fired).arg (onMain).arg (wasDeferred));
    }
    {
        std::atomic<bool> fired { false }, onMain { false };
        std::thread t ([&] { juce::MessageManager::callAsync ([&] { onMain = onMainThread(); fired = true; }); });
        t.join();
        spinUntil ([&] { return fired.load(); }, 1000);
        r.check ("callAsync posted from a background thread lands on the Qt main thread", fired && onMain);
    }
    {
        TickCounter timer;
        timer.startTimer (10);
        spinFor (500);
        timer.stopTimer();
        r.check ("juce::Timer ticks (10 ms timer, 500 ms)", timer.ticks >= 30 && timer.ticks <= 55 && timer.allOnMain,
                 QStringLiteral ("%1 ticks, ideal 50").arg (timer.ticks));
    }
    {
        Updater u;
        u.triggerAsyncUpdate();
        spinUntil ([&] { return u.fired; }, 1000);
        r.check ("juce::AsyncUpdater fires", u.fired && u.onMain);
    }
    {
        // A background thread taking the MessageManagerLock needs the message thread to
        // dispatch a blocking message; here that dispatch is done by Qt.
        std::atomic<bool> done { false }, gained { false };
        std::thread t ([&] { { juce::MessageManagerLock lock; gained = lock.lockWasGained(); } done = true; });
        spinUntil ([&] { return done.load(); }, 2000);
        const bool ok = done && gained;
        r.check ("juce::MessageManagerLock from a background thread is granted", ok);

        if (done)
            t.join();
        else
            t.detach();
    }
    {
        std::vector<double> ms;

        for (int i = 0; i < 200; ++i)
        {
            bool fired = false;
            QElapsedTimer clock;
            clock.start();
            qint64 ns = 0;
            juce::MessageManager::callAsync ([&] { ns = clock.nsecsElapsed(); fired = true; });
            spinUntil ([&] { return fired; }, 1000);
            ms.push_back (ns / 1.0e6);
        }

        std::sort (ms.begin(), ms.end());
        r.info (QStringLiteral ("callAsync round trip over 200 posts: median %1 ms, p95 %2 ms, max %3 ms")
                    .arg (ms[100], 0, 'f', 3).arg (ms[190], 0, 'f', 3).arg (ms.back(), 0, 'f', 3));
    }

    //========================================================================== QML
    auto* window = qobject_cast<QQuickWindow*> (ctx.qmlRoot);
    auto* rootItem = window != nullptr ? window->contentItem() : nullptr;
    spinFor (100); // let delegates incubate

    auto* trackList = findItem (rootItem, "trackList");
    auto* tempoField = findItem (rootItem, "tempoField");
    auto* positionLabel = findItem (rootItem, "positionLabel");
    auto* clipField = findItem (rootItem, "clipName_0_0");

    r.check ("Main.qml loads with the registered C++ types", window != nullptr && trackList != nullptr
                                                              && tempoField != nullptr && positionLabel != nullptr);
    r.check ("QML ListView sees the edit's 2 tracks, clip delegate exists",
             trackList != nullptr && trackList->property ("count").toInt() == 2 && clipField != nullptr,
             QStringLiteral ("count=%1").arg (trackList != nullptr ? trackList->property ("count").toInt() : -1));

    //========================================================================== ValueTree -> Qt
    auto* tracks = ctx.controller.tracks();
    auto* clips0 = tracks->clipsAt (0);
    auto* clips1 = tracks->clipsAt (1);

    r.check ("initial model contents (2 tracks, 1 clip each)",
             tracks->rowCount() == 2 && clips0 != nullptr && clips1 != nullptr
                 && clips0->rowCount() == 1 && clips1->rowCount() == 1
                 && clips0->data (clips0->index (0), ClipListModel::NameRole).toString() == "Sweep"
                 && clips1->data (clips1->index (0), ClipListModel::KindRole).toString() == "MIDICLIP");

    if (clips0 == nullptr || clips1 == nullptr)
    {
        r.check ("remaining checks skipped: models missing", false);
        return r.failures;
    }

    auto audioClip = ctx.host.audioTrack (0)->getClips()[0];
    {
        ModelSpy spy (*clips0);
        audioClip->setName ("Renamed by engine");
        const auto shown = clips0->data (clips0->index (0), ClipListModel::NameRole).toString();
        r.check ("engine-side clip rename -> dataChanged(name) on the Qt model",
                 spy.dataChanged == 1 && spy.lastRow == 0 && spy.lastRoles.contains (ClipListModel::NameRole)
                     && shown == "Renamed by engine",
                 QStringLiteral ("signals=%1 model says '%2'").arg (spy.dataChanged).arg (shown));

        const auto inQml = clipField != nullptr ? clipField->property ("text").toString() : QString();
        r.check ("... and the QML TextField bound to it shows the new name", inQml == "Renamed by engine",
                 QStringLiteral ("QML text '%1'").arg (inQml));
    }
    {
        ModelSpy spy (*clips1);
        auto extra = te::insertMIDIClip (*ctx.host.audioTrack (1), "Extra",
                                         te::TimeRange (5_tp, te::TimeDuration::fromSeconds (1.0)));
        const bool insertedOk = spy.inserted == 1 && spy.lastRow == 1 && clips1->rowCount() == 2
                                && clips1->data (clips1->index (1), ClipListModel::NameRole).toString() == "Extra";
        r.check ("engine-side clip insert -> rowsInserted", insertedOk,
                 QStringLiteral ("signals=%1 rows=%2").arg (spy.inserted).arg (clips1->rowCount()));

        spy.clear();
        extra->removeFromParent();
        r.check ("engine-side clip delete -> rowsRemoved", spy.removed == 1 && spy.lastRow == 1 && clips1->rowCount() == 1,
                 QStringLiteral ("signals=%1 rows=%2").arg (spy.removed).arg (clips1->rowCount()));
    }
    {
        ModelSpy spy (*tracks);
        edit.ensureNumberOfAudioTracks (3);
        const bool insertedOk = spy.inserted == 1 && tracks->rowCount() == 3 && tracks->clipsAt (2) != nullptr;
        r.check ("engine-side track insert -> rowsInserted, nested clip model created", insertedOk,
                 QStringLiteral ("signals=%1 rows=%2").arg (spy.inserted).arg (tracks->rowCount()));

        spy.clear();
        edit.deleteTrack (te::getAudioTracks (edit)[2]);
        spinFor (50); // deleteLater of the nested model
        r.check ("engine-side track delete -> rowsRemoved, no model reset",
                 spy.removed == 1 && tracks->rowCount() == 2 && spy.resets == 0 && tracks->clipsAt (0) == clips0,
                 QStringLiteral ("signals=%1 rows=%2 resets=%3").arg (spy.removed).arg (tracks->rowCount()).arg (spy.resets));

        // Reorder the two tracks directly in the tree, then put them back.
        spy.clear();
        const int a = edit.state.indexOf (ctx.host.audioTrack (0)->state);
        const int b = edit.state.indexOf (ctx.host.audioTrack (1)->state);
        edit.state.moveChild (b, a, nullptr);
        const bool swapped = tracks->clipsAt (1) == clips0 && tracks->clipsAt (0) == clips1
                             && tracks->data (tracks->index (0), TrackListModel::NameRole).toString() == "MIDI";
        edit.state.moveChild (a, b, nullptr);
        r.check ("engine-side track reorder -> rowsMoved, nested models follow",
                 swapped && spy.moved == 2 && spy.resets == 0 && tracks->clipsAt (0) == clips0
                     && tracks->data (tracks->index (0), TrackListModel::NameRole).toString() == "Audio",
                 QStringLiteral ("moved=%1 resets=%2").arg (spy.moved).arg (spy.resets));
    }

    um.clearUndoHistory();
    spinFor (50);

    //========================================================================== Qt -> edit -> undo
    {
        ModelSpy spy (*clips0);
        int undoSignals = 0;
        QObject guard;
        QObject::connect (&ctx.controller, &EditController::undoStateChanged, &guard, [&] { ++undoSignals; });

        const bool accepted = clips0->setData (clips0->index (0), QStringLiteral ("Renamed from Qt"), ClipListModel::NameRole);
        r.check ("Qt-side rename (QAbstractItemModel::setData) lands in te::Clip",
                 accepted && audioClip->getName() == "Renamed from Qt" && spy.dataChanged == 1,
                 QStringLiteral ("clip name '%1'").arg (toQString (audioClip->getName())));

        spinUntil ([&] { return undoSignals > 0; }, 1000);
        r.check ("UndoManager change reaches QML-facing canUndo (ChangeBroadcaster via the bridged loop)",
                 undoSignals > 0 && ctx.controller.canUndo() && ctx.controller.undoText() == "Rename clip",
                 QStringLiteral ("undoText '%1'").arg (ctx.controller.undoText()));

        spy.clear();
        ctx.controller.undo();
        const auto shown = clips0->data (clips0->index (0), ClipListModel::NameRole).toString();
        r.check ("undo reverts the rename in the edit and in the Qt model",
                 audioClip->getName() == "Renamed by engine" && shown == "Renamed by engine" && spy.dataChanged == 1,
                 QStringLiteral ("clip name '%1', model '%2'").arg (toQString (audioClip->getName()), shown));

        ctx.controller.redo();
        r.check ("redo re-applies it", audioClip->getName() == "Renamed from Qt");
        ctx.controller.undo();
    }
    {
        // Same thing, but driven the way the UI drives it: set the QML TextField's text and
        // emit its editingFinished signal, so the QML handler makes the C++ call.
        if (clipField != nullptr)
        {
            clipField->setProperty ("text", "Renamed in QML");
            QMetaObject::invokeMethod (clipField, "editingFinished");
        }

        r.check ("QML TextField edit -> ClipListModel.rename -> te::Clip", audioClip->getName() == "Renamed in QML",
                 QStringLiteral ("clip name '%1'").arg (toQString (audioClip->getName())));

        ctx.controller.undo();
        const auto inQml = clipField != nullptr ? clipField->property ("text").toString() : QString();
        r.check ("undo reverts it, QML field follows", audioClip->getName() == "Renamed by engine" && inQml == "Renamed by engine",
                 QStringLiteral ("clip '%1', QML '%2'").arg (toQString (audioClip->getName()), inQml));
    }
    {
        const double before = edit.tempoSequence.getTempo (0)->getBpm();
        int tempoSignals = 0;
        QObject guard;
        QObject::connect (&ctx.controller, &EditController::tempoChanged, &guard, [&] { ++tempoSignals; });

        if (tempoField != nullptr)
        {
            tempoField->setProperty ("text", "133.00");
            QMetaObject::invokeMethod (tempoField, "editingFinished");
        }

        const double after = edit.tempoSequence.getTempo (0)->getBpm();
        r.check ("QML tempo field -> EditController.tempo -> TempoSequence",
                 juce::approximatelyEqual (after, 133.0) && tempoSignals >= 1,
                 QStringLiteral ("bpm %1 -> %2").arg (before).arg (after));

        ctx.controller.undo();
        const double undone = edit.tempoSequence.getTempo (0)->getBpm();
        const auto inQml = tempoField != nullptr ? tempoField->property ("text").toString() : QString();
        r.check ("undo reverts the tempo, QML field follows",
                 juce::approximatelyEqual (undone, before) && inQml == QString::number (before, 'f', 2),
                 QStringLiteral ("bpm %1, QML '%2'").arg (undone).arg (inQml));
    }

    //========================================================================== transport, null device
    {
        int positionSignals = 0;
        QObject guard;
        QObject::connect (&ctx.controller, &EditController::positionChanged, &guard, [&] { ++positionSignals; });
        const auto labelBefore = positionLabel != nullptr ? positionLabel->property ("text").toString() : QString();

        ctx.controller.play();
        QElapsedTimer clock;
        clock.start();
        const bool advanced = spinUntil ([&] { return ctx.controller.playing() && ctx.controller.position() >= 0.5; }, 4000);
        const double wall = clock.elapsed() / 1000.0;
        const double pos = ctx.controller.position();
        const auto labelAfter = positionLabel != nullptr ? positionLabel->property ("text").toString() : QString();

        r.check ("transport plays on the null device; playhead reaches QML",
                 advanced && labelAfter != labelBefore && positionSignals >= 5,
                 QStringLiteral ("position %1 s after %2 s wall, %3 positionChanged signals, label '%4' -> '%5'")
                     .arg (pos, 0, 'f', 3).arg (wall, 0, 'f', 3).arg (positionSignals).arg (labelBefore, labelAfter));

        ctx.controller.stop();
        const bool stopped = spinUntil ([&] { return ! ctx.controller.playing(); }, 2000);
        r.check ("transport stops, playing property follows", stopped);
    }

    //========================================================================== plugin (optional)
    if (! ctx.pluginPath.isEmpty())
    {
        juce::String error;
        auto plugin = ctx.host.loadExternalPlugin (toJuceString (ctx.pluginPath), 0, error);
        auto* ext = dynamic_cast<te::ExternalPlugin*> (plugin.get());

        if (ext != nullptr)
            spinUntil ([&] { return ! ext->isInitialisingAsync(); }, 10000);

        auto* instance = ext != nullptr ? ext->getAudioPluginInstance() : nullptr;
        r.check ("plugin scan + instantiate on track 1 (editor NOT opened)", instance != nullptr,
                 instance != nullptr ? QStringLiteral ("'%1', hasEditor=%2").arg (toQString (instance->getName())).arg (instance->hasEditor())
                                     : toQString (error));
    }

    spinFor (100);
    const int assertions = ctx.host.logger().numAssertions.load();
    r.check ("no JUCE/Tracktion assertion was logged during the run", assertions == 0,
             QStringLiteral ("%1 assertion(s)").arg (assertions));

    r.info (QStringLiteral ("JUCE fds watched at end: %1, fd/poll dispatches: %2")
                .arg (ctx.loop.watchedFdCount()).arg (ctx.loop.dispatchCount()));
    std::printf ("%s  (%d failed)\n", r.failures == 0 ? "SELFTEST PASS" : "SELFTEST FAIL", r.failures);
    std::fflush (stdout);
    return r.failures;
}
