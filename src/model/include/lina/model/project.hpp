// SPDX-License-Identifier: GPL-3.0-or-later
//
// The project: everything saved together as one piece of work
// (docs/design/03-glossary-and-domain-model.md). A plain value with deep
// equality. Code that only reads a document works on `const Project &`; the
// only way to change one that is in use is through `Document` (document.hpp).
#ifndef LINA_MODEL_PROJECT_HPP
#define LINA_MODEL_PROJECT_HPP

#include "lina/model/clip.hpp"
#include "lina/model/fault.hpp"
#include "lina/model/ids.hpp"
#include "lina/model/time.hpp"
#include "lina/model/track.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace lina::model {

// One row of the session.
struct Scene {
  SceneId id;
  std::string name;

  friend bool operator==(const Scene &, const Scene &) = default;
};

// The arrangement's loop range.
struct LoopRange {
  bool enabled{false};
  TickPosition start;
  TickDuration length{ticksPerWholeNote * 4};

  friend bool operator==(const LoopRange &, const LoopRange &) = default;
};

struct Project {
  // The identity counter: the next identity the document hands out. Never
  // decreases.
  std::uint64_t nextId{1};
  Tempo tempo;
  TimeSignature timeSignature;
  LoopRange loop;
  // Where play starts from.
  TickPosition startMarker;
  LaunchQuantisation launchQuantisation{LaunchQuantisation::Bar1};
  std::vector<MediaReference> media;
  std::vector<Scene> scenes;
  // Audio and MIDI tracks, in display order.
  std::vector<Track> tracks;
  std::vector<Track> returns;
  Track master;

  friend bool operator==(const Project &, const Project &) = default;
};

// A project with a master track and nothing else.
Project makeEmptyProject();

// Equality of everything except the identity counter. Undoing an edit restores
// the content but never hands an identity out twice, so the counter stays where
// it was.
bool sameContent(const Project &a, const Project &b);

// Checks every rule of the model over a whole project and returns everything it
// finds; empty means the project is valid. Every document a `Document` holds
// passes this at all times.
std::vector<Failure> validate(const Project &project);

// Where a clip lives. `slot` is null for an arrangement clip.
struct ClipLocation {
  const Track *track{nullptr};
  const Slot *slot{nullptr};
  const Clip *clip{nullptr};
};

struct SlotLocation {
  const Track *track{nullptr};
  const Slot *slot{nullptr};
};

struct DeviceLocation {
  const Track *track{nullptr};
  const Device *device{nullptr};
};

struct EnvelopeLocation {
  const Track *track{nullptr};
  const Envelope *envelope{nullptr};
};

// Lookups by identity. Each returns null pointers when the project has no such
// object. The pointers are invalidated by the next edit of the document.
// `findTrack` also finds return tracks and the master. Every lookup is a
// linear search through the project; there is no index yet.
const Scene *findScene(const Project &project, SceneId id);
const MediaReference *findMedia(const Project &project, MediaId id);
const Track *findTrack(const Project &project, TrackId id);
SlotLocation findSlot(const Project &project, SlotId id);
ClipLocation findClip(const Project &project, ClipId id);
DeviceLocation findDevice(const Project &project, DeviceId id);
EnvelopeLocation findEnvelope(const Project &project, EnvelopeId id);
const Note *findNote(const Clip &clip, NoteId id);

} // namespace lina::model

#endif // LINA_MODEL_PROJECT_HPP
