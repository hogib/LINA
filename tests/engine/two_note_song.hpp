// SPDX-License-Identifier: GPL-3.0-or-later
//
// The small edit most engine tests play: one track with the test instrument
// and one MIDI clip with two notes, at a tempo and sample rate where every
// note edge falls exactly on a sample frame.
#ifndef LINA_TESTS_ENGINE_TWO_NOTE_SONG_HPP
#define LINA_TESTS_ENGINE_TWO_NOTE_SONG_HPP

#include "lina/engine/testing/edit_building.hpp"
#include "reference_signal.hpp"

#include <array>
#include <cstdint>

namespace lina::engine::testing {

/// 120 quarter notes per minute at 48 kHz: one quarter note is 24000 frames.
inline constexpr double songSampleRate = 48000.0;
inline constexpr std::int64_t songFramesPerBeat = 24000;
inline constexpr std::int64_t songFrameCount = 3 * songFramesPerBeat;

/// Adds the track to the edit and returns it. The clip starts on beat 1.
/// A4 at full velocity sounds from beat 1 for half a beat; C5 at velocity 64
/// sounds from beat 2 for a quarter of a beat.
inline tracktion::AudioTrack &addTwoNoteSong(Engine &engine) {
  setTempo(engine, 120.0);
  auto &track = addTrack(engine);
  addToneInstrument(track);
  const std::array notes{
      Note{.pitch = 69, .startBeat = 0.0, .lengthBeats = 0.5, .velocity = 127},
      Note{.pitch = 72, .startBeat = 1.0, .lengthBeats = 0.25, .velocity = 64},
  };
  addMidiClip(track, 1.0, 4.0, notes);
  return track;
}

/// The two notes as they must sound at 48 kHz.
inline std::array<SoundingNote, 2> twoNoteSongNotes() {
  return {
      SoundingNote{
          .pitch = 69,
          .velocity = 127,
          .onFrame = 24000,
          .offFrame = releaseFrame(36000, songSampleRate),
      },
      SoundingNote{
          .pitch = 72,
          .velocity = 64,
          .onFrame = 48000,
          .offFrame = releaseFrame(54000, songSampleRate),
      },
  };
}

} // namespace lina::engine::testing

#endif // LINA_TESTS_ENGINE_TWO_NOTE_SONG_HPP
