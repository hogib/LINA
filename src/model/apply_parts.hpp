// SPDX-License-Identifier: GPL-3.0-or-later
//
// Private to lina_model: one `applyOne` per command type, one `restoreOne` per
// restore, and the helpers they share. Every `applyOne` checks all it needs to
// before it changes anything, so a failure leaves the project untouched. A
// `restoreOne` checks nothing: see detail/inverse.hpp.
#ifndef LINA_MODEL_SRC_APPLY_PARTS_HPP
#define LINA_MODEL_SRC_APPLY_PARTS_HPP

#include "apply.hpp"
#include "lookup.hpp"
#include "rules.hpp"

#include <algorithm>
#include <cstddef>
#include <expected>
#include <iterator>
#include <utility>
#include <vector>

namespace lina::model::detail {

using Result = std::expected<Applied, Failure>;

inline std::unexpected<Failure> fail(Fault fault, ObjectRef object) {
  return std::unexpected(Failure{.fault = fault, .object = object});
}

// The result of a command that asked for what is already the case.
inline Result unchanged() { return Applied{}; }

// The value as the document stores it: negative zero becomes zero, so that a
// document never holds two spellings of one number.
inline double canonical(double value) { return value == 0.0 ? 0.0 : value; }
inline Send canonical(Send send) {
  send.levelDb = canonical(send.levelDb);
  return send;
}
inline std::vector<AutomationPoint>
canonical(std::vector<AutomationPoint> points) {
  for (AutomationPoint &point : points) {
    point.value = canonical(point.value);
  }
  return points;
}
template <class Type> Type canonical(const Type &value) { return value; }

template <class Container>
auto iteratorAt(Container &container, std::size_t index) {
  return std::next(container.begin(), static_cast<std::ptrdiff_t>(index));
}

template <class Container, class Iterator>
std::size_t indexOf(const Container &container, Iterator position) {
  return static_cast<std::size_t>(position - container.begin());
}

// Moves the element at `from` to `to`, keeping the order of the others.
template <class Container>
void moveElement(Container &container, std::size_t from, std::size_t to) {
  const auto source = iteratorAt(container, from);
  const auto target = iteratorAt(container, to);
  if (from < to) {
    std::rotate(source, std::next(source), std::next(target));
  } else {
    std::rotate(target, source, std::next(source));
  }
}

// Records a track, slot or clip and everything inside it as added or removed.
inline void addedTrack(ChangeSet &changes, const Track &track) {
  auto visit = [&changes](ObjectRef object, ObjectRef parent) {
    changes.added(object, parent);
  };
  walkTrack(track, visit);
}
inline void removedTrack(ChangeSet &changes, const Track &track) {
  auto visit = [&changes](ObjectRef object, ObjectRef parent) {
    changes.removed(object, parent);
  };
  walkTrack(track, visit);
}
inline void addedSlot(ChangeSet &changes, const Slot &slot, ObjectRef parent) {
  auto visit = [&changes](ObjectRef object, ObjectRef container) {
    changes.added(object, container);
  };
  walkSlot(slot, parent, visit);
}
inline void removedSlot(ChangeSet &changes, const Slot &slot,
                        ObjectRef parent) {
  auto visit = [&changes](ObjectRef object, ObjectRef container) {
    changes.removed(object, container);
  };
  walkSlot(slot, parent, visit);
}
inline void addedClip(ChangeSet &changes, const Clip &clip, ObjectRef parent) {
  auto visit = [&changes](ObjectRef object, ObjectRef container) {
    changes.added(object, container);
  };
  walkClip(clip, parent, visit);
}
inline void removedClip(ChangeSet &changes, const Clip &clip,
                        ObjectRef parent) {
  auto visit = [&changes](ObjectRef object, ObjectRef container) {
    changes.removed(object, container);
  };
  walkClip(clip, parent, visit);
}

// Puts an envelope into a track's list, which is kept sorted by identity.
inline void insertEnvelope(Track &track, Envelope envelope) {
  const auto after = std::ranges::lower_bound(track.automation, envelope.id, {},
                                              &Envelope::id);
  track.automation.insert(after, std::move(envelope));
}

// Takes the envelopes that `matches` out of a track, in identity order.
template <class Predicate>
std::vector<Envelope> takeEnvelopes(Track &track, Predicate matches) {
  std::vector<Envelope> taken;
  for (Envelope &envelope : track.automation) {
    if (matches(envelope)) {
      taken.push_back(std::move(envelope));
    }
  }
  std::erase_if(track.automation, [&taken](const Envelope &envelope) {
    return std::ranges::find(taken, envelope.id, &Envelope::id) != taken.end();
  });
  return taken;
}

Result applyOne(Project &project, const SetProjectProperty &command);
Result applyOne(Project &project, const AddMedia &command);
Result applyOne(Project &project, const RemoveMedia &command);
Result applyOne(Project &project, const RelinkMedia &command);
Result applyOne(Project &project, const AddScene &command);
Result applyOne(Project &project, const RemoveScene &command);
Result applyOne(Project &project, const MoveScene &command);
Result applyOne(Project &project, const RenameScene &command);
Result applyOne(Project &project, const AddTrack &command);
Result applyOne(Project &project, const RemoveTrack &command);
Result applyOne(Project &project, const MoveTrack &command);
Result applyOne(Project &project, const SetTrackProperty &command);
Result applyOne(Project &project, const AddClip &command);
Result applyOne(Project &project, const RemoveClip &command);
Result applyOne(Project &project, const MoveClip &command);
Result applyOne(Project &project, const SetClipProperty &command);
Result applyOne(Project &project, const AddNotes &command);
Result applyOne(Project &project, const RemoveNotes &command);
Result applyOne(Project &project, const SetNotes &command);
Result applyOne(Project &project, const SetControllerData &command);
Result applyOne(Project &project, const AddDevice &command);
Result applyOne(Project &project, const RemoveDevice &command);
Result applyOne(Project &project, const MoveDevice &command);
Result applyOne(Project &project, const SetDeviceProperty &command);
Result applyOne(Project &project, const AddEnvelope &command);
Result applyOne(Project &project, const RemoveEnvelope &command);
Result applyOne(Project &project, const SetEnvelopePoints &command);

Applied restoreOne(Project &project, const RestoreMedia &restore);
Applied restoreOne(Project &project, const RestoreScene &restore);
Applied restoreOne(Project &project, const RestoreTrack &restore);
Applied restoreOne(Project &project, const RestoreClip &restore);
Applied restoreOne(Project &project, const RestoreNotes &restore);
Applied restoreOne(Project &project, const RestoreDevice &restore);
Applied restoreOne(Project &project, const RestoreEnvelope &restore);

} // namespace lina::model::detail

#endif // LINA_MODEL_SRC_APPLY_PARTS_HPP
