// SPDX-License-Identifier: GPL-3.0-or-later
//
// Test support: oracles for change sets. `diff` compares two projects object
// by object, without looking at what any command claims, and says what a
// correct change set must contain. A `ParentMap` is the other way round: a
// consumer's picture of the document, kept up to date from change sets alone.
#ifndef LINA_MODEL_TESTS_SUPPORT_DIFF_HPP
#define LINA_MODEL_TESTS_SUPPORT_DIFF_HPP

#include "lina/model/change_set.hpp"
#include "lina/model/project.hpp"

#include <map>

namespace lina::model::testing {

// The exact change set between two projects: every object added, removed or
// moved, and every property that differs.
ChangeSet diff(const Project &before, const Project &after);

// True when `claimed` is an acceptable account of `exact` after merging: the
// same Added and Removed entries, and for Changed entries at least the
// properties and the containers that `exact` has. `claimed` may name more
// Changed objects and properties than really differ, but an object it names
// without need must not be said to have moved.
bool covers(const ChangeSet &claimed, const ChangeSet &exact);

// Every object with an identity, and its container.
using ParentMap = std::map<ObjectRef, ObjectRef>;

ParentMap parentsOf(const Project &project);

// Updates a consumer's picture from a change set and nothing else. False when
// the change set contradicts the picture: it adds what is there, removes or
// changes what is not, or names a container the object was not in.
bool follow(ParentMap &picture, const ChangeSet &changes);

} // namespace lina::model::testing

#endif // LINA_MODEL_TESTS_SUPPORT_DIFF_HPP
