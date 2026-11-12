// SPDX-License-Identifier: GPL-3.0-or-later
#include "model.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <numbers>
#include <unordered_map>
#include <unordered_set>

namespace lina {

namespace {
/// Samples covered by one entry of the finest peak level.
constexpr std::size_t PEAK_BASE = 16;

template <class... Ts> struct Overloaded : Ts... {
  using Ts::operator()...;
};
} // namespace

std::uint64_t Rng::next() {
  s_ += 0x9E37'79B9'7F4A'7C15ull;
  std::uint64_t z = s_;
  z = (z ^ (z >> 30)) * 0xBF58'476D'1CE4'E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D0'49BB'1331'11EBull;
  return z ^ (z >> 31);
}

// ---------------------------------------------------------------- audio peaks

AudioSource AudioSource::synth(Rng &rng, double seconds) {
  const auto n = static_cast<std::size_t>(seconds * SAMPLE_RATE);
  const float tau = 2.0f * std::numbers::pi_v<float>;
  const float f0 = 40.0f + rng.unit() * 400.0f;
  const std::size_t hitEvery =
      (static_cast<std::size_t>(SAMPLE_RATE) / 8) * (1 + rng.below(6));
  const float decay = 0.9992f + rng.unit() * 0.0007f;
  const float noiseAmt = rng.unit() * 0.5f;
  float env = 0.0f;
  float phase = 0.0f;
  std::vector<Peak> base;
  base.reserve(n / PEAK_BASE + 1);
  float lo = std::numeric_limits<float>::max();
  float hi = std::numeric_limits<float>::lowest();
  for (std::size_t i = 0; i < n; ++i) {
    if (i % hitEvery == 0)
      env = 0.35f + rng.unit() * 0.65f;
    env *= decay;
    phase += tau * f0 / static_cast<float>(SAMPLE_RATE);
    if (phase > tau)
      phase -= tau;
    const float noise = (rng.unit() * 2.0f - 1.0f) * noiseAmt;
    const float s = std::clamp(
        env * (std::sin(phase) * (1.0f - noiseAmt) + noise), -1.0f, 1.0f);
    lo = std::min(lo, s);
    hi = std::max(hi, s);
    if (i % PEAK_BASE == PEAK_BASE - 1 || i == n - 1) {
      base.push_back({lo, hi});
      lo = std::numeric_limits<float>::max();
      hi = std::numeric_limits<float>::lowest();
    }
  }
  AudioSource src;
  src.lenSamples = n;
  src.levels_.push_back(std::move(base));
  while (src.levels_.back().size() > 1) {
    const auto &prev = src.levels_.back();
    std::vector<Peak> next;
    next.reserve(prev.size() / 2 + 1);
    for (std::size_t i = 0; i < prev.size(); i += 2) {
      const Peak a = prev[i];
      const Peak b = i + 1 < prev.size() ? prev[i + 1] : a;
      next.push_back({std::min(a.lo, b.lo), std::max(a.hi, b.hi)});
    }
    src.levels_.push_back(std::move(next));
  }
  return src;
}

int AudioSource::levelFor(double samplesPerPx) const {
  const double ratio =
      std::max(samplesPerPx / static_cast<double>(PEAK_BASE), 1.0);
  return std::min(static_cast<int>(std::floor(std::log2(ratio))),
                  static_cast<int>(levels_.size()) - 1);
}

Peak AudioSource::peak(int level, double s0, double s1) const {
  const auto &lv = levels_[static_cast<std::size_t>(level)];
  const double size = static_cast<double>(PEAK_BASE << level);
  const auto n = static_cast<std::int64_t>(lv.size());
  const auto i0 = static_cast<std::int64_t>(std::floor(s0 / size));
  const auto i1 = std::min(
      std::max(static_cast<std::int64_t>(std::ceil(s1 / size)), i0 + 1),
      i0 + 8);
  float lo = std::numeric_limits<float>::max();
  float hi = std::numeric_limits<float>::lowest();
  std::int64_t k = ((i0 % n) + n) % n;
  for (std::int64_t i = i0; i < i1; ++i) {
    const Peak p = lv[static_cast<std::size_t>(k)];
    lo = std::min(lo, p.lo);
    hi = std::max(hi, p.hi);
    if (++k == n)
      k = 0;
  }
  return {lo, hi};
}

// ---------------------------------------------------------------- containers

void Track::reindex() {
  std::ranges::sort(clips, [](const Clip &a, const Clip &b) {
    return std::pair(a.start, a.id) < std::pair(b.start, b.id);
  });
  maxLen = 0;
  for (const Clip &c : clips)
    maxLen = std::max(maxLen, c.len);
}

std::span<const Clip> Track::clipsIn(Tick t0, Tick t1) const {
  const auto a = std::ranges::partition_point(
      clips, [&](const Clip &c) { return c.start + maxLen <= t0; });
  const auto b = std::ranges::partition_point(
      clips, [&](const Clip &c) { return c.start < t1; });
  if (b <= a)
    return {};
  return {a, b};
}

void MidiClip::reindex() {
  std::ranges::sort(notes, [](const Note &a, const Note &b) {
    return std::pair(a.start, a.id) < std::pair(b.start, b.id);
  });
  maxLen = 0;
  for (const Note &n : notes)
    maxLen = std::max(maxLen, n.len);
}

std::span<const Note> MidiClip::notesIn(Tick t0, Tick t1) const {
  const auto a = std::ranges::partition_point(
      notes, [&](const Note &n) { return n.start + maxLen <= t0; });
  const auto b = std::ranges::partition_point(
      notes, [&](const Note &n) { return n.start < t1; });
  if (b <= a)
    return {};
  return {a, b};
}

// ---------------------------------------------------------------- commands

std::string summary(const Command &cmd) {
  return std::visit(
      Overloaded{
          [](const MoveClip &c) {
            return std::format("MoveClip {{ id: {}, track: {}, start: {} }}",
                               c.id, c.newTrack, c.newStart);
          },
          [](const ResizeClip &c) {
            return std::format("ResizeClip {{ id: {}, start: {}, len: {} }}",
                               c.id, c.newStart, c.newLen);
          },
          [](const DeleteClip &c) {
            return std::format("DeleteClip {{ id: {} }}", c.id);
          },
          [](const ToggleMute &c) {
            return std::format("ToggleMute {{ track: {} }}", c.track);
          },
          [](const ToggleSolo &c) {
            return std::format("ToggleSolo {{ track: {} }}", c.track);
          },
          [](const MoveNotes &c) {
            return std::format("MoveNotes {{ {} notes, dt: {}, dpitch: {} }}",
                               c.ids.size(), c.dt, c.dpitch);
          },
          [](const ResizeNotes &c) {
            return std::format("ResizeNotes {{ {} notes, dlen: {} }}",
                               c.ids.size(), c.dlen);
          },
          [](const SetVelocities &c) {
            return std::format("SetVelocities {{ {} notes }}",
                               c.changes.size());
          },
          [](const DeleteNotes &c) {
            return std::format("DeleteNotes {{ {} notes }}", c.ids.size());
          },
          [](const SetPlayhead &c) {
            return std::format("SetPlayhead {{ tick: {} }}", c.tick);
          },
          [](const TogglePlay &) { return std::string("TogglePlay"); },
      },
      cmd);
}

// ---------------------------------------------------------------- project

Project Project::synthetic(std::uint64_t seed) {
  constexpr std::size_t TRACKS = 64;
  constexpr std::size_t CLIPS = 10'000;
  constexpr std::size_t NOTES = 10'000;
  Rng rng(seed);
  Project p;

  for (int i = 0; i < 16; ++i)
    p.sources.push_back(AudioSource::synth(rng, 8.0));

  const char *const kinds[] = {"Kick", "Snare", "Hats", "Bass",
                               "Pad",  "Lead",  "Vox",  "FX"};
  ClipId nextId = 1;
  p.tracks.reserve(TRACKS);
  for (std::size_t t = 0; t < TRACKS; ++t) {
    const std::size_t n = CLIPS / TRACKS + (t < CLIPS % TRACKS ? 1 : 0);
    const char *kind = kinds[t % std::size(kinds)];
    Track track;
    track.clips.reserve(n);
    const Tick starts[] = {0, 0, BAR / 2, BAR};
    Tick pos = rng.pick(starts);
    for (std::size_t i = 0; i < n; ++i) {
      const Tick lens[] = {BAR / 2, BAR, BAR, BAR * 2, BAR * 2, BAR * 4};
      const Tick offsets[] = {0, 0, PPQ, BAR};
      const Tick gaps[] = {0, 0, 0, BAR / 2, BAR};
      const Tick len = rng.pick(lens);
      const std::size_t source = rng.below(p.sources.size());
      const Tick offset = rng.pick(offsets);
      track.clips.push_back({nextId, source, pos, len, offset,
                             std::format("{} {:05}", kind, nextId)});
      ++nextId;
      pos += len + rng.pick(gaps);
    }
    track.name = std::format("{:02} {}", t + 1, kind);
    track.hue = std::fmod(static_cast<float>(t) * 0.137f, 1.0f);
    track.reindex();
    p.tracks.push_back(std::move(track));
  }
  Tick end = BAR;
  for (const Track &t : p.tracks)
    if (!t.clips.empty())
      end = std::max(end, t.clips.back().end());
  p.len = (end + BAR - 1) / BAR * BAR;

  p.midi.len = 256 * BAR;
  const auto steps = static_cast<std::uint64_t>(p.midi.len / (PPQ / 4));
  p.midi.notes.reserve(NOTES);
  int voicePitch[] = {48, 60, 67, 72};
  for (std::size_t i = 0; i < NOTES; ++i) {
    const std::size_t v = i % std::size(voicePitch);
    voicePitch[v] =
        std::clamp(voicePitch[v] + static_cast<int>(rng.below(9)) - 4, 30, 100);
    const Tick lens[] = {PPQ / 8, PPQ / 4, PPQ / 4, PPQ / 2, PPQ, PPQ * 2};
    const Tick start = static_cast<Tick>(rng.below(steps)) * (PPQ / 4);
    const Tick len = rng.pick(lens);
    const auto vel = static_cast<std::uint8_t>(30 + rng.below(98));
    p.midi.notes.push_back({static_cast<NoteId>(i + 1), start, len,
                            static_cast<std::uint8_t>(voicePitch[v]), vel});
  }
  p.midi.reindex();
  p.transport = {true, 0.0};
  return p;
}

std::size_t Project::clipCount() const {
  std::size_t n = 0;
  for (const Track &t : tracks)
    n += t.clips.size();
  return n;
}

std::optional<Project::ClipRef> Project::findClip(ClipId id) const {
  for (std::size_t ti = 0; ti < tracks.size(); ++ti)
    for (const Clip &c : tracks[ti].clips)
      if (c.id == id)
        return ClipRef{ti, &c};
  return std::nullopt;
}

void Project::advanceTransport(double dtSeconds) {
  if (!transport.playing)
    return;
  transport.pos += dtSeconds * TEMPO_BPM / 60.0 * static_cast<double>(PPQ);
  if (transport.pos >= static_cast<double>(len))
    transport.pos = 0.0;
}

void Project::apply(const Command &cmd) {
  const auto idSet = [](const std::vector<NoteId> &ids) {
    return std::unordered_set<NoteId>(ids.begin(), ids.end());
  };
  std::visit(
      Overloaded{
          [&](const MoveClip &c) {
            const auto ref = findClip(c.id);
            if (!ref)
              return;
            const std::size_t to = std::min(c.newTrack, tracks.size() - 1);
            auto &from = tracks[ref->track].clips;
            const auto it = from.begin() + (ref->clip - from.data());
            Clip clip = std::move(*it);
            from.erase(it);
            clip.start = std::max<Tick>(c.newStart, 0);
            tracks[ref->track].reindex();
            tracks[to].clips.push_back(std::move(clip));
            tracks[to].reindex();
          },
          [&](const ResizeClip &c) {
            const auto ref = findClip(c.id);
            if (!ref)
              return;
            Track &track = tracks[ref->track];
            Clip &clip = track.clips[static_cast<std::size_t>(
                ref->clip - track.clips.data())];
            const Tick newStart = std::max<Tick>(c.newStart, 0);
            clip.offset += newStart - clip.start; // keep the audio in place
            clip.start = newStart;
            clip.len = std::max(c.newLen, MIN_CLIP_LEN);
            track.reindex();
          },
          [&](const DeleteClip &c) {
            if (const auto ref = findClip(c.id)) {
              Track &track = tracks[ref->track];
              std::erase_if(track.clips,
                            [&](const Clip &x) { return x.id == c.id; });
              track.reindex();
            }
          },
          [&](const ToggleMute &c) {
            if (c.track < tracks.size())
              tracks[c.track].mute ^= true;
          },
          [&](const ToggleSolo &c) {
            if (c.track < tracks.size())
              tracks[c.track].solo ^= true;
          },
          [&](const MoveNotes &c) {
            const auto ids = idSet(c.ids);
            for (Note &n : midi.notes) {
              if (!ids.contains(n.id))
                continue;
              n.start = std::max<Tick>(n.start + c.dt, 0);
              n.pitch = static_cast<std::uint8_t>(
                  std::clamp(static_cast<int>(n.pitch) + c.dpitch, 0, 127));
            }
            midi.reindex();
          },
          [&](const ResizeNotes &c) {
            const auto ids = idSet(c.ids);
            for (Note &n : midi.notes)
              if (ids.contains(n.id))
                n.len = std::max(n.len + c.dlen, MIN_NOTE_LEN);
            midi.reindex();
          },
          [&](const SetVelocities &c) {
            const std::unordered_map<NoteId, std::uint8_t> map(
                c.changes.begin(), c.changes.end());
            for (Note &n : midi.notes)
              if (const auto it = map.find(n.id); it != map.end())
                n.vel = std::clamp<std::uint8_t>(it->second, 1, 127);
          },
          [&](const DeleteNotes &c) {
            const auto ids = idSet(c.ids);
            std::erase_if(midi.notes,
                          [&](const Note &n) { return ids.contains(n.id); });
            midi.reindex();
          },
          [&](const SetPlayhead &c) {
            transport.pos =
                static_cast<double>(std::clamp<Tick>(c.tick, 0, len));
          },
          [&](const TogglePlay &) { transport.playing ^= true; },
      },
      cmd);
}

Tick gridStep(double pxPerTick, double minPx) {
  constexpr Tick STEPS[] = {PPQ / 4, PPQ / 2, PPQ,      BAR,     BAR * 2,
                            BAR * 4, BAR * 8, BAR * 16, BAR * 64};
  for (const Tick s : STEPS)
    if (static_cast<double>(s) * pxPerTick >= minPx)
      return s;
  return BAR * 64;
}

Tick snap(double tick, Tick step) {
  return static_cast<Tick>(std::round(tick / static_cast<double>(step))) * step;
}

} // namespace lina
