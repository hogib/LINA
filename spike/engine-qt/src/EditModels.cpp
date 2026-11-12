#include "EditModels.h"

#include <QThread>

#include <algorithm>

QString toQString (const juce::String& s)       { return QString::fromUtf8 (s.toRawUTF8()); }
juce::String toJuceString (const QString& s)    { return juce::String::fromUTF8 (s.toUtf8().constData()); }

//==============================================================================
ValueTreeChildListModel::ValueTreeChildListModel (juce::ValueTree tree, QObject* parent)
    : QAbstractListModel (parent), parentTree (std::move (tree))
{
    parentTree.addListener (this);
}

ValueTreeChildListModel::~ValueTreeChildListModel()
{
    parentTree.removeListener (this);
}

void ValueTreeChildListModel::populate()
{
    rebuildRows();
    rowsReset();
}

int ValueTreeChildListModel::rowCount (const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : numRows();
}

int ValueTreeChildListModel::rowOf (const juce::ValueTree& v) const
{
    const auto it = std::find (rows.begin(), rows.end(), v);
    return it == rows.end() ? -1 : static_cast<int> (it - rows.begin());
}

void ValueTreeChildListModel::rebuildRows()
{
    rows.clear();

    for (const auto& child : parentTree)
        if (accepts (child))
            rows.push_back (child);
}

void ValueTreeChildListModel::resetAll()
{
    beginResetModel();
    rebuildRows();
    rowsReset();
    endResetModel();
}

void ValueTreeChildListModel::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    Q_ASSERT (QThread::currentThread() == thread());

    // The listener hears every descendant of parentTree; only direct, accepted children are rows.
    if (tree.getParent() != parentTree)
        return;

    const int row = rowOf (tree);

    if (row < 0)
        return;

    if (const auto roles = rolesForProperty (property); ! roles.isEmpty())
        Q_EMIT dataChanged (index (row), index (row), roles);
}

void ValueTreeChildListModel::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child)
{
    Q_ASSERT (QThread::currentThread() == thread());

    if (parent != parentTree || ! accepts (child))
        return;

    const int childIndex = parent.indexOf (child);
    const auto pos = std::find_if (rows.begin(), rows.end(),
                                   [&] (const juce::ValueTree& r) { return parent.indexOf (r) > childIndex; });
    const int row = static_cast<int> (pos - rows.begin());

    beginInsertRows ({}, row, row);
    rows.insert (pos, child);
    rowInserted (row);
    endInsertRows();
}

void ValueTreeChildListModel::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int)
{
    Q_ASSERT (QThread::currentThread() == thread());

    if (parent != parentTree)
        return;

    const int row = rowOf (child);

    if (row < 0)
        return;

    beginRemoveRows ({}, row, row);
    rows.erase (rows.begin() + row);
    rowRemoved (row);
    endRemoveRows();
}

void ValueTreeChildListModel::valueTreeChildOrderChanged (juce::ValueTree& parent, int, int newIndex)
{
    Q_ASSERT (QThread::currentThread() == thread());

    if (parent != parentTree)
        return;

    // Tracktion reorders children of the Edit tree routinely (e.g. while inserting a track),
    // and most of those moves concern children that are not rows of this model. Resetting
    // here would throw away every nested model and every QML delegate, so: ignore moves of
    // non-rows, and translate a moved row into beginMoveRows.
    const auto moved = parent.getChild (newIndex);
    const int from = rowOf (moved);

    if (from < 0)
        return;

    int to = 0;

    for (const auto& r : rows)
        if (r != moved && parent.indexOf (r) < newIndex)
            ++to;

    if (to == from)
        return;

    beginMoveRows ({}, from, from, {}, to > from ? to + 1 : to);
    rows.erase (rows.begin() + from);
    rows.insert (rows.begin() + to, moved);
    rowMoved (from, to);
    endMoveRows();
}

void ValueTreeChildListModel::valueTreeRedirected (juce::ValueTree& tree)
{
    if (tree == parentTree)
        resetAll();
}

//==============================================================================
ClipListModel::ClipListModel (te::Edit& e, juce::ValueTree trackState, QObject* parent)
    : ValueTreeChildListModel (std::move (trackState), parent), edit (e)
{
    populate();
}

bool ClipListModel::accepts (const juce::ValueTree& v) const
{
    return te::Clip::isClipState (v);
}

QList<int> ClipListModel::rolesForProperty (const juce::Identifier& p) const
{
    if (p == te::IDs::name)     return { NameRole, Qt::DisplayRole };
    if (p == te::IDs::start)    return { StartRole };
    if (p == te::IDs::length)   return { LengthRole };
    return {};
}

QHash<int, QByteArray> ClipListModel::roleNames() const
{
    return { { NameRole, "name" }, { KindRole, "kind" }, { StartRole, "start" }, { LengthRole, "length" } };
}

QVariant ClipListModel::data (const QModelIndex& idx, int role) const
{
    if (! idx.isValid() || idx.row() >= numRows())
        return {};

    const auto& v = rowState (idx.row());

    switch (role)
    {
        case Qt::DisplayRole:
        case NameRole:      return toQString (v[te::IDs::name].toString());
        case KindRole:      return toQString (v.getType().toString());
        case StartRole:     return static_cast<double> (v[te::IDs::start]);
        case LengthRole:    return static_cast<double> (v[te::IDs::length]);
        default:            return {};
    }
}

Qt::ItemFlags ClipListModel::flags (const QModelIndex& idx) const
{
    return ValueTreeChildListModel::flags (idx) | Qt::ItemIsEditable;
}

bool ClipListModel::setData (const QModelIndex& idx, const QVariant& value, int role)
{
    if (role != NameRole && role != Qt::EditRole)
        return false;

    return rename (idx.row(), value.toString());
}

te::Clip* ClipListModel::clipAt (int row) const
{
    if (row < 0 || row >= numRows())
        return nullptr;

    return te::findClipForState (edit, rowState (row));
}

bool ClipListModel::rename (int row, const QString& newName)
{
    auto* clip = clipAt (row);

    if (clip == nullptr)
        return false;

    // Write through the Tracktion object, not the raw tree: te::Clip::setName has side
    // effects (optionally renaming takes) and its CachedValue carries the Edit's UndoManager.
    // dataChanged is emitted from valueTreePropertyChanged, synchronously, inside this call.
    edit.getUndoManager().beginNewTransaction ("Rename clip");
    clip->setName (toJuceString (newName));
    return true;
}

//==============================================================================
TrackListModel::TrackListModel (te::Edit& e, QObject* parent)
    : ValueTreeChildListModel (e.state, parent), edit (e)
{
    populate();
}

TrackListModel::~TrackListModel() = default;

bool TrackListModel::accepts (const juce::ValueTree& v) const
{
    // Top-level audio/MIDI tracks only. Tempo, marker, chord, arranger and master tracks are
    // siblings in the same tree; folder tracks nest and would need a tree model.
    return v.hasType (te::IDs::TRACK);
}

QList<int> TrackListModel::rolesForProperty (const juce::Identifier& p) const
{
    if (p == te::IDs::name)
        return { NameRole, Qt::DisplayRole };

    return {};
}

QHash<int, QByteArray> TrackListModel::roleNames() const
{
    return { { NameRole, "name" }, { ClipsRole, "clips" } };
}

QVariant TrackListModel::data (const QModelIndex& idx, int role) const
{
    if (! idx.isValid() || idx.row() >= numRows())
        return {};

    switch (role)
    {
        case Qt::DisplayRole:
        case NameRole:      return toQString (rowState (idx.row())[te::IDs::name].toString());
        case ClipsRole:     return QVariant::fromValue (clipsAt (idx.row()));
        default:            return {};
    }
}

ClipListModel* TrackListModel::clipsAt (int row) const
{
    return row >= 0 && row < static_cast<int> (clipModels.size()) ? clipModels[static_cast<size_t> (row)] : nullptr;
}

void TrackListModel::rowInserted (int row)
{
    clipModels.insert (clipModels.begin() + row, new ClipListModel (edit, rowState (row), this));
}

void TrackListModel::rowRemoved (int row)
{
    // deleteLater: a QML delegate may still be bound to the sub-model during this call stack.
    clipModels[static_cast<size_t> (row)]->deleteLater();
    clipModels.erase (clipModels.begin() + row);
}

void TrackListModel::rowMoved (int from, int to)
{
    auto* m = clipModels[static_cast<size_t> (from)];
    clipModels.erase (clipModels.begin() + from);
    clipModels.insert (clipModels.begin() + to, m);
}

void TrackListModel::rowsReset()
{
    for (auto* m : clipModels)
        m->deleteLater();

    clipModels.clear();

    for (int i = 0; i < numRows(); ++i)
        clipModels.push_back (new ClipListModel (edit, rowState (i), this));
}

//==============================================================================
EditController::EditController (te::Edit& e, QObject* parent)
    : QObject (parent),
      edit (e),
      tempoSequenceState (e.state.getChildWithName (te::IDs::TEMPOSEQUENCE))
{
    trackModel = new TrackListModel (edit, this);

    tempoSequenceState.addListener (this);
    edit.getTransport().addChangeListener (this);    // ChangeBroadcaster -> AsyncUpdater -> message queue
    edit.getUndoManager().addChangeListener (this);

    // High-rate data is *pulled* at UI rate rather than pushed per audio block.
    positionTimer.setInterval (33);
    connect (&positionTimer, &QTimer::timeout, this, [this] { pollTransport(); });
    positionTimer.start();
}

EditController::~EditController()
{
    edit.getUndoManager().removeChangeListener (this);
    edit.getTransport().removeChangeListener (this);
    tempoSequenceState.removeListener (this);
}

double EditController::tempo() const
{
    return edit.tempoSequence.getTempo (0)->getBpm();
}

void EditController::setTempo (double bpm)
{
    bpm = std::clamp (bpm, 20.0, 300.0);

    if (juce::approximatelyEqual (bpm, tempo()))
        return;

    edit.getUndoManager().beginNewTransaction ("Set tempo");
    edit.tempoSequence.getTempo (0)->setBpm (bpm);
}

bool EditController::canUndo() const    { return edit.getUndoManager().canUndo(); }
bool EditController::canRedo() const    { return edit.getUndoManager().canRedo(); }
QString EditController::undoText() const { return toQString (edit.getUndoManager().getUndoDescription()); }

void EditController::play()             { edit.getTransport().play (false); }
void EditController::stop()             { edit.getTransport().stop (false, false); }
void EditController::undo()             { edit.undo(); }
void EditController::redo()             { edit.redo(); }

void EditController::valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier& property)
{
    if (property == te::IDs::bpm)
        Q_EMIT tempoChanged();
}

void EditController::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &edit.getUndoManager())
        Q_EMIT undoStateChanged();
    else
        pollTransport();
}

void EditController::pollTransport()
{
    auto& transport = edit.getTransport();

    if (const double p = transport.getPosition().inSeconds(); ! juce::exactlyEqual (p, lastPosition))
    {
        lastPosition = p;
        Q_EMIT positionChanged();
    }

    if (const bool playingNow = transport.isPlaying(); playingNow != lastPlaying)
    {
        lastPlaying = playingNow;
        Q_EMIT playingChanged();
    }
}
