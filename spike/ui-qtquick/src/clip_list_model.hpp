// SPDX-License-Identifier: GPL-3.0-or-later
// NAIVE VARIANT ONLY: a flat list model with one row per clip, feeding a QML
// Repeater that creates one Rectangle (+ Text) per clip. Exists purely to
// measure that approach.
#pragma once

#include "controller.hpp"

#include <QAbstractListModel>
#include <QColor>

#include <vector>

class ClipListModel : public QAbstractListModel {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(AppController *controller READ controller WRITE setController
                 NOTIFY controllerChanged)

public:
  enum Role {
    TrackRole = Qt::UserRole + 1,
    StartRole,
    LenRole,
    NameRole,
    ColorRole
  };

  using QAbstractListModel::QAbstractListModel;

  AppController *controller() const { return ctl_; }
  void setController(AppController *c);

  int rowCount(const QModelIndex &parent = {}) const override;
  QVariant data(const QModelIndex &index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

signals:
  void controllerChanged();

private:
  struct Row {
    int track;
    double start, len;
    QString name;
    QColor color;
  };
  void reload();

  AppController *ctl_ = nullptr;
  std::vector<Row> rows_;
};
