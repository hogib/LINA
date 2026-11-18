// SPDX-License-Identifier: GPL-3.0-or-later
//
// Commands (docs/design/13-architecture.md): plain values that describe one
// edit of the document. Applying one through `Document` is the only way to
// change a document.
//
// Two things to know:
//   - Add*: creates something new. Identities inside the given value must be
//     unassigned; the document assigns them (the change set tells which).
//   - Everything else acts on existing objects named by identity.
//
// There is no command that brings an object back with its old identity. Only
// undo does that, with inverses the document makes for itself and that no
// caller can construct a transaction from (detail/inverse.hpp).
//
// A command that would break a rule of the model fails and changes nothing.
// A failure names an object that exists: for an Add*, the container the new
// object was meant for. The one exception is "unknown object", which names
// the identity the command asked for. Commands never resolve overlaps by
// themselves: placing a clip over another, or a note over one of the same
// pitch, fails. The caller trims or removes first, in the same transaction.
//
// Floating-point values are stored with negative zero turned into zero, so
// that equal documents are written as equal bytes.
#ifndef LINA_MODEL_COMMAND_HPP
#define LINA_MODEL_COMMAND_HPP

#include "lina/model/change_set.hpp"
#include "lina/model/clip.hpp"
#include "lina/model/ids.hpp"
#include "lina/model/project.hpp"
#include "lina/model/time.hpp"
#include "lina/model/track.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace lina::model {

// A new value for the property `Which` of some object.
template <Property Which, class Type> struct Value {
  Type value{};

  friend bool operator==(const Value &, const Value &) = default;
};

using NameValue = Value<Property::Name, std::string>;
using ColourValue = Value<Property::Colour, Colour>;

// --- Project ---

using TempoValue = Value<Property::Tempo, Tempo>;
using TimeSignatureValue = Value<Property::TimeSignature, TimeSignature>;
using LoopRangeValue = Value<Property::LoopRange, LoopRange>;
using StartMarkerValue = Value<Property::StartMarker, TickPosition>;
using DefaultLaunchValue =
    Value<Property::LaunchQuantisation, LaunchQuantisation>;

using ProjectProperty =
    std::variant<TempoValue, TimeSignatureValue, LoopRangeValue,
                 StartMarkerValue, DefaultLaunchValue>;

struct SetProjectProperty {
  ProjectProperty property;

  friend bool operator==(const SetProjectProperty &,
                         const SetProjectProperty &) = default;
};

// --- Media references ---

// Appends a media reference to the project.
struct AddMedia {
  MediaReference media;

  friend bool operator==(const AddMedia &, const AddMedia &) = default;
};

// Fails while an audio clip still uses the reference.
struct RemoveMedia {
  MediaId media;

  friend bool operator==(const RemoveMedia &, const RemoveMedia &) = default;
};

// Points a media reference at the same file in another place.
struct RelinkMedia {
  MediaId media;
  std::string path;

  friend bool operator==(const RelinkMedia &, const RelinkMedia &) = default;
};

// --- Scenes ---

// Inserts a scene at `index` and gives every audio and MIDI track an empty slot
// for it.
struct AddScene {
  std::string name;
  std::size_t index{0};

  friend bool operator==(const AddScene &, const AddScene &) = default;
};

// Removes a scene with its slots and the clips in them.
struct RemoveScene {
  SceneId scene;

  friend bool operator==(const RemoveScene &, const RemoveScene &) = default;
};

struct MoveScene {
  SceneId scene;
  std::size_t index{0};

  friend bool operator==(const MoveScene &, const MoveScene &) = default;
};

struct RenameScene {
  SceneId scene;
  std::string name;

  friend bool operator==(const RenameScene &, const RenameScene &) = default;
};

// --- Tracks ---

// Inserts an empty audio, MIDI or return track at `index` of its list. An audio
// or MIDI track gets a slot per scene and a silent send per return track; a new
// return track gives every audio and MIDI track a silent send to it.
struct AddTrack {
  TrackKind kind{TrackKind::Audio};
  std::string name;
  std::size_t index{0};

  friend bool operator==(const AddTrack &, const AddTrack &) = default;
};

// Removes an audio, MIDI or return track with everything on it. Removing a
// return track also removes every send to it and the automation of those
// sends.
struct RemoveTrack {
  TrackId track;

  friend bool operator==(const RemoveTrack &, const RemoveTrack &) = default;
};

// Moves a track to `index` of its own list (audio and MIDI tracks, or return
// tracks).
struct MoveTrack {
  TrackId track;
  std::size_t index{0};

  friend bool operator==(const MoveTrack &, const MoveTrack &) = default;
};

using VolumeValue = Value<Property::Volume, double>;
using PanValue = Value<Property::Pan, double>;
using MuteValue = Value<Property::Mute, bool>;
using SoloValue = Value<Property::Solo, bool>;
using ArmedValue = Value<Property::Armed, bool>;
using MonitoringValue = Value<Property::Monitoring, Monitoring>;
// The level of the send to one return track.
using SendValue = Value<Property::Sends, Send>;
using InputValue = Value<Property::Input, TrackInput>;

using TrackProperty =
    std::variant<NameValue, ColourValue, VolumeValue, PanValue, MuteValue,
                 SoloValue, ArmedValue, MonitoringValue, SendValue, InputValue>;

// Fails with WrongTrackKind where the track has no such control: the master
// has only a name, a colour and a volume; return tracks have no arm,
// monitoring, input or sends. An input of the wrong sort for the track fails
// with WrongInputKind.
struct SetTrackProperty {
  TrackId track;
  TrackProperty property;

  friend bool operator==(const SetTrackProperty &,
                         const SetTrackProperty &) = default;
};

// --- Clips ---

// Where a clip goes: a slot, or the arrangement of a track.
using ClipContainer = std::variant<SlotId, TrackId>;

// Puts a new clip into an empty slot or, at `clip.extent.position`, into an
// arrangement.
struct AddClip {
  ClipContainer container;
  Clip clip;

  friend bool operator==(const AddClip &, const AddClip &) = default;
};

struct RemoveClip {
  ClipId clip;

  friend bool operator==(const RemoveClip &, const RemoveClip &) = default;
};

// Moves a clip to an empty slot, or to `position` in an arrangement (possibly
// its own), of a track of the same kind. `position` must be zero when the
// container is a slot.
struct MoveClip {
  ClipId clip;
  ClipContainer container;
  TickPosition position;

  friend bool operator==(const MoveClip &, const MoveClip &) = default;
};

using ExtentValue = Value<Property::Extent, ClipExtent>;
using LoopValue = Value<Property::Loop, LoopSettings>;
using LaunchOverrideValue =
    Value<Property::LaunchQuantisation, std::optional<LaunchQuantisation>>;
using GainValue = Value<Property::Gain, double>;
using FollowTempoValue = Value<Property::FollowTempo, bool>;
using StretchModeValue = Value<Property::StretchMode, StretchMode>;
using TransposeValue = Value<Property::Transpose, std::int32_t>;
using WarpValue = Value<Property::Warp, WarpMap>;
using RegionValue = Value<Property::Region, SampleRange>;

using ClipProperty =
    std::variant<NameValue, ColourValue, ExtentValue, LoopValue,
                 LaunchOverrideValue, GainValue, FollowTempoValue,
                 StretchModeValue, TransposeValue, WarpValue, RegionValue>;

// The properties from gain onwards exist on audio clips only (WrongClipKind
// otherwise).
struct SetClipProperty {
  ClipId clip;
  ClipProperty property;

  friend bool operator==(const SetClipProperty &,
                         const SetClipProperty &) = default;
};

// --- Notes ---

// Adds notes to a MIDI clip; identities are assigned in the order given.
struct AddNotes {
  ClipId clip;
  std::vector<Note> notes;

  friend bool operator==(const AddNotes &, const AddNotes &) = default;
};

struct RemoveNotes {
  ClipId clip;
  std::vector<NoteId> notes;

  friend bool operator==(const RemoveNotes &, const RemoveNotes &) = default;
};

// Replaces the pitch, start, length and velocity of existing notes, named by
// the identity in each given note, all at once: only the result has to be free
// of overlaps.
struct SetNotes {
  ClipId clip;
  std::vector<Note> notes;

  friend bool operator==(const SetNotes &, const SetNotes &) = default;
};

// --- Devices ---

// Inserts a new device at `index` of a track's chain.
struct AddDevice {
  TrackId track;
  Device device;
  std::size_t index{0};

  friend bool operator==(const AddDevice &, const AddDevice &) = default;
};

// Removes a device together with the automation of its parameters.
struct RemoveDevice {
  DeviceId device;

  friend bool operator==(const RemoveDevice &, const RemoveDevice &) = default;
};

// Moves a device to `index` of the chain of `track`: its own track to reorder,
// another track to move it there. The device keeps its identity, its state and
// its parameter values, and automation of its parameters goes with it. The
// result must satisfy the chain rule: an instrument only first on a MIDI
// track.
struct MoveDevice {
  DeviceId device;
  TrackId track;
  std::size_t index{0};

  friend bool operator==(const MoveDevice &, const MoveDevice &) = default;
};

// Sets one parameter's stored value, or forgets it when `value` is empty.
struct ParameterSetting {
  std::string id;
  std::optional<double> value;

  friend bool operator==(const ParameterSetting &,
                         const ParameterSetting &) = default;
};

using BypassedValue = Value<Property::Bypassed, bool>;
using StateValue = Value<Property::State, std::string>;
using ParameterSettingValue = Value<Property::Parameters, ParameterSetting>;

using DeviceProperty =
    std::variant<BypassedValue, StateValue, ParameterSettingValue>;

struct SetDeviceProperty {
  DeviceId device;
  DeviceProperty property;

  friend bool operator==(const SetDeviceProperty &,
                         const SetDeviceProperty &) = default;
};

// --- Controller data ---

// Replaces the recorded data of one controller in a MIDI clip: the envelope
// with the same kind and controller number, which is created if the clip has
// none and removed when `data.points` is empty.
struct SetControllerData {
  ClipId clip;
  ControllerEnvelope data;

  friend bool operator==(const SetControllerData &,
                         const SetControllerData &) = default;
};

// --- Automation ---

// Gives a parameter of a track an envelope. The target must be on that track
// and not have an envelope yet.
struct AddEnvelope {
  TrackId track;
  Envelope envelope;

  friend bool operator==(const AddEnvelope &, const AddEnvelope &) = default;
};

struct RemoveEnvelope {
  EnvelopeId envelope;

  friend bool operator==(const RemoveEnvelope &,
                         const RemoveEnvelope &) = default;
};

// Replaces all points of an envelope. They must be in time order and inside
// the range of the parameter.
struct SetEnvelopePoints {
  EnvelopeId envelope;
  std::vector<AutomationPoint> points;

  friend bool operator==(const SetEnvelopePoints &,
                         const SetEnvelopePoints &) = default;
};

using Command =
    std::variant<SetProjectProperty, AddMedia, RemoveMedia, RelinkMedia,
                 AddScene, RemoveScene, MoveScene, RenameScene, AddTrack,
                 RemoveTrack, MoveTrack, SetTrackProperty, AddClip, RemoveClip,
                 MoveClip, SetClipProperty, AddNotes, RemoveNotes, SetNotes,
                 SetControllerData, AddDevice, RemoveDevice, MoveDevice,
                 SetDeviceProperty, AddEnvelope, RemoveEnvelope,
                 SetEnvelopePoints>;

} // namespace lina::model

#endif // LINA_MODEL_COMMAND_HPP
