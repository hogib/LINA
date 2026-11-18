// SPDX-License-Identifier: GPL-3.0-or-later
//
// Not part of the model's API: how the document remembers what takes an edit
// back. The inverse of most commands is another command. The inverse of a
// removal is a restore, which puts the removed thing back with its identities
// and is trusted without checks, because the document only ever replays the
// restores it recorded itself, in the reverse of the order it recorded them.
// `Document::apply` and `Document::perform` take a `Command`, so no caller can
// hand the document a restore.
#ifndef LINA_MODEL_DETAIL_INVERSE_HPP
#define LINA_MODEL_DETAIL_INVERSE_HPP

#include "lina/model/command.hpp"

#include <cstddef>
#include <variant>
#include <vector>

namespace lina::model::detail {

struct RestoreMedia {
  MediaReference media;
  std::size_t index{0};
};

// `slots` holds the scene's slot for each audio and MIDI track, in track order.
struct RestoreScene {
  Scene scene;
  std::size_t index{0};
  std::vector<Slot> slots;
};

// An envelope and the track it belongs on.
struct OwnedEnvelope {
  TrackId track;
  Envelope envelope;
};

// For a return track, `sendLevels` holds the level of the send to it for each
// audio and MIDI track, in track order, and `sendEnvelopes` the automation of
// those sends that went with it.
struct RestoreTrack {
  Track track;
  std::size_t index{0};
  std::vector<double> sendLevels;
  std::vector<OwnedEnvelope> sendEnvelopes;
};

struct RestoreClip {
  ClipContainer container;
  Clip clip;
};

struct RestoreNotes {
  ClipId clip;
  std::vector<Note> notes;
};

// `envelopes` holds the automation of the device's parameters that went with
// it.
struct RestoreDevice {
  TrackId track;
  Device device;
  std::size_t index{0};
  std::vector<Envelope> envelopes;
};

struct RestoreEnvelope {
  TrackId track;
  Envelope envelope;
};

using Inverse =
    std::variant<Command, RestoreMedia, RestoreScene, RestoreTrack, RestoreClip,
                 RestoreNotes, RestoreDevice, RestoreEnvelope>;

} // namespace lina::model::detail

#endif // LINA_MODEL_DETAIL_INVERSE_HPP
