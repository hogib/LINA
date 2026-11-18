// SPDX-License-Identifier: GPL-3.0-or-later
//
// Offline rendering, to a buffer and to a WAV file. A render is defined as
// what block-driven playback from the same frame produces, and is made by
// the same code; these tests hold it to that, and to leaving the manual
// device and the files around it in a defined state.

#include <catch2/catch_test_macros.hpp>

#include "lina/engine/testing/engine_fixture.hpp"
#include "reference_signal.hpp"
#include "two_note_song.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

using namespace lina::engine;
using namespace lina::engine::testing;

namespace {

RenderRequest songFrom(std::int64_t startFrame, std::int64_t frameCount,
                       int blockSize = 512) {
  return {
      .format = {.sampleRate = songSampleRate, .blockSize = blockSize},
      .startFrame = startFrame,
      .frameCount = frameCount,
  };
}

RenderRequest wholeSong(int blockSize = 512) {
  return songFrom(0, songFrameCount, blockSize);
}

/// What block-driven playback from `startFrame` gives on a device of the
/// song's sample rate.
AudioBuffer playFrom(Engine &engine, std::int64_t startFrame,
                     std::int64_t frameCount, int blockSize = 512) {
  engine.openManualDevice(
      {.sampleRate = songSampleRate, .blockSize = blockSize});
  engine.play(startFrame);
  return engine.process(frameCount);
}

std::string contentOf(const std::filesystem::path &file) {
  std::ifstream stream(file, std::ios::binary);
  return {std::istreambuf_iterator<char>(stream),
          std::istreambuf_iterator<char>()};
}

std::vector<std::string> namesIn(const std::filesystem::path &folder) {
  std::vector<std::string> names;
  for (const auto &entry : std::filesystem::directory_iterator(folder)) {
    names.push_back(entry.path().filename().string());
  }
  std::ranges::sort(names);
  return names;
}

/// The samples of a WAV file, with what its header says about them.
struct WavFile {
  double sampleRate = 0.0;
  unsigned int bitsPerSample = 0;
  bool isFloat = false;
  std::vector<std::vector<float>> channels;
};

WavFile readWav(const std::filesystem::path &file) {
  juce::WavAudioFormat wav;
  // The reader takes ownership of the stream it is given, which is JUCE's
  // idiom; both are owned by smart pointers from here on.
  auto stream = std::make_unique<juce::FileInputStream>(
      juce::File(juce::String(file.string())));
  REQUIRE(stream->openedOk());
  const std::unique_ptr<juce::AudioFormatReader> reader(
      wav.createReaderFor(stream.release(), true));
  REQUIRE(reader != nullptr);

  const auto frameCount = static_cast<int>(reader->lengthInSamples);
  juce::AudioBuffer<float> samples(static_cast<int>(reader->numChannels),
                                   frameCount);
  REQUIRE(reader->read(&samples, 0, frameCount, 0, true, true));
  WavFile result{
      .sampleRate = reader->sampleRate,
      .bitsPerSample = reader->bitsPerSample,
      .isFloat = reader->usesFloatingPointData,
      .channels = {},
  };
  for (int channel = 0; channel < samples.getNumChannels(); ++channel) {
    const std::span data(samples.getReadPointer(channel),
                         static_cast<std::size_t>(frameCount));
    result.channels.emplace_back(data.begin(), data.end());
  }
  return result;
}

} // namespace

TEST_CASE("an offline render holds the expected samples at the expected "
          "frames",
          "[engine][render]") {
  EngineFixture fixture("render-expected-samples");
  addTwoNoteSong(fixture.engine);

  // No manual device was ever opened: rendering needs none, and leaves none.
  const auto audio = fixture.engine.renderOffline(wholeSong());
  CHECK(audio.sampleRate == songSampleRate);
  REQUIRE(audio.channels.size() == 2);
  REQUIRE(audio.frameCount() == static_cast<std::size_t>(songFrameCount));

  const auto expected =
      referenceSignal(songFrameCount, songSampleRate, twoNoteSongNotes());
  // Tolerance: formulaTolerance, 1e-6 of full scale.
  CHECK(maxDifference(audio.channels.at(0), expected) <= formulaTolerance);
  CHECK(maxDifference(audio.channels.at(1), expected) <= formulaTolerance);
  CHECK(firstSoundingFrame(audio.channels.at(0)) == 24000);
  CHECK(audio.channels.at(0).at(24000) == 1.0F);
  CHECK_FALSE(fixture.engine.hasManualDevice());
  CHECK_FALSE(fixture.engine.isPlaying());
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("an offline render equals block-driven playback from the same "
          "frame",
          "[engine][render]") {
  EngineFixture fixture("render-equals-playback");
  auto &track = addTwoNoteSong(fixture.engine);
  // A delay of a tenth of a second at unity gain: what the first note leaves
  // in it sounds until frame 40794, long after the note has ended.
  addDelayGainEffect(track, 4800, 1.0F);

  const auto whole = playFrom(fixture.engine, 0, songFrameCount);
  REQUIRE(firstSoundingFrame(whole.channels.at(0)) == 28800);
  REQUIRE(whole.channels.at(0).at(28800) == 1.0F);

  // Tolerance: none, here and below. Rendered with the block size of the
  // playback and with another one, the stream is the same bit for bit.
  CHECK(identical(fixture.engine.renderOffline(wholeSong(512)), whole));
  CHECK(identical(fixture.engine.renderOffline(wholeSong(64)), whole));

  {
    INFO("from a start in silence");
    const auto rendered = fixture.engine.renderOffline(songFrom(20000, 30000));
    CHECK(firstSoundingFrame(rendered.channels.at(0)) == 8800);
    CHECK(identical(rendered, playFrom(fixture.engine, 20000, 30000)));
  }

  {
    INFO("from starts inside a held note");
    // The first note is held from 24000 to 35994. Neither start is a whole
    // number of periods of the note into it.
    for (const std::int64_t start :
         {std::int64_t{30010}, std::int64_t{35900}}) {
      CAPTURE(start);
      const auto rendered =
          fixture.engine.renderOffline(songFrom(start, 20000));
      const auto played = playFrom(fixture.engine, start, 20000);
      CHECK(identical(rendered, played));
      // The engine starts the held note again, 0.0001 s into the stream, and
      // the instrument plays it from the beginning of its waveform. Through
      // the delay that is frame 4805.
      CHECK(firstSoundingFrame(rendered.channels.at(0)) == 4805);
      CHECK(rendered.channels.at(0).at(4805) == 1.0F);
    }
  }

  {
    INFO("from a start inside the tail of the delay");
    // At 37000 no note is held, and a stream that has played from the
    // beginning is in the tail of the delay. The render is not a slice of
    // that stream: its delay starts empty, so it is silent until the second
    // note comes through the delay at 52800.
    REQUIRE(whole.channels.at(0).at(37000) != 0.0F);
    const auto rendered = fixture.engine.renderOffline(songFrom(37000, 20000));
    CHECK(firstSoundingFrame(rendered.channels.at(0)) == 52800 - 37000);
    CHECK(identical(rendered, playFrom(fixture.engine, 37000, 20000)));
  }
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("a render leaves the manual device as it was and playback stopped",
          "[engine][render]") {
  EngineFixture fixture("render-device-state");
  addTwoNoteSong(fixture.engine);
  const auto played = playFrom(fixture.engine, 0, songFrameCount);
  REQUIRE(firstSoundingFrame(played.channels.at(0)) == 24000);

  // Playing, in the middle of the first note, on a 48 kHz device.
  fixture.engine.play();
  REQUIRE(fixture.engine.process(30000).channels.at(0).at(29999) != 0.0F);
  REQUIRE(fixture.engine.isPlaying());

  // A render at another sample rate and block size. At 44.1 kHz a beat is
  // 22050 frames.
  const auto rendered = fixture.engine.renderOffline({
      .format = {.sampleRate = 44100.0, .blockSize = 256},
      .startFrame = 0,
      .frameCount = 30000,
  });
  CHECK(rendered.sampleRate == 44100.0);
  REQUIRE(rendered.frameCount() == 30000);
  CHECK(firstSoundingFrame(rendered.channels.at(0)) == 22050);
  CHECK(rendered.channels.at(0).at(22050) == 1.0F);

  // The device is the caller's own again, stopped and silent.
  CHECK(fixture.engine.hasManualDevice());
  CHECK_FALSE(fixture.engine.isPlaying());
  const auto idle = fixture.engine.process(1000);
  CHECK(idle.sampleRate == songSampleRate);
  CHECK(firstSoundingFrame(idle.channels.at(0)) == -1);

  // And it plays as it did before. Tolerance: none.
  fixture.engine.play();
  CHECK(identical(fixture.engine.process(songFrameCount), played));
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("two offline renders of the same edit are identical",
          "[engine][render]") {
  EngineFixture fixture("render-repeatable");
  addTwoNoteSong(fixture.engine);
  const auto first = fixture.engine.renderOffline(wholeSong());
  const auto second = fixture.engine.renderOffline(wholeSong());
  CHECK(firstSoundingFrame(first.channels.at(0)) == 24000);
  CHECK(first.channels.at(0).at(24000) == 1.0F);
  // Tolerance: none.
  CHECK(identical(first, second));
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("a render to a WAV file writes the same samples as 32-bit float",
          "[engine][render]") {
  EngineFixture fixture("render-wav");
  addTwoNoteSong(fixture.engine);
  // The folder does not exist yet.
  const auto folder = fixture.scratch / "out" / "deeper";
  const auto file = folder / "song.wav";

  // The song is longer than what a render to a file holds in memory at a
  // time, so this takes more than one piece.
  const auto written = fixture.engine.renderToWavFile(wholeSong(), file);
  REQUIRE(written.has_value());
  CHECK(namesIn(folder) == std::vector<std::string>{"song.wav"});
  CHECK_FALSE(fixture.engine.hasManualDevice());

  const auto wav = readWav(file);
  CHECK(wav.sampleRate == songSampleRate);
  CHECK(wav.bitsPerSample == 32);
  CHECK(wav.isFloat);
  // Tolerance: none. A float WAV file stores the samples as they are.
  const auto buffer = fixture.engine.renderOffline(wholeSong());
  CHECK(firstSoundingFrame(buffer.channels.at(0)) == 24000);
  REQUIRE(wav.channels.size() == 2);
  CHECK(maxDifference(wav.channels.at(0), buffer.channels.at(0)) == 0.0F);
  CHECK(maxDifference(wav.channels.at(1), buffer.channels.at(1)) == 0.0F);

  // A second render to the same name takes the place of the first.
  const auto part = songFrom(24000, 1000);
  REQUIRE(fixture.engine.renderToWavFile(part, file).has_value());
  CHECK(namesIn(folder) == std::vector<std::string>{"song.wav"});
  const auto replaced = readWav(file);
  REQUIRE(replaced.channels.size() == 2);
  REQUIRE(replaced.channels.at(0).size() == 1000);
  CHECK(replaced.channels.at(0).at(0) == 1.0F);
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("an edit that makes no sound renders as silence",
          "[engine][render]") {
  EngineFixture fixture("render-silence");
  const auto empty = fixture.engine.renderOffline({
      .format = {.sampleRate = 44100.0, .blockSize = 256},
      .startFrame = 0,
      .frameCount = 1000,
  });
  CHECK(empty.sampleRate == 44100.0);
  REQUIRE(empty.channels.size() == 2);
  REQUIRE(empty.frameCount() == 1000);
  CHECK(firstSoundingFrame(empty.channels.at(0)) == -1);
  CHECK(firstSoundingFrame(empty.channels.at(1)) == -1);
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("a render that fails keeps the file that was there",
          "[engine][render]") {
  EngineFixture fixture("render-failures");
  addTwoNoteSong(fixture.engine);
  const auto folder = fixture.scratch / "out";
  const auto file = folder / "song.wav";
  std::filesystem::create_directories(folder);
  const std::string earlier = "an earlier render";
  std::ofstream(file) << earlier;
  fixture.engine.openManualDevice({.sampleRate = 44100.0, .blockSize = 64});

  {
    INFO("the temporary file cannot be created");
    // Something that is not a file has the temporary file's name.
    const auto inTheWay = folder / "song.wav.lina-partial";
    std::filesystem::create_directories(inTheWay);
    const auto result = fixture.engine.renderToWavFile(wholeSong(), file);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().find("is in the way") != std::string::npos);
    CHECK(std::filesystem::is_directory(inTheWay));
    CHECK(contentOf(file) == earlier);
    std::filesystem::remove(inTheWay);
  }

  {
    INFO("the target is a folder");
    const auto result = fixture.engine.renderToWavFile(wholeSong(), folder);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().find("is a folder") != std::string::npos);
    CHECK(namesIn(folder) == std::vector<std::string>{"song.wav"});
    CHECK(std::filesystem::is_directory(folder));
    CHECK_FALSE(std::filesystem::exists(fixture.scratch / "out.lina-partial"));
    CHECK(contentOf(file) == earlier);
  }

  {
    INFO("the target lies below a file");
    const auto result =
        fixture.engine.renderToWavFile(wholeSong(), file / "below.wav");
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().find("cannot write") != std::string::npos);
    CHECK(namesIn(folder) == std::vector<std::string>{"song.wav"});
    CHECK(contentOf(file) == earlier);
  }

  {
    INFO("a temporary file left by a render that was cut short");
    // It is replaced, and the render succeeds.
    std::ofstream(folder / "song.wav.lina-partial") << "half a render";
    REQUIRE(fixture.engine.renderToWavFile(wholeSong(), file).has_value());
    CHECK(namesIn(folder) == std::vector<std::string>{"song.wav"});
    CHECK(readWav(file).channels.at(0).at(24000) == 1.0F);
  }

  // Whatever happened, the caller's device is back and nothing is playing.
  CHECK(fixture.engine.hasManualDevice());
  CHECK_FALSE(fixture.engine.isPlaying());
  CHECK(fixture.engine.process(16).sampleRate == 44100.0);
  CHECK(fixture.assertions.count() == 0);
}

TEST_CASE("a render request out of range is refused", "[engine][render]") {
  EngineFixture fixture("render-bad-requests");
  addTwoNoteSong(fixture.engine);
  const auto file = fixture.scratch / "out.wav";

  const auto refused = [&](const RenderRequest &request) {
    CHECK_THROWS_AS((void)fixture.engine.renderOffline(request),
                    std::invalid_argument);
    CHECK_THROWS_AS((void)fixture.engine.renderToWavFile(request, file),
                    std::invalid_argument);
  };
  refused(songFrom(0, 0));
  refused(songFrom(0, -1));
  refused(songFrom(-1, 100));
  refused(songFrom(0, Engine::maxRenderFrameCount + 1));
  refused(songFrom(0, 100, 0));
  auto noRate = songFrom(0, 100);
  noRate.format.sampleRate = 0.0;
  refused(noRate);

  // Nothing was written and no device was opened on the way.
  CHECK_FALSE(std::filesystem::exists(file));
  CHECK_FALSE(fixture.engine.hasManualDevice());
  CHECK(fixture.assertions.count() == 0);
}
