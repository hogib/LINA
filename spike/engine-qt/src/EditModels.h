#pragma once

// Model bridge: juce::ValueTree (the Edit's state) -> QAbstractListModel -> QML, and QML
// actions -> Tracktion model objects -> juce::UndoManager.
//
// Threading rule the whole file rests on: Tracktion mutates Edit ValueTrees only on the JUCE
// message thread, ValueTree listeners are called synchronously on the mutating thread, and
// the JUCE message thread *is* the Qt GUI thread. So listener callbacks may emit Qt model
// signals directly. Nothing here is safe from any other thread.

#include <QAbstractListModel>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include <tracktion_engine/tracktion_engine.h>

#include <memory>
#include <vector>

namespace te = tracktion;

QString toQString (const juce::String&);
juce::String toJuceString (const QString&);

//==============================================================================
/** Shared glue: a list model over "those children of one ValueTree that pass a filter".

    Why a shadow row list is needed rather than indexing the tree directly:
      - ValueTree notifies *after* a child was added/removed; Qt wants beginInsertRows /
        beginRemoveRows *before* the model's data changes. With rows held here, "the model's
        data" is this vector, which we change between begin and end.
      - A Tracktion tree mixes child kinds (a TRACK holds clips, plugins, automation...), so
        row != child index.
*/
class ValueTreeChildListModel : public QAbstractListModel,
                                protected juce::ValueTree::Listener
{
    Q_OBJECT

public:
    ~ValueTreeChildListModel() override;

    int rowCount (const QModelIndex& parent = {}) const override;

protected:
    ValueTreeChildListModel (juce::ValueTree parentTree, QObject* parent);

    /** Subclasses call this at the end of their constructor (it uses the virtuals). */
    void populate();

    virtual bool accepts (const juce::ValueTree& child) const = 0;
    virtual QList<int> rolesForProperty (const juce::Identifier&) const = 0;
    virtual void rowInserted (int /*row*/) {}
    virtual void rowRemoved (int /*row*/) {}
    virtual void rowMoved (int /*from*/, int /*to*/) {}
    virtual void rowsReset() {}

    const juce::ValueTree& rowState (int row) const   { return rows[static_cast<size_t> (row)]; }
    int rowOf (const juce::ValueTree&) const;
    int numRows() const                               { return static_cast<int> (rows.size()); }

private:
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override;
    void valueTreeRedirected (juce::ValueTree&) override;

    void rebuildRows();
    void resetAll();

    // The listener is attached to *this handle*, not to the shared tree node: the handle has
    // to stay alive, and must not be reassigned, for as long as callbacks are wanted.
    juce::ValueTree parentTree;
    std::vector<juce::ValueTree> rows;
};

//==============================================================================
class ClipListModel final : public ValueTreeChildListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE ("Owned by TrackListModel")

public:
    enum Roles { NameRole = Qt::UserRole + 1, KindRole, StartRole, LengthRole };

    ClipListModel (te::Edit&, juce::ValueTree trackState, QObject* parent);

    QVariant data (const QModelIndex&, int role) const override;
    bool setData (const QModelIndex&, const QVariant&, int role) override;
    Qt::ItemFlags flags (const QModelIndex&) const override;
    QHash<int, QByteArray> roleNames() const override;

    /** Renames through te::Clip::setName inside its own undo transaction. */
    Q_INVOKABLE bool rename (int row, const QString& newName);

    te::Clip* clipAt (int row) const;

private:
    bool accepts (const juce::ValueTree&) const override;
    QList<int> rolesForProperty (const juce::Identifier&) const override;

    te::Edit& edit;
};

//==============================================================================
class TrackListModel final : public ValueTreeChildListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE ("Owned by EditController")

public:
    enum Roles { NameRole = Qt::UserRole + 1, ClipsRole };

    TrackListModel (te::Edit&, QObject* parent);
    ~TrackListModel() override;

    QVariant data (const QModelIndex&, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    ClipListModel* clipsAt (int row) const;

private:
    bool accepts (const juce::ValueTree&) const override;
    QList<int> rolesForProperty (const juce::Identifier&) const override;
    void rowInserted (int row) override;
    void rowRemoved (int row) override;
    void rowMoved (int from, int to) override;
    void rowsReset() override;

    te::Edit& edit;
    std::vector<ClipListModel*> clipModels; // parallel to the rows, QObject-parented to this
};

//==============================================================================
/** Transport, tempo and undo, as one QObject for QML. */
class EditController final : public QObject,
                             private juce::ValueTree::Listener,
                             private juce::ChangeListener
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE ("Created in C++")

    Q_PROPERTY (TrackListModel* tracks READ tracks CONSTANT)
    Q_PROPERTY (double tempo READ tempo WRITE setTempo NOTIFY tempoChanged)
    Q_PROPERTY (double position READ position NOTIFY positionChanged)
    Q_PROPERTY (bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY (bool canUndo READ canUndo NOTIFY undoStateChanged)
    Q_PROPERTY (bool canRedo READ canRedo NOTIFY undoStateChanged)
    Q_PROPERTY (QString undoText READ undoText NOTIFY undoStateChanged)

public:
    explicit EditController (te::Edit&, QObject* parent = nullptr);
    ~EditController() override;

    TrackListModel* tracks() const   { return trackModel; }

    double tempo() const;
    void setTempo (double bpm);

    double position() const          { return lastPosition; }
    bool playing() const             { return lastPlaying; }
    bool canUndo() const;
    bool canRedo() const;
    QString undoText() const;

    Q_INVOKABLE void play();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

Q_SIGNALS:
    void tempoChanged();
    void positionChanged();
    void playingChanged();
    void undoStateChanged();

private:
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void pollTransport();

    te::Edit& edit;
    juce::ValueTree tempoSequenceState; // listener handle, see ValueTreeChildListModel
    TrackListModel* trackModel = nullptr;
    QTimer positionTimer;
    double lastPosition = 0.0;
    bool lastPlaying = false;
};
