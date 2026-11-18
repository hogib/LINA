// SPDX-License-Identifier: GPL-3.0-or-later
//
// Clips and what they contain (docs/design/03, 05, 07, 08): notes, controller
// data, audio regions and the media references those point at. Plain values
// with deep equality.
#ifndef LINA_MODEL_CLIP_HPP
#define LINA_MODEL_CLIP_HPP

#include "lina/model/ids.hpp"
#include "lina/model/time.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace lina::model {

// Decibel value at and below which a level control means silence.
inline constexpr double silenceDb = -96.0;
// Range of an audio clip's gain.
inline constexpr double maxClipGainDb = 24.0;
// Range of an audio clip's transposition, in cents either way.
inline constexpr std::int32_t maxTransposeCents = 4'800;

// A display colour, 0xRRGGBB.
struct Colour {
  std::uint32_t rgb{0x808080};

  friend bool operator==(const Colour &, const Colour &) = default;
};

// One note of a MIDI clip. `start` counts from the start of the clip's content.
// Notes carry no channel.
struct Note {
  NoteId id;
  std::uint8_t pitch{60};
  TickPosition start;
  TickDuration length{ticksPerQuarter};
  std::uint8_t velocity{100};

  friend bool operator==(const Note &, const Note &) = default;
};

// Which stream of controller data an envelope inside a MIDI clip holds.
enum class ControllerKind : std::uint8_t {
  PitchBend,
  ChannelPressure,
  ControlChange,
};

// One recorded controller value. Pitch bend runs from -8192 to 8191 with rest
// at 0; the other kinds run from 0 to 127.
struct ControllerPoint {
  TickPosition time;
  std::int32_t value{0};

  friend bool operator==(const ControllerPoint &,
                         const ControllerPoint &) = default;
};

// The recorded values of one controller inside a MIDI clip, in time order.
// `controller` is the controller number for `ControlChange` and zero for the
// other kinds.
struct ControllerEnvelope {
  ControllerKind kind{ControllerKind::ControlChange};
  std::uint8_t controller{0};
  std::vector<ControllerPoint> points;

  friend bool operator==(const ControllerEnvelope &,
                         const ControllerEnvelope &) = default;
};

// What a MIDI clip holds. Notes are kept sorted by start, then pitch;
// controller envelopes by kind, then controller number.
struct MidiContent {
  std::vector<Note> notes;
  std::vector<ControllerEnvelope> controllers;

  friend bool operator==(const MidiContent &, const MidiContent &) = default;
};

// A pointer from the project to an audio file, with enough about the file to
// notice that it has gone missing or been replaced.
struct MediaReference {
  MediaId id;
  // Relative when the file is inside the project folder.
  std::string path;
  std::uint64_t sizeBytes{0};
  // Hexadecimal content hash; the model only stores and compares it.
  std::string contentHash;
  std::uint32_t sampleRate{48'000};

  friend bool operator==(const MediaReference &,
                         const MediaReference &) = default;
};

// A region of an audio file, in that file's own samples: [start, end).
struct SampleRange {
  std::int64_t start{0};
  std::int64_t end{0};

  friend bool operator==(const SampleRange &, const SampleRange &) = default;
};

// Pins a sample of the audio file to a position in the clip's content.
struct WarpMarker {
  std::int64_t sample{0};
  TickPosition tick;

  friend bool operator==(const WarpMarker &, const WarpMarker &) = default;
};

// Where a clip's source tempo came from.
enum class SourceTempoOrigin : std::uint8_t { File, Detected, Guessed, Manual };

// How an audio clip's material relates to musical time: at least two markers,
// increasing in both samples and ticks. The clip's "source tempo" is a view of
// these.
struct WarpMap {
  std::vector<WarpMarker> markers;
  SourceTempoOrigin origin{SourceTempoOrigin::Guessed};

  friend bool operator==(const WarpMap &, const WarpMap &) = default;
};

// How an audio clip that follows the tempo changes speed.
enum class StretchMode : std::uint8_t { Stretch, Repitch };

// What an audio clip holds: a region of one media reference and how it is
// played.
struct AudioContent {
  MediaId media;
  SampleRange region;
  WarpMap warp;
  double gainDb{0.0};
  bool followTempo{true};
  StretchMode stretchMode{StretchMode::Stretch};
  std::int32_t transposeCents{0};

  friend bool operator==(const AudioContent &, const AudioContent &) = default;
};

// A clip's own loop: when enabled, playback repeats [start, start + length) of
// the content.
struct LoopSettings {
  bool enabled{false};
  TickPosition start;
  TickDuration length{ticksPerWholeNote};

  friend bool operator==(const LoopSettings &, const LoopSettings &) = default;
};

// Where a clip sits and which part of its content it shows. `position` is the
// place on the arrangement and is always zero for a clip in a slot;
// `contentStart` is the content position that plays first.
struct ClipExtent {
  TickPosition position;
  TickDuration length{ticksPerWholeNote};
  TickPosition contentStart;

  friend bool operator==(const ClipExtent &, const ClipExtent &) = default;
};

using ClipContent = std::variant<MidiContent, AudioContent>;

// A bounded piece of musical material, living in one slot or in one track's
// arrangement.
struct Clip {
  ClipId id;
  std::string name;
  Colour colour;
  ClipExtent extent;
  LoopSettings loop;
  // Overrides the project's launch quantisation when set.
  std::optional<LaunchQuantisation> launchQuantisation;
  ClipContent content;

  [[nodiscard]] TickPosition end() const {
    return extent.position + extent.length;
  }

  friend bool operator==(const Clip &, const Clip &) = default;
};

} // namespace lina::model

#endif // LINA_MODEL_CLIP_HPP
