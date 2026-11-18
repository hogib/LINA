// SPDX-License-Identifier: GPL-3.0-or-later
#include "lina/model/time.hpp"

namespace lina::model {

namespace {

// A grid line, unless it lies past the last storable position.
std::optional<TickPosition> lineAt(std::int64_t ticks) {
  if (ticks > maxTicks) {
    return std::nullopt;
  }
  return TickPosition{ticks};
}

} // namespace

std::optional<TickDuration> launchInterval(LaunchQuantisation quantisation,
                                           TimeSignature signature) {
  const TickDuration bar = signature.barLength();
  switch (quantisation) {
  case LaunchQuantisation::None:
    return std::nullopt;
  case LaunchQuantisation::Bars8:
    return bar * 8;
  case LaunchQuantisation::Bars4:
    return bar * 4;
  case LaunchQuantisation::Bars2:
    return bar * 2;
  case LaunchQuantisation::Bar1:
    return bar;
  case LaunchQuantisation::Half:
    return wholeNoteFraction(2);
  case LaunchQuantisation::Quarter:
    return wholeNoteFraction(4);
  case LaunchQuantisation::Eighth:
    return wholeNoteFraction(8);
  case LaunchQuantisation::Sixteenth:
    return wholeNoteFraction(16);
  case LaunchQuantisation::ThirtySecond:
    return wholeNoteFraction(32);
  }
  return std::nullopt;
}

// In all three, both operands are at most 2^53, so neither the remainder
// doubled nor a line plus one grid step can overflow 64 bits.

std::optional<TickPosition> floorToGrid(TickPosition position,
                                        TickDuration grid) {
  if (!isStorable(position) || !isStorable(grid)) {
    return std::nullopt;
  }
  return TickPosition{position.ticks - (position.ticks % grid.ticks)};
}

std::optional<TickPosition> ceilToGrid(TickPosition position,
                                       TickDuration grid) {
  const std::optional<TickPosition> below = floorToGrid(position, grid);
  if (!below.has_value() || *below == position) {
    return below;
  }
  return lineAt(below->ticks + grid.ticks);
}

std::optional<TickPosition> roundToGrid(TickPosition position,
                                        TickDuration grid) {
  const std::optional<TickPosition> below = floorToGrid(position, grid);
  if (!below.has_value()) {
    return below;
  }
  const std::int64_t past = position.ticks - below->ticks;
  return (past * 2 >= grid.ticks) ? lineAt(below->ticks + grid.ticks) : below;
}

std::optional<BarBeat> toBarBeat(TickPosition position,
                                 TimeSignature signature) {
  if (!isStorable(position)) {
    return std::nullopt;
  }
  const std::int64_t bar = signature.barLength().ticks;
  const std::int64_t beat = signature.beatLength().ticks;
  const std::int64_t insideBar = position.ticks % bar;
  return BarBeat{
      .bar = (position.ticks / bar) + 1,
      .beat = (insideBar / beat) + 1,
      .tick = insideBar % beat,
  };
}

} // namespace lina::model
