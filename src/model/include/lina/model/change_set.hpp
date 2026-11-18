// SPDX-License-Identifier: GPL-3.0-or-later
//
// Change sets (docs/design/13-architecture.md): what an edit did to the
// document, precise enough to update a view or the engine's projection without
// rebuilding it.
//
// A change set describes the difference between the document before and after
// one command, one transaction, one undo or one redo:
//   - Every object with an identity that came into existence is listed as
//     Added, and every one that went out of existence as Removed. Nothing is
//     implied: adding a track lists the track, each of its slots, clips, notes
//     and devices.
//   - An object that exists on both sides and whose own properties or
//     container changed is listed as Changed, with the properties that
//     changed. For a single command the list is exact. After merging (a
//     transaction of several commands) it may name a property that ended up
//     with its old value, but never misses one that differs.
//   - Each object appears at most once.
//
// Entries are sorted by kind, then identity. Kinds are in containment order
// (ids.hpp), so reading forwards meets a container before its contents, and
// reading backwards the reverse. Added and Changed objects are looked up in the
// document as it is after the edit; Removed ones are gone, which is why their
// old container is recorded here.
//
// What a consumer has to know beyond that:
//   - Moves. An object that moved to another container (a clip to another slot
//     or track, a device to another track, an envelope following its device)
//     is Changed with the property Container, `previousParent` the old
//     container and `parent` the new one. What is inside a moved object has no
//     entries: a moved clip's notes are neither Added nor Removed nor Changed.
//   - A move out of something that is then removed. When a clip is moved off a
//     track and the track is removed in the same transaction, the track is
//     Removed and the clip is Changed with `previousParent` naming the removed
//     track. A consumer that tears down everything it holds for a Removed
//     container must first rehome the objects whose `previousParent` is that
//     container.
//   - Removed then put back. An object removed and added again with the same
//     identity inside one change set (an undo and a redo merged, or a remove
//     undone inside a rolled-back transaction) is one Changed entry with every
//     property set (`PropertySet::all()`), `previousParent` where it was and
//     `parent` where it is now. The same holds, entry by entry, for everything
//     inside it. Treat it as "rebuild this object".
//   - Reorders. Reordering scenes, tracks or return tracks is reported on the
//     Project only (SceneOrder, TrackOrder, ReturnOrder). The slots of every
//     track follow scene order and the sends of every track follow return
//     order, without entries of their own. Reordering devices is reported on
//     their track (DeviceOrder), not on the devices. A device moved to another
//     track is reported on itself (Container) and on both tracks (DeviceOrder);
//     where it now sits in the chain is read from the document.
//   - Dependants. Only the object a command names is reported, not what refers
//     to it: relinking a media reference reports the media reference (Path),
//     and the audio clips that use it must be found by the consumer. Changing
//     the tempo or the time signature reports the Project only.
//   - Created and destroyed inside one change set: no entry at all.
//   - A property named in a merged change set may have ended up with the value
//     it had before (set, then set back). Only single commands are exact.
#ifndef LINA_MODEL_CHANGE_SET_HPP
#define LINA_MODEL_CHANGE_SET_HPP

#include "lina/model/ids.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace lina::model {

// The properties a change can name. Which ones apply depends on the kind of
// object.
enum class Property : std::uint8_t {
  // Track, scene, clip.
  Name,
  // Track, clip.
  Colour,
  // Project.
  Tempo,
  TimeSignature,
  LoopRange,
  StartMarker,
  // The order of the project's scenes, tracks or return tracks changed without
  // any of them being added or removed. Slots follow scene order and sends
  // follow return order.
  SceneOrder,
  TrackOrder,
  ReturnOrder,
  // Project (the default) and clip (its override).
  LaunchQuantisation,
  // Track: the mixer strip.
  Volume,
  Pan,
  Mute,
  Solo,
  Armed,
  Monitoring,
  // Track: a send level changed, or a send appeared or vanished with its return
  // track.
  Sends,
  // Track: the order of its devices changed, or a device was moved in from
  // or out to another track. Not set when devices are only added or removed.
  DeviceOrder,
  // Clip: position, length or content start. Position also changes when a clip
  // is moved.
  Extent,
  // Clip: it now lives in another slot or another track's arrangement.
  Container,
  Loop,
  // Audio clip.
  Gain,
  FollowTempo,
  StretchMode,
  Transpose,
  Warp,
  Region,
  // Note: pitch, start, length or velocity.
  NoteData,
  // Device.
  Bypassed,
  State,
  Parameters,
  // Media reference.
  Path,
  // Track: which input it records from.
  Input,
  // Envelope: its points.
  Points,
  // MIDI clip: its controller data.
  Controllers,
};

// A set of properties.
class PropertySet {
public:
  constexpr PropertySet() = default;
  // Implicit on purpose: one property is a set of one.
  // NOLINTNEXTLINE(cppcoreguidelines-explicit-constructor,misc-explicit-constructor)
  constexpr PropertySet(Property property)
      : bits_(std::uint64_t{1} << static_cast<unsigned>(property)) {}

  // Every property; used when an object was taken out and put back within one
  // change set.
  static constexpr PropertySet all() {
    PropertySet set;
    set.bits_ = ~std::uint64_t{0};
    return set;
  }

  [[nodiscard]] constexpr bool contains(Property property) const {
    return (bits_ & PropertySet{property}.bits_) != 0;
  }
  [[nodiscard]] constexpr bool empty() const { return bits_ == 0; }

  constexpr PropertySet &operator|=(PropertySet other) {
    bits_ |= other.bits_;
    return *this;
  }
  friend constexpr PropertySet operator|(PropertySet a, PropertySet b) {
    return a |= b;
  }
  friend constexpr bool operator==(PropertySet, PropertySet) = default;

private:
  std::uint64_t bits_{0};
};

enum class ChangeKind : std::uint8_t { Added, Removed, Changed };

// What happened to one object.
struct Change {
  ObjectRef object;
  ChangeKind kind{ChangeKind::Changed};
  // The container after the edit; for a Removed object, the one it was taken
  // out of. A track, scene or media reference is contained by the project; a
  // slot, device or envelope by its track; a clip by its slot or, in the
  // arrangement, by its track; a note by its clip.
  ObjectRef parent;
  // The container before the edit. Differs from `parent` only for a Changed
  // object that moved.
  ObjectRef previousParent;
  // For Changed: which properties. Empty for Added and Removed.
  PropertySet properties;

  friend bool operator==(const Change &, const Change &) = default;
};

class ChangeSet {
public:
  // Record one object. Each folds into what the set already says about that
  // object, so the calls can be made in the order the edits happened.
  void added(ObjectRef object, ObjectRef parent);
  void removed(ObjectRef object, ObjectRef parent);
  void changed(ObjectRef object, ObjectRef parent, PropertySet properties);
  // Changed, with a container that differs from before.
  void moved(ObjectRef object, ObjectRef previousParent, ObjectRef parent,
             PropertySet properties);

  // Folds in a change set that describes a later edit.
  void merge(const ChangeSet &later);

  [[nodiscard]] const std::vector<Change> &changes() const { return changes_; }
  [[nodiscard]] bool empty() const { return changes_.empty(); }
  // What the set says about one object, or null when it says nothing.
  [[nodiscard]] const Change *find(ObjectRef object) const;

  // The identity of the first Added object of a kind, in identity order: how a
  // caller learns the identity the document gave to what a command created.
  template <ObjectKind Kind>
  [[nodiscard]] std::optional<Id<Kind>> firstAdded() const {
    const std::optional<std::uint64_t> id = firstAddedOfKind(Kind);
    return id.has_value() ? std::optional{Id<Kind>{*id}} : std::nullopt;
  }

  friend bool operator==(const ChangeSet &, const ChangeSet &) = default;

private:
  void fold(const Change &later);
  [[nodiscard]] std::optional<std::uint64_t>
  firstAddedOfKind(ObjectKind kind) const;

  // Sorted by object, one entry per object.
  std::vector<Change> changes_;
};

} // namespace lina::model

#endif // LINA_MODEL_CHANGE_SET_HPP
