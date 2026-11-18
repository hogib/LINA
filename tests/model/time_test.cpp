// SPDX-License-Identifier: GPL-3.0-or-later
#include "lina/model/time.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <limits>

namespace lina::model {

TEST_CASE("a whole note divides exactly into every note value on offer",
          "[model][time]") {
  REQUIRE(ticksPerQuarter == 15'120);
  REQUIRE(ticksPerWholeNote == 60'480);

  // Straight, triplet, quintuplet and septuplet values down to 1/64 notes.
  constexpr std::array<std::int64_t, 28> parts{
      1,  2,  4,  8,  16, 32, 64, 3,  6,   12, 24, 48, 96, 5,
      10, 20, 40, 80, 7,  14, 28, 56, 112, 9,  18, 36, 15, 30,
  };
  for (const std::int64_t count : parts) {
    const TickDuration part = wholeNoteFraction(count).value_or(TickDuration{});
    REQUIRE(part.ticks * count == ticksPerWholeNote);
  }
  REQUIRE(wholeNoteFraction(4) == TickDuration{ticksPerQuarter});
  REQUIRE(!wholeNoteFraction(11).has_value());
  REQUIRE(!wholeNoteFraction(0).has_value());
  REQUIRE(!wholeNoteFraction(-4).has_value());
}

TEST_CASE("positions and lengths combine without mixing up", "[model][time]") {
  const TickPosition position{100};
  const TickDuration length{30};
  REQUIRE(position + length == TickPosition{130});
  REQUIRE(position - length == TickPosition{70});
  REQUIRE(TickPosition{130} - position == length);
  REQUIRE(length + length == TickDuration{60});
  REQUIRE(length - TickDuration{10} == TickDuration{20});
  REQUIRE(length * 3 == TickDuration{90});
  REQUIRE(TickPosition{1} < TickPosition{2});
  REQUIRE(TickDuration{2} > TickDuration{1});
}

TEST_CASE("only non-negative positions and positive lengths up to the limit "
          "are storable",
          "[model][time]") {
  REQUIRE(isStorable(TickPosition{0}));
  REQUIRE(isStorable(TickPosition{maxTicks}));
  REQUIRE(!isStorable(TickPosition{-1}));
  REQUIRE(!isStorable(TickPosition{maxTicks + 1}));
  REQUIRE(isStorable(TickDuration{1}));
  REQUIRE(isStorable(TickDuration{maxTicks}));
  REQUIRE(!isStorable(TickDuration{0}));
  REQUIRE(!isStorable(TickDuration{maxTicks + 1}));
}

TEST_CASE("tempo is 20 to 300 beats per minute in hundredths",
          "[model][time]") {
  REQUIRE(Tempo{}.hundredths() == 12'000);
  REQUIRE(Tempo{}.beatsPerMinute() == 120.0);
  REQUIRE(Tempo::fromHundredths(2'000).has_value());
  REQUIRE(Tempo::fromHundredths(30'000).has_value());
  REQUIRE(!Tempo::fromHundredths(1'999).has_value());
  REQUIRE(!Tempo::fromHundredths(30'001).has_value());
  const Tempo tempo = Tempo::fromHundredths(12'345).value_or(Tempo{});
  REQUIRE(tempo.hundredths() == 12'345);
  REQUIRE(tempo.beatsPerMinute() == 123.45);
  REQUIRE(tempo != Tempo{});
  REQUIRE(Tempo::fromHundredths(12'000) == Tempo{});
}

TEST_CASE("time signatures allow numerators 1 to 99 over 1, 2, 4, 8 or 16",
          "[model][time]") {
  REQUIRE(TimeSignature{}.numerator() == 4);
  REQUIRE(TimeSignature{}.denominator() == 4);
  REQUIRE(TimeSignature{}.barLength() == TickDuration{ticksPerWholeNote});
  for (const std::int32_t denominator : {1, 2, 4, 8, 16}) {
    REQUIRE(TimeSignature::make(1, denominator).has_value());
    REQUIRE(TimeSignature::make(99, denominator).has_value());
  }
  REQUIRE(!TimeSignature::make(0, 4).has_value());
  REQUIRE(!TimeSignature::make(100, 4).has_value());
  REQUIRE(!TimeSignature::make(4, 3).has_value());
  REQUIRE(!TimeSignature::make(4, 32).has_value());
  REQUIRE(!TimeSignature::make(4, 0).has_value());

  const TimeSignature sevenEight =
      TimeSignature::make(7, 8).value_or(TimeSignature{});
  REQUIRE(sevenEight.numerator() == 7);
  REQUIRE(sevenEight.denominator() == 8);
  REQUIRE(sevenEight.beatLength() == TickDuration{ticksPerQuarter / 2});
  REQUIRE(sevenEight.barLength() == TickDuration{7 * ticksPerQuarter / 2});
  REQUIRE(sevenEight != TimeSignature{});
}

TEST_CASE("launch quantisation intervals follow the time signature for bar "
          "values only",
          "[model][time]") {
  const TimeSignature common;
  const TimeSignature waltz = TimeSignature::make(3, 4).value_or(common);
  REQUIRE(waltz.numerator() == 3);
  const TickDuration quarter{ticksPerQuarter};

  REQUIRE(!launchInterval(LaunchQuantisation::None, common).has_value());
  REQUIRE(launchInterval(LaunchQuantisation::Bars8, common) == quarter * 32);
  REQUIRE(launchInterval(LaunchQuantisation::Bars4, common) == quarter * 16);
  REQUIRE(launchInterval(LaunchQuantisation::Bars2, common) == quarter * 8);
  REQUIRE(launchInterval(LaunchQuantisation::Bar1, common) == quarter * 4);
  REQUIRE(launchInterval(LaunchQuantisation::Bar1, waltz) == quarter * 3);
  REQUIRE(launchInterval(LaunchQuantisation::Bars8, waltz) == quarter * 24);
  REQUIRE(launchInterval(LaunchQuantisation::Half, waltz) == quarter * 2);
  REQUIRE(launchInterval(LaunchQuantisation::Quarter, waltz) == quarter);
  REQUIRE(launchInterval(LaunchQuantisation::Eighth, common) ==
          TickDuration{ticksPerQuarter / 2});
  REQUIRE(launchInterval(LaunchQuantisation::Sixteenth, common) ==
          TickDuration{ticksPerQuarter / 4});
  REQUIRE(launchInterval(LaunchQuantisation::ThirtySecond, common) ==
          TickDuration{ticksPerQuarter / 8});
  // A value outside the enumeration has no interval.
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): on purpose
  const auto outside = static_cast<LaunchQuantisation>(200);
  REQUIRE(!launchInterval(outside, common).has_value());
}

TEST_CASE("grid rounding is exact inside the storable range", "[model][time]") {
  const TickDuration grid{100};
  REQUIRE(floorToGrid({250}, grid) == TickPosition{200});
  REQUIRE(floorToGrid({200}, grid) == TickPosition{200});
  REQUIRE(floorToGrid({0}, grid) == TickPosition{0});
  REQUIRE(floorToGrid({99}, grid) == TickPosition{0});

  REQUIRE(ceilToGrid({201}, grid) == TickPosition{300});
  REQUIRE(ceilToGrid({200}, grid) == TickPosition{200});
  REQUIRE(ceilToGrid({0}, grid) == TickPosition{0});

  REQUIRE(roundToGrid({249}, grid) == TickPosition{200});
  REQUIRE(roundToGrid({250}, grid) == TickPosition{300});
  REQUIRE(roundToGrid({251}, grid) == TickPosition{300});
  REQUIRE(roundToGrid({49}, grid) == TickPosition{0});

  // A grid of one tick: every position is on a line.
  REQUIRE(floorToGrid({37}, TickDuration{1}) == TickPosition{37});
  REQUIRE(ceilToGrid({37}, TickDuration{1}) == TickPosition{37});
  REQUIRE(roundToGrid({37}, TickDuration{1}) == TickPosition{37});
}

TEST_CASE("grid functions have no answer outside their domain and never "
          "overflow",
          "[model][time]") {
  constexpr std::int64_t least = std::numeric_limits<std::int64_t>::min();
  constexpr std::int64_t most = std::numeric_limits<std::int64_t>::max();
  const TickDuration grid{1'000};

  // Positions that cannot be stored. Run under UBSan, these are the inputs
  // that used to overflow.
  for (const std::int64_t ticks :
       {least, least + 1, std::int64_t{-1}, maxTicks + 1, most - 5, most}) {
    REQUIRE(!floorToGrid({ticks}, grid).has_value());
    REQUIRE(!ceilToGrid({ticks}, grid).has_value());
    REQUIRE(!roundToGrid({ticks}, grid).has_value());
  }
  // Grids that are not lengths.
  for (const std::int64_t ticks :
       {least, std::int64_t{-5}, std::int64_t{0}, maxTicks + 1, most}) {
    REQUIRE(!floorToGrid({500}, {ticks}).has_value());
    REQUIRE(!ceilToGrid({500}, {ticks}).has_value());
    REQUIRE(!roundToGrid({500}, {ticks}).has_value());
  }

  // The largest grid: its only lines are zero and the last storable position.
  const TickDuration widest{maxTicks};
  REQUIRE(floorToGrid({maxTicks - 1}, widest) == TickPosition{0});
  REQUIRE(ceilToGrid({1}, widest) == TickPosition{maxTicks});
  REQUIRE(roundToGrid({maxTicks / 2}, widest) == TickPosition{maxTicks});
  REQUIRE(roundToGrid({(maxTicks / 2) - 1}, widest) == TickPosition{0});
  REQUIRE(floorToGrid({maxTicks}, widest) == TickPosition{maxTicks});
  REQUIRE(ceilToGrid({maxTicks}, widest) == TickPosition{maxTicks});

  // A line past the last storable position is no answer.
  const TickPosition nearEnd{maxTicks - 1};
  REQUIRE(floorToGrid(nearEnd, grid) == TickPosition{maxTicks - 992});
  REQUIRE(!ceilToGrid(nearEnd, grid).has_value());
  REQUIRE(!roundToGrid(nearEnd, grid).has_value());
  REQUIRE(roundToGrid({maxTicks - 900}, grid) == TickPosition{maxTicks - 992});
}

TEST_CASE("ranges are storable only when their end is", "[model][time]") {
  REQUIRE(isStorable(TickPosition{0}, TickDuration{maxTicks}));
  REQUIRE(isStorable(TickPosition{maxTicks - 1}, TickDuration{1}));
  REQUIRE(!isStorable(TickPosition{maxTicks}, TickDuration{1}));
  REQUIRE(!isStorable(TickPosition{1}, TickDuration{maxTicks}));
  REQUIRE(!isStorable(TickPosition{maxTicks}, TickDuration{maxTicks}));
  REQUIRE(!isStorable(TickPosition{-1}, TickDuration{10}));
  REQUIRE(!isStorable(TickPosition{0}, TickDuration{0}));
  // Each part is checked before the sum, so nothing overflows.
  REQUIRE(!isStorable(TickPosition{std::numeric_limits<std::int64_t>::max()},
                      TickDuration{std::numeric_limits<std::int64_t>::max()}));
}

TEST_CASE("bars and beats are derived from the time signature",
          "[model][time]") {
  const TimeSignature common;
  REQUIRE(toBarBeat({0}, common) == BarBeat{.bar = 1, .beat = 1, .tick = 0});
  REQUIRE(toBarBeat({(5 * ticksPerQuarter) + 10}, common) ==
          BarBeat{.bar = 2, .beat = 2, .tick = 10});
  REQUIRE(toBarBeat({(4 * ticksPerQuarter) - 1}, common) ==
          BarBeat{.bar = 1, .beat = 4, .tick = ticksPerQuarter - 1});

  const TimeSignature sixEight = TimeSignature::make(6, 8).value_or(common);
  REQUIRE(sixEight.denominator() == 8);
  // One bar of 6/8 is three quarter notes; the fifth eighth of the second bar.
  REQUIRE(toBarBeat({5 * ticksPerQuarter}, sixEight) ==
          BarBeat{.bar = 2, .beat = 5, .tick = 0});

  // Positions that cannot be stored have no bar: there is nothing before the
  // start of the arrangement.
  REQUIRE(!toBarBeat({-1}, common).has_value());
  REQUIRE(!toBarBeat({maxTicks + 1}, common).has_value());
  REQUIRE(toBarBeat({maxTicks}, common).has_value());
}

} // namespace lina::model
