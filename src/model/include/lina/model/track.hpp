// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tracks and what hangs off them (docs/design/03, 06, 09): slots, arrangement
// clips, the device chain, the mixer strip and automation envelopes. Plain
// values with deep equality.
#ifndef LINA_MODEL_TRACK_HPP
#define LINA_MODEL_TRACK_HPP

#include "lina/model/clip.hpp"
#include "lina/model/ids.hpp"
#include "lina/model/time.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace lina::model {

// Top of the volume range. The bottom is `silenceDb`.
inline constexpr double maxVolumeDb = 6.0;
// Top of the send level range. The bottom is `silenceDb`.
inline constexpr double maxSendDb = 0.0;

// What a hosted plugin does in a chain.
enum class DeviceRole : std::uint8_t { Instrument, AudioEffect };

// Names the plugin a device stands for, so that a missing plugin can still be
// shown.
struct PluginDescriptor {
  std::string format;
  std::string identifier;
  std::string name;
  std::string vendor;
  std::string version;

  friend bool operator==(const PluginDescriptor &,
                         const PluginDescriptor &) = default;
};

// The value of one plugin parameter, normalised to 0..1, keyed by the plugin's
// own stable parameter identifier.
struct ParameterValue {
  std::string id;
  double value{0.0};

  friend bool operator==(const ParameterValue &,
                         const ParameterValue &) = default;
};

// A hosted plugin in a track's chain. The model keeps a reference to the
// plugin's opaque state (the name of a state file) and the parameter values set
// from LINA, sorted by identifier.
struct Device {
  DeviceId id;
  DeviceRole role{DeviceRole::AudioEffect};
  PluginDescriptor plugin;
  std::string stateRef;
  bool bypassed{false};
  std::vector<ParameterValue> parameters;

  friend bool operator==(const Device &, const Device &) = default;
};

// When a track's input is heard.
enum class Monitoring : std::uint8_t { Off, On, Auto };

// Number of hardware input channels a track input can name.
inline constexpr std::uint32_t maxInputChannels = 1'024;

// Where a track records and monitors from. The model names an input without
// needing the device to be present: audio inputs by channel number of whatever
// interface is in use, MIDI inputs by the name the system reports for the
// device. An input that is not there when the project is opened is kept as it
// is.
struct NoInput {
  friend bool operator==(const NoInput &, const NoInput &) = default;
};

// One input channel of the audio interface, counted from zero, or the stereo
// pair that starts there.
struct AudioInput {
  std::uint32_t channel{0};
  bool stereo{false};

  friend bool operator==(const AudioInput &, const AudioInput &) = default;
};

// Every connected MIDI device.
struct AllMidiInputs {
  friend bool operator==(const AllMidiInputs &,
                         const AllMidiInputs &) = default;
};

// One MIDI device, by name.
struct MidiDeviceInput {
  std::string device;

  friend bool operator==(const MidiDeviceInput &,
                         const MidiDeviceInput &) = default;
};

// An audio track has no input or an audio input; a MIDI track has all MIDI
// inputs or one device; return tracks and the master have none.
using TrackInput =
    std::variant<NoInput, AudioInput, AllMidiInputs, MidiDeviceInput>;

// The level of the copy of a track's signal that feeds one return track.
struct Send {
  TrackId target;
  double levelDb{silenceDb};

  friend bool operator==(const Send &, const Send &) = default;
};

// A track's mixer strip. `sends` holds one entry per return track, in the
// project's return order. Pan runs from -1 (left) to 1 (right).
struct MixerStrip {
  double volumeDb{0.0};
  double pan{0.0};
  bool mute{false};
  bool solo{false};
  bool armed{false};
  Monitoring monitoring{Monitoring::Auto};
  TrackInput input;
  std::vector<Send> sends;

  friend bool operator==(const MixerStrip &, const MixerStrip &) = default;
};

// The mixer strip controls that can be automated.
enum class MixerParameter : std::uint8_t { Volume, Pan };

struct MixerTarget {
  MixerParameter parameter{MixerParameter::Volume};

  friend bool operator==(const MixerTarget &, const MixerTarget &) = default;
};

struct SendTarget {
  TrackId target;

  friend bool operator==(const SendTarget &, const SendTarget &) = default;
};

struct DeviceParameterTarget {
  DeviceId device;
  std::string parameter;

  friend bool operator==(const DeviceParameterTarget &,
                         const DeviceParameterTarget &) = default;
};

// The parameter an envelope drives: a control of the envelope's own track, one
// of its sends, or a parameter of one of its devices.
using AutomationTarget =
    std::variant<MixerTarget, SendTarget, DeviceParameterTarget>;

struct AutomationPoint {
  TickPosition time;
  double value{0.0};

  friend bool operator==(const AutomationPoint &,
                         const AutomationPoint &) = default;
};

// Automation for one parameter: points in arrangement time, in time order,
// joined by straight segments. Two points at the same time are a step. Values
// are in the unit of the parameter: decibels for volume and sends, -1..1 for
// pan, 0..1 for a plugin parameter. A track keeps its envelopes sorted by
// identity.
struct Envelope {
  EnvelopeId id;
  AutomationTarget target;
  std::vector<AutomationPoint> points;

  friend bool operator==(const Envelope &, const Envelope &) = default;
};

// One cell of the session: belongs to one track and one scene, empty or holding
// one clip.
struct Slot {
  SlotId id;
  SceneId scene;
  std::optional<Clip> clip;

  friend bool operator==(const Slot &, const Slot &) = default;
};

enum class TrackKind : std::uint8_t { Audio, Midi, Return, Master };

// A track of any kind. Audio and MIDI tracks have one slot per scene, in scene
// order, and arrangement clips sorted by position. Return tracks and the master
// have neither.
struct Track {
  TrackId id;
  TrackKind kind{TrackKind::Audio};
  std::string name;
  Colour colour;
  MixerStrip mixer;
  std::vector<Device> devices;
  std::vector<Slot> slots;
  std::vector<Clip> arrangement;
  std::vector<Envelope> automation;

  friend bool operator==(const Track &, const Track &) = default;
};

} // namespace lina::model

#endif // LINA_MODEL_TRACK_HPP
