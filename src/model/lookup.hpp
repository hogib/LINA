// SPDX-License-Identifier: GPL-3.0-or-later
//
// Private to lina_model: lookups by identity that work on a const or a mutable
// project alike, and a walk over every object that has an identity.
//
// There is no index from identity to object: every lookup is a linear search.
// With T tracks (returns and master included), S scenes, C clips, D devices
// and E envelopes in the project:
//   - scene, media reference: O(number of them)
//   - track:    O(T)
//   - slot:     O(T * S)
//   - clip:     O(T * S + C)
//   - device:   O(T + D)
//   - envelope: O(T + E)
//   - note in a clip: O(notes in that clip)
// A command does one or two such lookups; the public find* functions in
// project.hpp cost the same.
#ifndef LINA_MODEL_SRC_LOOKUP_HPP
#define LINA_MODEL_SRC_LOOKUP_HPP

#include "lina/model/project.hpp"

#include <algorithm>
#include <type_traits>
#include <variant>

namespace lina::model::detail {

// `To`, const when `From` is.
template <class From, class To>
using CopyConst = std::conditional_t<std::is_const_v<From>, const To, To>;

// The element of `range` whose `id` member equals `id`, or null.
template <class Range, class IdType>
auto findById(Range &range, IdType id) -> decltype(&*range.begin()) {
  const auto found = std::ranges::find_if(
      range, [id](const auto &element) { return element.id == id; });
  return found == range.end() ? nullptr : &*found;
}

template <class ProjectType, class Function>
void forEachTrack(ProjectType &project, Function function) {
  for (auto &track : project.tracks) {
    function(track);
  }
  for (auto &track : project.returns) {
    function(track);
  }
  function(project.master);
}

template <class ProjectType>
auto trackIn(ProjectType &project, TrackId id)
    -> CopyConst<ProjectType, Track> * {
  if (auto *track = findById(project.tracks, id)) {
    return track;
  }
  if (auto *track = findById(project.returns, id)) {
    return track;
  }
  return project.master.id == id ? &project.master : nullptr;
}

template <class TrackType> struct SlotSite {
  TrackType *track{nullptr};
  CopyConst<TrackType, Slot> *slot{nullptr};
};

template <class ProjectType>
auto slotIn(ProjectType &project, SlotId id)
    -> SlotSite<CopyConst<ProjectType, Track>> {
  for (auto &track : project.tracks) {
    if (auto *slot = findById(track.slots, id)) {
      return {.track = &track, .slot = slot};
    }
  }
  return {};
}

template <class TrackType> struct ClipSite {
  TrackType *track{nullptr};
  CopyConst<TrackType, Slot> *slot{nullptr};
  CopyConst<TrackType, Clip> *clip{nullptr};
};

template <class ProjectType>
auto clipIn(ProjectType &project, ClipId id)
    -> ClipSite<CopyConst<ProjectType, Track>> {
  for (auto &track : project.tracks) {
    for (auto &slot : track.slots) {
      if (slot.clip.has_value() && slot.clip->id == id) {
        return {.track = &track, .slot = &slot, .clip = &*slot.clip};
      }
    }
    if (auto *clip = findById(track.arrangement, id)) {
      return {.track = &track, .slot = nullptr, .clip = clip};
    }
  }
  return {};
}

template <class TrackType> struct DeviceSite {
  TrackType *track{nullptr};
  CopyConst<TrackType, Device> *device{nullptr};
};

template <class ProjectType>
auto deviceIn(ProjectType &project, DeviceId id)
    -> DeviceSite<CopyConst<ProjectType, Track>> {
  DeviceSite<CopyConst<ProjectType, Track>> site;
  forEachTrack(project, [&site, id](auto &track) {
    if (auto *device = findById(track.devices, id)) {
      site = {.track = &track, .device = device};
    }
  });
  return site;
}

template <class TrackType> struct EnvelopeSite {
  TrackType *track{nullptr};
  CopyConst<TrackType, Envelope> *envelope{nullptr};
};

template <class ProjectType>
auto envelopeIn(ProjectType &project, EnvelopeId id)
    -> EnvelopeSite<CopyConst<ProjectType, Track>> {
  EnvelopeSite<CopyConst<ProjectType, Track>> site;
  forEachTrack(project, [&site, id](auto &track) {
    if (auto *envelope = findById(track.automation, id)) {
      site = {.track = &track, .envelope = envelope};
    }
  });
  return site;
}

// Calls `visit(object, parent)` for a clip and its notes, container first.
template <class Visitor>
void walkClip(const Clip &clip, ObjectRef parent, Visitor &visit) {
  visit(ObjectRef{clip.id}, parent);
  if (const auto *midi = std::get_if<MidiContent>(&clip.content)) {
    for (const Note &note : midi->notes) {
      visit(ObjectRef{note.id}, ObjectRef{clip.id});
    }
  }
}

template <class Visitor>
void walkSlot(const Slot &slot, ObjectRef parent, Visitor &visit) {
  visit(ObjectRef{slot.id}, parent);
  if (slot.clip.has_value()) {
    walkClip(*slot.clip, ObjectRef{slot.id}, visit);
  }
}

// Calls `visit(object, parent)` for a track and everything in it, containers
// first.
template <class Visitor> void walkTrack(const Track &track, Visitor &visit) {
  const ObjectRef self{track.id};
  visit(self, projectRef);
  for (const Device &device : track.devices) {
    visit(ObjectRef{device.id}, self);
  }
  for (const Envelope &envelope : track.automation) {
    visit(ObjectRef{envelope.id}, self);
  }
  for (const Slot &slot : track.slots) {
    walkSlot(slot, self, visit);
  }
  for (const Clip &clip : track.arrangement) {
    walkClip(clip, self, visit);
  }
}

// Calls `visit(object, parent)` for every object in the project that has an
// identity.
template <class Visitor>
void walkProject(const Project &project, Visitor &visit) {
  for (const MediaReference &media : project.media) {
    visit(ObjectRef{media.id}, projectRef);
  }
  for (const Scene &scene : project.scenes) {
    visit(ObjectRef{scene.id}, projectRef);
  }
  forEachTrack(project,
               [&visit](const Track &track) { walkTrack(track, visit); });
}

} // namespace lina::model::detail

#endif // LINA_MODEL_SRC_LOOKUP_HPP
