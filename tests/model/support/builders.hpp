// SPDX-License-Identifier: GPL-3.0-or-later
//
// Test support: short ways to build model values, and a small document with a
// known layout.
#ifndef LINA_MODEL_TESTS_SUPPORT_BUILDERS_HPP
#define LINA_MODEL_TESTS_SUPPORT_BUILDERS_HPP

#include "lina/model/document.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace lina::model::testing {

// Lengths in quarter notes, for readable positions in tests.
constexpr TickPosition beat(std::int64_t quarters) {
  return {quarters * ticksPerQuarter};
}
constexpr TickDuration beats(std::int64_t quarters) {
  return {quarters * ticksPerQuarter};
}

// A note with no identity yet.
Note makeNote(std::uint8_t pitch, TickPosition start, TickDuration length,
              std::uint8_t velocity = 100);

// Clips with no identity yet.
Clip makeMidiClip(TickPosition position, TickDuration length,
                  std::vector<Note> notes = {});
Clip makeAudioClip(MediaId media, TickPosition position, TickDuration length);

MediaReference makeMedia(std::string path);
Device makeEffect(std::string name);
Device makeInstrument(std::string name);

// A document with one audio track, one MIDI track, one return track, two scenes
// and one media reference, built through commands.
struct Fixture {
  Document document;
  MediaId media;
  SceneId sceneA;
  SceneId sceneB;
  TrackId audio;
  TrackId midi;
  TrackId returnTrack;

  [[nodiscard]] const Project &project() const { return document.project(); }
  // The slot of a track for a scene.
  [[nodiscard]] SlotId slot(TrackId track, SceneId scene) const;
};

Fixture makeFixture();

} // namespace lina::model::testing

#endif // LINA_MODEL_TESTS_SUPPORT_BUILDERS_HPP
