// SPDX-License-Identifier: GPL-3.0-or-later
// Document model for the spike: plain structs, no Qt types.
// Views read `const Project&` and emit `Command`s; `Project::apply` is the only
// mutation path.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace lina {

using Tick = std::int64_t;
using ClipId = std::uint32_t;
using NoteId = std::uint32_t;

inline constexpr Tick PPQ = 960;
inline constexpr Tick BAR = PPQ * 4;
inline constexpr double SAMPLE_RATE = 48'000.0;
inline constexpr double TEMPO_BPM = 120.0;
/// Fixed tempo, so ticks map linearly to samples: 48000*60/(120*960) = 25.
inline constexpr double SAMPLES_PER_TICK =
    SAMPLE_RATE * 60.0 / (TEMPO_BPM * static_cast<double>(PPQ));
inline constexpr Tick MIN_CLIP_LEN = PPQ / 4;
inline constexpr Tick MIN_NOTE_LEN = PPQ / 8;

// ---------------------------------------------------------------- rng

/// splitmix64; deterministic, no dependency. Same generator as the egui spike.
class Rng {
public:
  explicit Rng(std::uint64_t seed) : s_(seed) {}
  std::uint64_t next();
  std::uint64_t below(std::uint64_t n) { return next() % n; }
  float unit() {
    return static_cast<float>(next() >> 40) / static_cast<float>(1u << 24);
  }
  template <typename T, std::size_t N> T pick(const T (&xs)[N]) {
    return xs[below(N)];
  }

private:
  std::uint64_t s_;
};

// ---------------------------------------------------------------- audio peaks

struct Peak {
  float lo, hi;
};

/// Min/max peak mipmap of one synthetic audio file. The samples are discarded.
class AudioSource {
public:
  static AudioSource synth(Rng &rng, double seconds);

  /// Coarsest level whose entries are no wider than `samplesPerPx`.
  int levelFor(double samplesPerPx) const;
  /// Min/max over the sample range [s0, s1) at `level`. The source loops.
  Peak peak(int level, double s0, double s1) const;

  std::size_t lenSamples = 0;

private:
  /// levels_[l][i] = min/max over samples [i*16*2^l, (i+1)*16*2^l)
  std::vector<std::vector<Peak>> levels_;
};

// ---------------------------------------------------------------- arrangement

struct Clip {
  ClipId id;
  std::size_t source;
  Tick start;
  Tick len;
  Tick offset; ///< Offset into the source, in ticks.
  std::string name;
  Tick end() const { return start + len; }
};

struct Track {
  std::string name;
  float hue = 0;
  bool mute = false;
  bool solo = false;
  std::vector<Clip> clips; ///< Sorted by start. Overlaps are allowed.
  Tick maxLen = 0;

  void reindex();
  /// Superset of the clips intersecting [t0, t1); the caller checks `end() >
  /// t0`.
  std::span<const Clip> clipsIn(Tick t0, Tick t1) const;
};

// ---------------------------------------------------------------- midi

struct Note {
  NoteId id;
  Tick start;
  Tick len;
  std::uint8_t pitch;
  std::uint8_t vel;
  Tick end() const { return start + len; }
};

struct MidiClip {
  Tick len = 0;
  std::vector<Note> notes; ///< Sorted by start.
  Tick maxLen = 0;

  void reindex();
  /// Superset of the notes intersecting [t0, t1); the caller checks `end() >
  /// t0`.
  std::span<const Note> notesIn(Tick t0, Tick t1) const;
};

struct Transport {
  bool playing = false;
  double pos = 0.0;
};

// ---------------------------------------------------------------- commands

struct MoveClip {
  ClipId id;
  std::size_t newTrack;
  Tick newStart;
};
struct ResizeClip {
  ClipId id;
  Tick newStart;
  Tick newLen;
};
struct DeleteClip {
  ClipId id;
};
struct ToggleMute {
  std::size_t track;
};
struct ToggleSolo {
  std::size_t track;
};
struct MoveNotes {
  std::vector<NoteId> ids;
  Tick dt;
  int dpitch;
};
struct ResizeNotes {
  std::vector<NoteId> ids;
  Tick dlen;
};
struct SetVelocities {
  std::vector<std::pair<NoteId, std::uint8_t>> changes;
};
struct DeleteNotes {
  std::vector<NoteId> ids;
};
struct SetPlayhead {
  Tick tick;
};
struct TogglePlay {};

using Command = std::variant<MoveClip, ResizeClip, DeleteClip, ToggleMute,
                             ToggleSolo, MoveNotes, ResizeNotes, SetVelocities,
                             DeleteNotes, SetPlayhead, TogglePlay>;

/// Short description for a status bar (avoids dumping 10k ids).
std::string summary(const Command &cmd);

// ---------------------------------------------------------------- project

struct Project {
  Tick len = 0;
  std::vector<AudioSource> sources;
  std::vector<Track> tracks;
  MidiClip midi;
  Transport transport;

  static Project synthetic(std::uint64_t seed);

  struct ClipRef {
    std::size_t track;
    const Clip *clip;
  };
  std::size_t clipCount() const;
  std::optional<ClipRef> findClip(ClipId id) const;
  void advanceTransport(double dtSeconds);

  /// The single place where the document changes.
  void apply(const Command &cmd);
};

/// Grid step for a zoom level: the finest musical division at least `minPx`
/// wide.
Tick gridStep(double pxPerTick, double minPx);
Tick snap(double tick, Tick step);

} // namespace lina
