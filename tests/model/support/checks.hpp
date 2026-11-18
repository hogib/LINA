// SPDX-License-Identifier: GPL-3.0-or-later
//
// Test support: applying one command with every general property checked on the
// way, so that each test only has to state what is particular to it.
#ifndef LINA_MODEL_TESTS_SUPPORT_CHECKS_HPP
#define LINA_MODEL_TESTS_SUPPORT_CHECKS_HPP

#include "support/diff.hpp"

#include "lina/model/document.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <vector>

namespace lina::model::testing {

// Applies a command that must succeed, as one transaction. Checks that the
// rules still hold, that the change set is exactly the difference, and that
// undo and redo take the document back and forth. Returns the change set; the
// document is left with the command applied.
inline ChangeSet applied(Document &document, const Command &command) {
  const Project before = document.project();
  const auto changes = document.perform("test", command);
  REQUIRE(changes.has_value());
  REQUIRE(validate(document.project()).empty());
  REQUIRE(*changes == diff(before, document.project()));
  if (changes->empty()) {
    REQUIRE(document.project() == before);
    return *changes;
  }
  const Project after = document.project();
  const auto undone = document.undo();
  REQUIRE(undone.has_value());
  REQUIRE(sameContent(document.project(), before));
  REQUIRE(*undone == diff(after, document.project()));
  const auto redone = document.redo();
  REQUIRE(redone.has_value());
  REQUIRE(document.project() == after);
  REQUIRE(*redone == *changes);
  return *changes;
}

// Applies a command that must fail, checks that it changed nothing and left no
// undo step, and returns the fault.
inline Fault refused(Document &document, const Command &command) {
  const Project before = document.project();
  const bool couldUndo = document.canUndo();
  const auto changes = document.perform("test", command);
  REQUIRE(!changes.has_value());
  REQUIRE(document.project() == before);
  REQUIRE(document.canUndo() == couldUndo);
  REQUIRE(!document.inTransaction());
  return changes.error().fault;
}

// As `refused`, but returns the whole failure: the fault and the object it
// names.
inline Failure refusal(Document &document, const Command &command) {
  const Project before = document.project();
  const auto changes = document.perform("test", command);
  REQUIRE(!changes.has_value());
  REQUIRE(document.project() == before);
  return changes.error();
}

// True when the change set says exactly this about the object.
inline bool says(const ChangeSet &changes, ObjectRef object, ChangeKind kind,
                 PropertySet properties = {}) {
  const Change *change = changes.find(object);
  return change != nullptr && change->kind == kind &&
         change->properties == properties;
}

inline bool hasFault(const std::vector<Failure> &failures, Fault fault) {
  return std::ranges::any_of(failures, [fault](const Failure &failure) {
    return failure.fault == fault;
  });
}

} // namespace lina::model::testing

#endif // LINA_MODEL_TESTS_SUPPORT_CHECKS_HPP
