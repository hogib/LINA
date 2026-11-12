// SPDX-License-Identifier: GPL-3.0-or-later
// The one QObject that owns the document. Views read `project()` and call
// `submit()`; QML chrome reads the few values it needs through invokables
// guarded by `revision`.
#pragma once

#include "model.hpp"

#include <QColor>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

class AppController : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Created in C++")
  Q_PROPERTY(int revision READ revision NOTIFY documentChanged)
  Q_PROPERTY(int trackCount READ trackCount NOTIFY documentChanged)
  Q_PROPERTY(int clipCount READ clipCount NOTIFY documentChanged)
  Q_PROPERTY(int noteCount READ noteCount NOTIFY documentChanged)
  Q_PROPERTY(int applied READ applied NOTIFY documentChanged)
  Q_PROPERTY(QString lastCommand READ lastCommand NOTIFY documentChanged)
  Q_PROPERTY(bool playing READ playing NOTIFY documentChanged)
  Q_PROPERTY(double playheadTick READ playheadTick NOTIFY playheadChanged)

public:
  explicit AppController(std::uint64_t seed, QObject *parent = nullptr);

  const lina::Project &project() const { return project_; }
  /// The single entry point for document changes coming from views and chrome.
  void submit(const lina::Command &cmd);
  /// Bench support: back to the freshly generated document.
  void reset();
  /// Advances the fake transport clock; emits playheadChanged only.
  void advance(double dtSeconds);
  /// Starts a 60 Hz timer driving `advance` (windowed app only; the bench steps
  /// by hand).
  void startClock();

  int revision() const { return revision_; }
  int trackCount() const { return static_cast<int>(project_.tracks.size()); }
  int clipCount() const { return static_cast<int>(project_.clipCount()); }
  int noteCount() const { return static_cast<int>(project_.midi.notes.size()); }
  int applied() const { return applied_; }
  QString lastCommand() const { return lastCommand_; }
  bool playing() const { return project_.transport.playing; }
  double playheadTick() const { return project_.transport.pos; }

  Q_INVOKABLE QString trackName(int track) const;
  Q_INVOKABLE QColor trackColor(int track) const;
  Q_INVOKABLE bool trackMuted(int track) const;
  Q_INVOKABLE bool trackSoloed(int track) const;
  Q_INVOKABLE void toggleMute(int track);
  Q_INVOKABLE void toggleSolo(int track);
  Q_INVOKABLE void togglePlay();

signals:
  void documentChanged();
  void playheadChanged();

private:
  lina::Project project_;
  lina::Project pristine_;
  int revision_ = 0;
  int applied_ = 0;
  QString lastCommand_;
  QTimer clock_;
};
