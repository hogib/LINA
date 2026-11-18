// SPDX-License-Identifier: GPL-3.0-or-later
#include "lina/model/change_set.hpp"

#include <algorithm>

namespace lina::model {

void ChangeSet::added(ObjectRef object, ObjectRef parent) {
  fold({
      .object = object,
      .kind = ChangeKind::Added,
      .parent = parent,
      .previousParent = parent,
      .properties = {},
  });
}

void ChangeSet::removed(ObjectRef object, ObjectRef parent) {
  fold({
      .object = object,
      .kind = ChangeKind::Removed,
      .parent = parent,
      .previousParent = parent,
      .properties = {},
  });
}

void ChangeSet::changed(ObjectRef object, ObjectRef parent,
                        PropertySet properties) {
  moved(object, parent, parent, properties);
}

void ChangeSet::moved(ObjectRef object, ObjectRef previousParent,
                      ObjectRef parent, PropertySet properties) {
  fold({
      .object = object,
      .kind = ChangeKind::Changed,
      .parent = parent,
      .previousParent = previousParent,
      .properties = properties,
  });
}

void ChangeSet::merge(const ChangeSet &later) {
  for (const Change &change : later.changes_) {
    fold(change);
  }
}

const Change *ChangeSet::find(ObjectRef object) const {
  const auto at =
      std::ranges::lower_bound(changes_, object, {}, &Change::object);
  return (at != changes_.end() && at->object == object) ? &*at : nullptr;
}

void ChangeSet::fold(const Change &later) {
  const auto at =
      std::ranges::lower_bound(changes_, later.object, {}, &Change::object);
  if (at == changes_.end() || at->object != later.object) {
    changes_.insert(at, later);
    return;
  }

  Change &earlier = *at;
  switch (earlier.kind) {
  case ChangeKind::Added:
    if (later.kind == ChangeKind::Removed) {
      // Created and destroyed inside one change set: it never existed for the
      // reader.
      changes_.erase(at);
    } else {
      earlier.parent = later.parent;
      earlier.previousParent = later.parent;
    }
    return;
  case ChangeKind::Removed:
    // Taken out and put back: the same object, with anything about it possibly
    // different.
    earlier.kind = ChangeKind::Changed;
    earlier.parent = later.parent;
    earlier.properties = PropertySet::all();
    return;
  case ChangeKind::Changed:
    if (later.kind == ChangeKind::Removed) {
      earlier.kind = ChangeKind::Removed;
      earlier.parent = earlier.previousParent;
      earlier.properties = {};
    } else {
      earlier.parent = later.parent;
      earlier.properties |= later.properties;
    }
    return;
  }
}

std::optional<std::uint64_t>
ChangeSet::firstAddedOfKind(ObjectKind kind) const {
  const auto found = std::ranges::find_if(changes_, [kind](const Change &c) {
    return c.kind == ChangeKind::Added && c.object.kind == kind;
  });
  return found == changes_.end() ? std::nullopt
                                 : std::optional{found->object.id};
}

} // namespace lina::model
