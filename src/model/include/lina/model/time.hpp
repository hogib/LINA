// SPDX-License-Identifier: GPL-3.0-or-later
//
// Exact musical time (docs/design/04-transport-and-musical-time.md). Every
// stored position and length is a whole number of ticks; a quarter note is
// always 15,120 ticks, whatever the time signature. Nothing here is a
// floating-point position.
#ifndef LINA_MODEL_TIME_HPP
#define LINA_MODEL_TIME_HPP

#include <compare>
#include <cstdint>
#include <optional>

namespace lina::model {

// Ticks in a quarter note. Divides evenly by 16, 20, 24 and 28: straight,
// quintuplet, triplet and septuplet subdivisions down to 1/64 notes.
inline constexpr std::int64_t ticksPerQuarter = 15'120;
inline constexpr std::int64_t ticksPerWholeNote = 4 * ticksPerQuarter;

// Largest tick count a document may store: for a position, for a length, and
// for the end of anything that has both (position plus length). Every stored
// position, length and end is therefore exactly representable as a double when
// the engine converts it on the way out, and no sum of two stored values
// overflows.
inline constexpr std::int64_t maxTicks = 9'007'199'254'740'992; // 2^53

// A length in musical time.
struct TickDuration {
  std::int64_t ticks{0};

  friend constexpr auto operator<=>(TickDuration, TickDuration) = default;
};

// A point in musical time: ticks from the start of the arrangement, or from the
// start of a clip's content.
struct TickPosition {
  std::int64_t ticks{0};

  friend constexpr auto operator<=>(TickPosition, TickPosition) = default;
};

constexpr TickDuration operator+(TickDuration a, TickDuration b) {
  return {a.ticks + b.ticks};
}
constexpr TickDuration operator-(TickDuration a, TickDuration b) {
  return {a.ticks - b.ticks};
}
constexpr TickDuration operator*(TickDuration a, std::int64_t factor) {
  return {a.ticks * factor};
}
constexpr TickPosition operator+(TickPosition a, TickDuration b) {
  return {a.ticks + b.ticks};
}
constexpr TickPosition operator-(TickPosition a, TickDuration b) {
  return {a.ticks - b.ticks};
}
constexpr TickDuration operator-(TickPosition a, TickPosition b) {
  return {a.ticks - b.ticks};
}

// True when a position or length is one a document may store.
constexpr bool isStorable(TickPosition position) {
  return position.ticks >= 0 && position.ticks <= maxTicks;
}
constexpr bool isStorable(TickDuration length) {
  return length.ticks >= 1 && length.ticks <= maxTicks;
}
// True when a range may be stored: its start, its length and its end.
constexpr bool isStorable(TickPosition start, TickDuration length) {
  return isStorable(start) && isStorable(length) &&
         start.ticks + length.ticks <= maxTicks;
}

// The length of one of `parts` equal parts of a whole note: 4 is a quarter
// note, 12 a quarter-note triplet, 80 a quintuplet 1/64. Empty when the part is
// not a whole number of ticks.
constexpr std::optional<TickDuration> wholeNoteFraction(std::int64_t parts) {
  if (parts < 1 || ticksPerWholeNote % parts != 0) {
    return std::nullopt;
  }
  return TickDuration{ticksPerWholeNote / parts};
}

// Project tempo: 20 to 300 beats per minute in steps of 0.01, stored as
// hundredths so that it is exact. A beat is a quarter note.
class Tempo {
public:
  static constexpr std::int32_t minHundredths = 2'000;
  static constexpr std::int32_t maxHundredths = 30'000;

  // 120 beats per minute.
  constexpr Tempo() = default;

  // Empty when the value is outside the range.
  static constexpr std::optional<Tempo>
  fromHundredths(std::int32_t hundredths) {
    if (hundredths < minHundredths || hundredths > maxHundredths) {
      return std::nullopt;
    }
    return Tempo{hundredths};
  }

  [[nodiscard]] constexpr std::int32_t hundredths() const {
    return hundredths_;
  }
  [[nodiscard]] constexpr double beatsPerMinute() const {
    return static_cast<double>(hundredths_) / 100.0;
  }

  friend constexpr bool operator==(Tempo, Tempo) = default;

private:
  constexpr explicit Tempo(std::int32_t hundredths) : hundredths_(hundredths) {}

  std::int32_t hundredths_{12'000};
};

// Time signature: numerator 1 to 99 over a denominator of 1, 2, 4, 8 or 16.
class TimeSignature {
public:
  // 4/4.
  constexpr TimeSignature() = default;

  // Empty when either number is not allowed.
  static constexpr std::optional<TimeSignature> make(std::int32_t numerator,
                                                     std::int32_t denominator) {
    const bool denominatorAllowed = denominator == 1 || denominator == 2 ||
                                    denominator == 4 || denominator == 8 ||
                                    denominator == 16;
    if (numerator < 1 || numerator > 99 || !denominatorAllowed) {
      return std::nullopt;
    }
    return TimeSignature{numerator, denominator};
  }

  [[nodiscard]] constexpr std::int32_t numerator() const { return numerator_; }
  [[nodiscard]] constexpr std::int32_t denominator() const {
    return denominator_;
  }
  // The length of one beat as the signature counts it (an eighth note in 6/8).
  [[nodiscard]] constexpr TickDuration beatLength() const {
    return {ticksPerWholeNote / denominator_};
  }
  [[nodiscard]] constexpr TickDuration barLength() const {
    return beatLength() * numerator_;
  }

  friend constexpr bool operator==(TimeSignature, TimeSignature) = default;

private:
  constexpr TimeSignature(std::int32_t numerator, std::int32_t denominator)
      : numerator_(numerator), denominator_(denominator) {}

  std::int32_t numerator_{4};
  std::int32_t denominator_{4};
};

// The musical interval whose boundaries a launch waits for.
enum class LaunchQuantisation : std::uint8_t {
  None,
  Bars8,
  Bars4,
  Bars2,
  Bar1,
  Half,
  Quarter,
  Eighth,
  Sixteenth,
  ThirtySecond,
};

// The length of a launch quantisation interval under a time signature. Empty
// for `None`.
std::optional<TickDuration> launchInterval(LaunchQuantisation quantisation,
                                           TimeSignature signature);

// Grid lines are the whole multiples of `grid`, counted from position zero.
// Domain: a storable position and a storable grid length. Outside it, and when
// the line asked for would lie past `maxTicks`, the result is empty; no input
// overflows. `roundToGrid` picks the nearest line and the later one on a tie.
std::optional<TickPosition> floorToGrid(TickPosition position,
                                        TickDuration grid);
std::optional<TickPosition> ceilToGrid(TickPosition position,
                                       TickDuration grid);
std::optional<TickPosition> roundToGrid(TickPosition position,
                                        TickDuration grid);

// A position as shown to the user: bar and beat count from 1, `tick` is the
// remainder inside the beat.
struct BarBeat {
  std::int64_t bar{1};
  std::int64_t beat{1};
  std::int64_t tick{0};

  friend constexpr bool operator==(const BarBeat &, const BarBeat &) = default;
};

// Bars and beats of a position under a single time signature that holds from
// position zero. Empty for a position that is not storable.
std::optional<BarBeat> toBarBeat(TickPosition position,
                                 TimeSignature signature);

} // namespace lina::model

#endif // LINA_MODEL_TIME_HPP
