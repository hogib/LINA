// SPDX-License-Identifier: GPL-3.0-or-later
#include "controller.hpp"

#include "sgutil.hpp"

AppController::AppController(std::uint64_t seed, QObject *parent)
    : QObject(parent), project_(lina::Project::synthetic(seed)),
      pristine_(project_) {}

void AppController::reset() {
  project_ = pristine_;
  ++revision_;
  applied_ = 0;
  lastCommand_.clear();
  emit documentChanged();
  emit playheadChanged();
}

void AppController::submit(const lina::Command &cmd) {
  const bool playheadOnly = std::holds_alternative<lina::SetPlayhead>(cmd);
  project_.apply(cmd);
  if (playheadOnly) {
    emit playheadChanged();
    return;
  }
  ++revision_;
  ++applied_;
  lastCommand_ = QString::fromStdString(lina::summary(cmd));
  emit documentChanged();
}

void AppController::advance(double dtSeconds) {
  if (!project_.transport.playing)
    return;
  project_.advanceTransport(dtSeconds);
  emit playheadChanged();
}

void AppController::startClock() {
  clock_.setInterval(16);
  clock_.setTimerType(Qt::PreciseTimer);
  connect(&clock_, &QTimer::timeout, this, [this] { advance(0.016); });
  clock_.start();
}

QString AppController::trackName(int track) const {
  if (track < 0 || track >= trackCount())
    return {};
  return QString::fromStdString(
      project_.tracks[static_cast<std::size_t>(track)].name);
}

QColor AppController::trackColor(int track) const {
  if (track < 0 || track >= trackCount())
    return {};
  const auto c =
      sg::hsv(project_.tracks[static_cast<std::size_t>(track)].hue, 0.5f, 0.7f);
  return QColor(c.r, c.g, c.b);
}

bool AppController::trackMuted(int track) const {
  return track >= 0 && track < trackCount() &&
         project_.tracks[static_cast<std::size_t>(track)].mute;
}

bool AppController::trackSoloed(int track) const {
  return track >= 0 && track < trackCount() &&
         project_.tracks[static_cast<std::size_t>(track)].solo;
}

void AppController::toggleMute(int track) {
  if (track >= 0 && track < trackCount())
    submit(lina::ToggleMute{static_cast<std::size_t>(track)});
}

void AppController::toggleSolo(int track) {
  if (track >= 0 && track < trackCount())
    submit(lina::ToggleSolo{static_cast<std::size_t>(track)});
}

void AppController::togglePlay() { submit(lina::TogglePlay{}); }
