// SPDX-License-Identifier: GPL-3.0-or-later
//
// Test support: a seeded, deterministic source of commands for property-style
// tests. The same seed and the same sequence of projects always give the same
// commands, with every compiler: only the raw output of the Mersenne Twister is
// used, never a standard distribution, and no two draws share an expression
// whose evaluation order the language leaves open.
#ifndef LINA_MODEL_TESTS_SUPPORT_GENERATOR_HPP
#define LINA_MODEL_TESTS_SUPPORT_GENERATOR_HPP

#include "lina/model/command.hpp"
#include "lina/model/project.hpp"

#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

namespace lina::model::testing {

class CommandGenerator {
public:
  explicit CommandGenerator(std::uint64_t seed) : engine_(seed) {}

  // A command aimed at the project as it is now. About two in three apply;
  // the rest are meant not to (unknown identities, identities where the
  // document must assign them, duplicates, overlaps, wrong kinds of track or
  // clip, values out of range, past the last storable tick, not numbers, not
  // enumerators), so that failure paths are walked too.
  Command next(const Project &project);

  // A number in [0, bound). `bound` must be at least 1.
  std::uint64_t below(std::uint64_t bound) { return engine_() % bound; }
  // True once in `oneIn` calls on average.
  bool rarely(std::uint64_t oneIn) { return below(oneIn) == 0; }

private:
  Command projectCommand();
  Command mediaCommand(const Project &project);
  Command sceneCommand(const Project &project);
  Command trackCommand(const Project &project);
  Command mixerCommand(const Project &project);
  Command clipCommand(const Project &project);
  Command clipPropertyCommand(const Project &project);
  Command noteCommand(const Project &project);
  Command controllerCommand(const Project &project);
  Command deviceCommand(const Project &project);
  Command automationCommand(const Project &project);

  // Values. Each is usually fine and now and then not: negative, past the
  // limit, not a number, or an enumerator that does not exist.
  TickPosition position();
  TickDuration length();
  double level(double low, double high);
  std::uint32_t colour();
  // An index below `bound`, now and then pushed `overshoot` further so that it
  // is out of range.
  std::size_t index(std::size_t bound, std::size_t overshoot);
  template <class Enumeration> Enumeration enumerator(std::uint64_t count) {
    const std::uint64_t picked = rarely(40) ? count + below(200) : below(count);
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): on purpose
    return static_cast<Enumeration>(picked);
  }

  // Things to aim at. Each is usually something in the project and now and
  // then an identity the project never handed out.
  TrackId anyTrack(const Project &project);
  ClipId anyClip(const Project &project);

  Clip clip(const Project &project, TrackKind kind);
  std::vector<Note> notes(std::size_t count);
  ClipContainer container(const Project &project);
  TrackInput input();
  std::vector<AutomationPoint> points(double low, double high);

  std::mt19937_64 engine_;
  std::uint64_t named_{0};
};

} // namespace lina::model::testing

#endif // LINA_MODEL_TESTS_SUPPORT_GENERATOR_HPP
