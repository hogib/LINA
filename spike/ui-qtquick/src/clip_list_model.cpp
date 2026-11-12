// SPDX-License-Identifier: GPL-3.0-or-later
#include "clip_list_model.hpp"

#include "sgutil.hpp"

void ClipListModel::setController(AppController *c) {
  if (c == ctl_)
    return;
  if (ctl_)
    ctl_->disconnect(this);
  ctl_ = c;
  // A whole-model reset on every document change: the crude but honest way to
  // keep a Qt item model in step with a plain-struct document that has no
  // change notifications.
  if (ctl_)
    connect(ctl_, &AppController::documentChanged, this,
            &ClipListModel::reload);
  reload();
  emit controllerChanged();
}

void ClipListModel::reload() {
  beginResetModel();
  rows_.clear();
  if (ctl_) {
    const lina::Project &p = ctl_->project();
    rows_.reserve(p.clipCount());
    for (std::size_t ti = 0; ti < p.tracks.size(); ++ti) {
      const lina::Track &tr = p.tracks[ti];
      const auto c = sg::hsv(tr.hue, tr.mute ? 0.08f : 0.5f, 0.5f);
      for (const lina::Clip &clip : tr.clips)
        rows_.push_back({static_cast<int>(ti), static_cast<double>(clip.start),
                         static_cast<double>(clip.len),
                         QString::fromStdString(clip.name),
                         QColor(c.r, c.g, c.b)});
    }
  }
  endResetModel();
}

int ClipListModel::rowCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

QVariant ClipListModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || index.row() >= static_cast<int>(rows_.size()))
    return {};
  const Row &r = rows_[static_cast<std::size_t>(index.row())];
  switch (role) {
  case TrackRole:
    return r.track;
  case StartRole:
    return r.start;
  case LenRole:
    return r.len;
  case NameRole:
    return r.name;
  case ColorRole:
    return r.color;
  default:
    return {};
  }
}

QHash<int, QByteArray> ClipListModel::roleNames() const {
  return {{TrackRole, "track"},
          {StartRole, "start"},
          {LenRole, "len"},
          {NameRole, "clipName"},
          {ColorRole, "clipColor"}};
}
