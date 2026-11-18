// SPDX-License-Identifier: GPL-3.0-or-later
#include "support/diff.hpp"

#include <algorithm>
#include <map>
#include <variant>
#include <vector>

namespace lina::model::testing {

namespace {

using ObjectPointer =
    std::variant<const MediaReference *, const Scene *, const Track *,
                 const Slot *, const Device *, const Envelope *, const Clip *,
                 const Note *>;

struct Located {
  ObjectRef parent;
  ObjectPointer object;
};

using Flat = std::map<ObjectRef, Located>;

void flattenClip(const Clip &clip, ObjectRef parent, Flat &flat) {
  flat[ObjectRef{clip.id}] = {.parent = parent, .object = &clip};
  if (const auto *midi = std::get_if<MidiContent>(&clip.content)) {
    for (const Note &note : midi->notes) {
      flat[ObjectRef{note.id}] = {
          .parent = ObjectRef{clip.id},
          .object = &note,
      };
    }
  }
}

void flattenTrack(const Track &track, Flat &flat) {
  const ObjectRef self{track.id};
  flat[self] = {.parent = projectRef, .object = &track};
  for (const Slot &slot : track.slots) {
    flat[ObjectRef{slot.id}] = {.parent = self, .object = &slot};
    if (slot.clip.has_value()) {
      flattenClip(*slot.clip, ObjectRef{slot.id}, flat);
    }
  }
  for (const Clip &clip : track.arrangement) {
    flattenClip(clip, self, flat);
  }
  for (const Device &device : track.devices) {
    flat[ObjectRef{device.id}] = {.parent = self, .object = &device};
  }
  for (const Envelope &envelope : track.automation) {
    flat[ObjectRef{envelope.id}] = {.parent = self, .object = &envelope};
  }
}

Flat flatten(const Project &project) {
  Flat flat;
  for (const MediaReference &media : project.media) {
    flat[ObjectRef{media.id}] = {.parent = projectRef, .object = &media};
  }
  for (const Scene &scene : project.scenes) {
    flat[ObjectRef{scene.id}] = {.parent = projectRef, .object = &scene};
  }
  for (const Track &track : project.tracks) {
    flattenTrack(track, flat);
  }
  for (const Track &track : project.returns) {
    flattenTrack(track, flat);
  }
  flattenTrack(project.master, flat);
  return flat;
}

template <class Type>
void note(PropertySet &set, Property property, const Type &before,
          const Type &after) {
  if (before != after) {
    set |= property;
  }
}

template <class Element>
std::vector<std::uint64_t> idsOf(const std::vector<Element> &list) {
  std::vector<std::uint64_t> ids;
  ids.reserve(list.size());
  for (const Element &element : list) {
    ids.push_back(element.id.value);
  }
  return ids;
}

// True when the elements present on both sides are in a different relative
// order.
bool orderDiffers(const std::vector<std::uint64_t> &before,
                  const std::vector<std::uint64_t> &after) {
  const auto keepShared = [](const std::vector<std::uint64_t> &list,
                             const std::vector<std::uint64_t> &other) {
    std::vector<std::uint64_t> shared;
    for (const std::uint64_t id : list) {
      if (std::ranges::find(other, id) != other.end()) {
        shared.push_back(id);
      }
    }
    return shared;
  };
  return keepShared(before, after) != keepShared(after, before);
}

std::vector<Send> sortedSends(const Track &track) {
  std::vector<Send> sends = track.mixer.sends;
  std::ranges::sort(sends, {}, &Send::target);
  return sends;
}

PropertySet differing(const MediaReference &before,
                      const MediaReference &after) {
  PropertySet set;
  note(set, Property::Path, before.path, after.path);
  return set;
}

PropertySet differing(const Scene &before, const Scene &after) {
  PropertySet set;
  note(set, Property::Name, before.name, after.name);
  return set;
}

PropertySet differing(const Track &before, const Track &after) {
  PropertySet set;
  note(set, Property::Name, before.name, after.name);
  note(set, Property::Colour, before.colour, after.colour);
  note(set, Property::Volume, before.mixer.volumeDb, after.mixer.volumeDb);
  note(set, Property::Pan, before.mixer.pan, after.mixer.pan);
  note(set, Property::Mute, before.mixer.mute, after.mixer.mute);
  note(set, Property::Solo, before.mixer.solo, after.mixer.solo);
  note(set, Property::Armed, before.mixer.armed, after.mixer.armed);
  note(set, Property::Monitoring, before.mixer.monitoring,
       after.mixer.monitoring);
  note(set, Property::Input, before.mixer.input, after.mixer.input);
  note(set, Property::Sends, sortedSends(before), sortedSends(after));
  if (orderDiffers(idsOf(before.devices), idsOf(after.devices))) {
    set |= Property::DeviceOrder;
  }
  return set;
}

PropertySet differing(const Slot & /*before*/, const Slot & /*after*/) {
  return {};
}

PropertySet differing(const Device &before, const Device &after) {
  PropertySet set;
  note(set, Property::Bypassed, before.bypassed, after.bypassed);
  note(set, Property::State, before.stateRef, after.stateRef);
  note(set, Property::Parameters, before.parameters, after.parameters);
  return set;
}

PropertySet differing(const Envelope &before, const Envelope &after) {
  PropertySet set;
  note(set, Property::Points, before.points, after.points);
  return set;
}

PropertySet differing(const Clip &before, const Clip &after) {
  PropertySet set;
  note(set, Property::Name, before.name, after.name);
  note(set, Property::Colour, before.colour, after.colour);
  note(set, Property::Extent, before.extent, after.extent);
  note(set, Property::Loop, before.loop, after.loop);
  note(set, Property::LaunchQuantisation, before.launchQuantisation,
       after.launchQuantisation);
  const auto *midiBefore = std::get_if<MidiContent>(&before.content);
  const auto *midiAfter = std::get_if<MidiContent>(&after.content);
  if (midiBefore != nullptr && midiAfter != nullptr) {
    note(set, Property::Controllers, midiBefore->controllers,
         midiAfter->controllers);
  }
  const auto *audioBefore = std::get_if<AudioContent>(&before.content);
  const auto *audioAfter = std::get_if<AudioContent>(&after.content);
  if (audioBefore != nullptr && audioAfter != nullptr) {
    note(set, Property::Gain, audioBefore->gainDb, audioAfter->gainDb);
    note(set, Property::FollowTempo, audioBefore->followTempo,
         audioAfter->followTempo);
    note(set, Property::StretchMode, audioBefore->stretchMode,
         audioAfter->stretchMode);
    note(set, Property::Transpose, audioBefore->transposeCents,
         audioAfter->transposeCents);
    note(set, Property::Warp, audioBefore->warp, audioAfter->warp);
    note(set, Property::Region, audioBefore->region, audioAfter->region);
  }
  return set;
}

PropertySet differing(const Note &before, const Note &after) {
  PropertySet set;
  note(set, Property::NoteData, before, after);
  return set;
}

PropertySet differing(const ObjectPointer &before, const ObjectPointer &after) {
  return std::visit(
      [&after](const auto *typedBefore) {
        const auto *typedAfter = std::get<decltype(typedBefore)>(after);
        return differing(*typedBefore, *typedAfter);
      },
      before);
}

PropertySet differing(const Project &before, const Project &after) {
  PropertySet set;
  note(set, Property::Tempo, before.tempo, after.tempo);
  note(set, Property::TimeSignature, before.timeSignature, after.timeSignature);
  note(set, Property::LoopRange, before.loop, after.loop);
  note(set, Property::StartMarker, before.startMarker, after.startMarker);
  note(set, Property::LaunchQuantisation, before.launchQuantisation,
       after.launchQuantisation);
  if (orderDiffers(idsOf(before.scenes), idsOf(after.scenes))) {
    set |= Property::SceneOrder;
  }
  if (orderDiffers(idsOf(before.tracks), idsOf(after.tracks))) {
    set |= Property::TrackOrder;
  }
  if (orderDiffers(idsOf(before.returns), idsOf(after.returns))) {
    set |= Property::ReturnOrder;
  }
  return set;
}

bool containsAll(PropertySet whole, PropertySet part) {
  return (whole | part) == whole;
}

} // namespace

ChangeSet diff(const Project &before, const Project &after) {
  ChangeSet changes;
  const Flat flatBefore = flatten(before);
  const Flat flatAfter = flatten(after);

  for (const auto &[object, located] : flatBefore) {
    const auto survivor = flatAfter.find(object);
    if (survivor == flatAfter.end()) {
      changes.removed(object, located.parent);
      continue;
    }
    PropertySet properties = differing(located.object, survivor->second.object);
    if (located.parent != survivor->second.parent) {
      properties |= Property::Container;
      if (std::holds_alternative<const Device *>(located.object)) {
        // A device that changed tracks disturbs both chains, if they are
        // still there.
        for (const ObjectRef track :
             {located.parent, survivor->second.parent}) {
          if (flatBefore.contains(track) && flatAfter.contains(track)) {
            changes.changed(track, projectRef, Property::DeviceOrder);
          }
        }
      }
    }
    if (!properties.empty()) {
      changes.moved(object, located.parent, survivor->second.parent,
                    properties);
    }
  }
  for (const auto &[object, located] : flatAfter) {
    if (!flatBefore.contains(object)) {
      changes.added(object, located.parent);
    }
  }

  const PropertySet ofProject = differing(before, after);
  if (!ofProject.empty()) {
    changes.changed(projectRef, projectRef, ofProject);
  }
  return changes;
}

bool covers(const ChangeSet &claimed, const ChangeSet &exact) {
  for (const Change &wanted : exact.changes()) {
    const Change *found = claimed.find(wanted.object);
    if (found == nullptr || found->kind != wanted.kind ||
        found->parent != wanted.parent ||
        found->previousParent != wanted.previousParent ||
        !containsAll(found->properties, wanted.properties)) {
      return false;
    }
  }
  // What is claimed beyond that may only be "possibly changed, where it was".
  return std::ranges::all_of(claimed.changes(), [&exact](const Change &change) {
    return exact.find(change.object) != nullptr ||
           (change.kind == ChangeKind::Changed &&
            change.parent == change.previousParent);
  });
}

ParentMap parentsOf(const Project &project) {
  ParentMap parents;
  for (const auto &[object, located] : flatten(project)) {
    parents[object] = located.parent;
  }
  return parents;
}

bool follow(ParentMap &picture, const ChangeSet &changes) {
  for (const Change &change : changes.changes()) {
    const auto known = picture.find(change.object);
    if (change.object == projectRef) {
      continue;
    }
    if (change.kind == ChangeKind::Added) {
      if (known != picture.end()) {
        return false;
      }
      picture[change.object] = change.parent;
    } else if (known == picture.end() ||
               known->second != change.previousParent) {
      return false;
    } else if (change.kind == ChangeKind::Removed) {
      picture.erase(known);
    } else {
      known->second = change.parent;
    }
  }
  return true;
}

} // namespace lina::model::testing
