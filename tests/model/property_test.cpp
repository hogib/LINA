// SPDX-License-Identifier: GPL-3.0-or-later
//
// Property-style tests over generated command sequences
// (docs/design/13-architecture.md, "The document model"). Seeds are fixed, so
// every run walks the same sequences.
#include "support/diff.hpp"
#include "support/generator.hpp"

#include "lina/model/document.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace lina::model::testing {

namespace {

struct Tally {
  std::size_t applied{0};
  std::size_t refused{0};
};

// A document together with what a consumer would know of it from change sets
// alone. After every edit the two must agree.
struct Watched {
  Document document;
  ParentMap picture{parentsOf(document.project())};

  void see(const ChangeSet &changes) {
    REQUIRE(follow(picture, changes));
    REQUIRE(picture == parentsOf(document.project()));
  }
};

// Applies `steps` generated commands, one transaction each, and checks after
// every one that the rules hold and that the change set is exactly the
// difference. Returns the project after each recorded step, starting with the
// project before the first.
std::vector<Project> walk(Watched &watched, std::uint64_t seed, int steps,
                          Tally &tally) {
  Document &document = watched.document;
  CommandGenerator generator{seed};
  std::vector<Project> states{document.project()};
  for (int step = 0; step < steps; ++step) {
    const Project before = document.project();
    const Command command = generator.next(before);
    const auto changes = document.perform("edit", command);
    if (!changes.has_value()) {
      ++tally.refused;
      REQUIRE(document.project() == before);
      // A failure names something that exists, or the project; only "unknown
      // object" names what the command asked for and the document lacks.
      const ObjectRef blamed = changes.error().object;
      REQUIRE((changes.error().fault == Fault::UnknownObject ||
               blamed == projectRef || watched.picture.contains(blamed)));
      continue;
    }
    ++tally.applied;
    REQUIRE(validate(document.project()).empty());
    REQUIRE(*changes == diff(before, document.project()));
    watched.see(*changes);
    if (changes->empty()) {
      REQUIRE(document.project() == before);
    } else {
      states.push_back(document.project());
    }
  }
  return states;
}

void checkUndoEverythingThenRedoEverything(std::uint64_t seed) {
  constexpr int steps = 1'500;
  Watched watched;
  Document &document = watched.document;
  Tally tally;
  const std::vector<Project> states = walk(watched, seed, steps, tally);
  // The generator must exercise both outcomes, or the test proves little. Each
  // bound is less than half of what the seeds give, so that a change to the
  // generator does not fail the test by accident.
  INFO("applied " << tally.applied << ", refused " << tally.refused
                  << ", recorded " << states.size());
  REQUIRE(tally.applied > steps / 4);
  REQUIRE(tally.refused > steps / 10);
  REQUIRE(states.size() > steps / 5);
  REQUIRE(document.project().tracks.size() > 1);
  const Project end = document.project();

  for (std::size_t remaining = states.size() - 1; remaining > 0; --remaining) {
    const Project before = document.project();
    const auto changes = document.undo();
    REQUIRE(changes.has_value());
    REQUIRE(sameContent(document.project(), states.at(remaining - 1)));
    REQUIRE(validate(document.project()).empty());
    REQUIRE(*changes == diff(before, document.project()));
    watched.see(*changes);
    // Undo never hands an identity back.
    REQUIRE(document.project().nextId == end.nextId);
  }
  REQUIRE(!document.canUndo());
  REQUIRE(sameContent(document.project(), states.front()));

  for (std::size_t next = 1; next < states.size(); ++next) {
    const Project before = document.project();
    const auto changes = document.redo();
    REQUIRE(changes.has_value());
    REQUIRE(sameContent(document.project(), states.at(next)));
    REQUIRE(*changes == diff(before, document.project()));
    watched.see(*changes);
  }
  REQUIRE(!document.canRedo());
  REQUIRE(document.project() == end);
}

// One transaction of several generated commands. Returns true when it left an
// undo step.
bool runTransaction(Watched &watched, CommandGenerator &generator) {
  Document &document = watched.document;
  const Project before = document.project();
  const bool couldUndo = document.canUndo();
  REQUIRE(document.begin("gesture").has_value());
  ChangeSet merged;
  const std::uint64_t count = 1 + generator.below(12);
  for (std::uint64_t i = 0; i < count; ++i) {
    const Project beforeCommand = document.project();
    const auto changes = document.apply(generator.next(beforeCommand));
    if (!changes.has_value()) {
      REQUIRE(document.project() == beforeCommand);
      continue;
    }
    REQUIRE(*changes == diff(beforeCommand, document.project()));
    merged.merge(*changes);
  }
  REQUIRE(document.commit().has_value());
  REQUIRE(validate(document.project()).empty());
  REQUIRE(covers(merged, diff(before, document.project())));
  watched.see(merged);
  if (merged.empty()) {
    // Nothing to take back: no step was recorded.
    REQUIRE(sameContent(document.project(), before));
    REQUIRE(document.canUndo() == couldUndo);
  }
  return !merged.empty();
}

void runRolledBackTransaction(Watched &watched, CommandGenerator &generator) {
  Document &document = watched.document;
  const Project before = document.project();
  const bool couldRedo = document.canRedo();
  REQUIRE(document.begin("abandoned").has_value());
  ChangeSet merged;
  for (int i = 0; i < 6; ++i) {
    const auto changes = document.apply(generator.next(document.project()));
    if (changes.has_value()) {
      merged.merge(*changes);
    }
  }
  const Project middle = document.project();
  const auto undone = document.rollback();
  REQUIRE(undone.has_value());
  REQUIRE(sameContent(document.project(), before));
  REQUIRE(covers(*undone, diff(middle, document.project())));
  // Forwards and back again, a consumer ends where it started.
  merged.merge(*undone);
  watched.see(merged);
  REQUIRE(document.canRedo() == couldRedo);
  REQUIRE(!document.inTransaction());
}

void checkInterleavedHistory(std::uint64_t seed) {
  constexpr int rounds = 600;
  Watched watched;
  Document &document = watched.document;
  CommandGenerator generator{seed};
  // states[cursor] is where the document is; later entries are what redo leads
  // to.
  std::vector<Project> states{document.project()};
  std::size_t cursor = 0;
  std::size_t undone = 0;
  std::size_t redone = 0;
  std::size_t recorded = 0;

  for (int round = 0; round < rounds; ++round) {
    const std::uint64_t choice = generator.below(10);
    const Project before = document.project();
    if (choice < 5) {
      if (runTransaction(watched, generator)) {
        states.resize(cursor + 1);
        states.push_back(document.project());
        ++cursor;
        ++recorded;
      }
    } else if (choice < 7 && document.canUndo()) {
      const auto changes = document.undo();
      REQUIRE(changes.has_value());
      --cursor;
      ++undone;
      REQUIRE(covers(*changes, diff(before, document.project())));
      watched.see(*changes);
    } else if (choice < 9 && document.canRedo()) {
      const auto changes = document.redo();
      REQUIRE(changes.has_value());
      ++cursor;
      ++redone;
      REQUIRE(covers(*changes, diff(before, document.project())));
      watched.see(*changes);
    } else {
      runRolledBackTransaction(watched, generator);
    }
    REQUIRE(sameContent(document.project(), states.at(cursor)));
    REQUIRE(validate(document.project()).empty());
    REQUIRE(document.canUndo() == (cursor > 0));
    REQUIRE(document.canRedo() == (cursor + 1 < states.size()));
  }
  // Every kind of round must have happened often; the bounds are less than
  // half of what the seeds give.
  INFO("recorded " << recorded << ", undone " << undone << ", redone "
                   << redone);
  REQUIRE(recorded > rounds / 8);
  REQUIRE(undone > rounds / 20);
  REQUIRE(redone > rounds / 40);
}

} // namespace

TEST_CASE("generated edits keep the rules, and undo and redo walk back and "
          "forth exactly",
          "[model][property]") {
  checkUndoEverythingThenRedoEverything(1);
  checkUndoEverythingThenRedoEverything(20'261'118);
}

TEST_CASE("generated transactions, undo, redo and rollback interleave "
          "consistently",
          "[model][property]") {
  checkInterleavedHistory(7);
  checkInterleavedHistory(15'120);
}

TEST_CASE("the command generator is deterministic", "[model][property]") {
  Watched first;
  Watched second;
  Tally firstTally;
  Tally secondTally;
  const std::vector<Project> a = walk(first, 99, 200, firstTally);
  const std::vector<Project> b = walk(second, 99, 200, secondTally);
  REQUIRE(a == b);
  REQUIRE(firstTally.applied == secondTally.applied);

  // The same figures with every compiler and standard library. They change
  // only when the generator or the model's rules change; update them then.
  INFO("applied " << firstTally.applied << ", refused " << firstTally.refused
                  << ", states " << a.size() << ", next identity "
                  << first.document.project().nextId);
  REQUIRE(firstTally.applied == 131);
  REQUIRE(firstTally.refused == 69);
  REQUIRE(a.size() == 117);
  REQUIRE(first.document.project().nextId == 79);
}

} // namespace lina::model::testing
