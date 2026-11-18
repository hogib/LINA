// SPDX-License-Identifier: GPL-3.0-or-later
//
// `validate` against projects damaged by hand, one rule at a time.
#include "support/builders.hpp"
#include "support/checks.hpp"

#include "lina/model/document.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace lina::model::testing {

namespace {

// A valid project with something of everything:
//   - tracks[0], audio: slot A holds an audio clip; two effects; envelopes on
//     volume, on the send and on a parameter of the first effect.
//   - tracks[1], MIDI: two arrangement clips, the first with three notes and
//     controller data; an instrument and an effect.
//   - returns[0] and the master: one effect each.
Project richProject() {
  Fixture fixture = makeFixture();
  Document &document = fixture.document;
  const auto perform = [&document](const Command &command) {
    return document.perform("build", command).has_value();
  };

  Clip first = makeMidiClip(beat(0), beats(4),
                            {
                                makeNote(60, beat(0), beats(1)),
                                makeNote(60, beat(1), beats(1)),
                                makeNote(64, beat(0), beats(2)),
                            });
  auto &midi = std::get<MidiContent>(first.content);
  midi.controllers = {
      {
          .kind = ControllerKind::PitchBend,
          .controller = 0,
          .points =
              {
                  {.time = beat(0), .value = -100},
                  {.time = beat(1), .value = 0},
              },
      },
      {
          .kind = ControllerKind::ControlChange,
          .controller = 1,
          .points = {{.time = beat(0), .value = 64}},
      },
      {
          .kind = ControllerKind::ControlChange,
          .controller = 64,
          .points = {{.time = beat(2), .value = 127}},
      },
  };
  bool built =
      perform(AddClip{.container = fixture.midi, .clip = std::move(first)}) &&
      perform(AddClip{
          .container = fixture.midi,
          .clip = makeMidiClip(beat(8), beats(4)),
      }) &&
      perform(AddClip{
          .container = fixture.slot(fixture.audio, fixture.sceneA),
          .clip = makeAudioClip(fixture.media, beat(0), beats(4)),
      });

  Device tuned = makeEffect("tuned");
  tuned.parameters = {
      {.id = "cutoff", .value = 0.5},
      {.id = "resonance", .value = 0.25},
  };
  built =
      built &&
      perform(AddDevice{.track = fixture.audio, .device = tuned, .index = 0}) &&
      perform(AddDevice{
          .track = fixture.audio,
          .device = makeEffect("second"),
          .index = 1,
      }) &&
      perform(AddDevice{
          .track = fixture.midi,
          .device = makeInstrument("synth"),
          .index = 0,
      }) &&
      perform(AddDevice{
          .track = fixture.midi,
          .device = makeEffect("delay"),
          .index = 1,
      }) &&
      perform(AddDevice{
          .track = fixture.returnTrack,
          .device = makeEffect("reverb"),
          .index = 0,
      }) &&
      perform(AddDevice{
          .track = fixture.project().master.id,
          .device = makeEffect("limiter"),
          .index = 0,
      });
  REQUIRE(built);

  const DeviceId tunedId = fixture.project().tracks.at(0).devices.at(0).id;
  const std::vector<AutomationPoint> ramp{
      {.time = beat(0), .value = 0.0},
      {.time = beat(4), .value = 1.0},
      {.time = beat(4), .value = 0.5},
  };
  const std::vector<AutomationPoint> fade{
      {.time = beat(0), .value = -12.0},
      {.time = beat(4), .value = 0.0},
  };
  built = perform(AddEnvelope{
              .track = fixture.audio,
              .envelope =
                  {
                      .id = EnvelopeId{0},
                      .target = MixerTarget{MixerParameter::Volume},
                      .points = ramp,
                  },
          }) &&
          perform(AddEnvelope{
              .track = fixture.audio,
              .envelope =
                  {
                      .id = EnvelopeId{0},
                      .target = SendTarget{fixture.returnTrack},
                      .points = fade,
                  },
          }) &&
          perform(AddEnvelope{
              .track = fixture.audio,
              .envelope =
                  {
                      .id = EnvelopeId{0},
                      .target =
                          DeviceParameterTarget{
                              .device = tunedId,
                              .parameter = "cutoff",
                          },
                      .points = ramp,
                  },
          }) &&
          perform(AddEnvelope{
              .track = fixture.returnTrack,
              .envelope =
                  {
                      .id = EnvelopeId{0},
                      .target = MixerTarget{MixerParameter::Pan},
                      .points = {{.time = beat(0), .value = -1.0}},
                  },
          });
  REQUIRE(built);
  return fixture.project();
}

// An enumeration value that is not one of the enumerators, as a careless cast
// of a number read from a file would give.
template <class Enumeration> Enumeration outside(int number) {
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): on purpose
  return static_cast<Enumeration>(number);
}

// One way of breaking a valid project, and the fault validation must then
// report.
struct Damage {
  Damage(std::string description, std::function<void(Project &)> change,
         Fault fault)
      : what(std::move(description)), apply(std::move(change)),
        expected(fault) {}

  std::string what;
  std::function<void(Project &)> apply;
  Fault expected;
};

Clip &slotClip(Project &project) {
  std::optional<Clip> &held = project.tracks.at(0).slots.at(0).clip;
  REQUIRE(held.has_value());
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access): required just above
  return *held;
}
AudioContent &audioOf(Project &project) {
  return std::get<AudioContent>(slotClip(project).content);
}
Clip &firstMidiClip(Project &project) {
  return project.tracks.at(1).arrangement.at(0);
}
MidiContent &midiOf(Project &project) {
  return std::get<MidiContent>(firstMidiClip(project).content);
}

void checkEach(const std::vector<Damage> &damages) {
  const Project valid = richProject();
  REQUIRE(validate(valid).empty());
  for (const Damage &damage : damages) {
    CAPTURE(damage.what);
    Project project = valid;
    damage.apply(project);
    const std::vector<Failure> failures = validate(project);
    REQUIRE(hasFault(failures, damage.expected));
    REQUIRE(!Document::fromProject(project).has_value());
  }
}

} // namespace

TEST_CASE("a valid project passes validation and becomes a document",
          "[model][validate]") {
  const Project project = richProject();
  REQUIRE(validate(project).empty());
  const auto document = Document::fromProject(project);
  REQUIRE(document.has_value());
  REQUIRE(document->project() == project);
  REQUIRE(!document->canUndo());

  REQUIRE(validate(makeEmptyProject()).empty());
}

TEST_CASE("validation reports every failure it finds, not only the first",
          "[model][validate]") {
  Project project = richProject();
  project.loop.length = {0};
  project.tracks.at(0).mixer.pan = 3.0;
  slotClip(project).extent.length = {0};
  const std::vector<Failure> failures = validate(project);
  REQUIRE(failures.size() == 3);
  REQUIRE(failures.at(0) ==
          Failure{.fault = Fault::ValueOutOfRange, .object = projectRef});
  const auto document = Document::fromProject(project);
  REQUIRE(!document.has_value());
  REQUIRE(document.error() == failures);
}

TEST_CASE("identities must be assigned, handed out and unique",
          "[model][validate]") {
  checkEach({
      {
          "scene shares a track's identity",
          [](Project &p) {
            p.scenes.at(0).id = SceneId{p.tracks.at(0).id.value};
          },
          Fault::IdInvalid,
      },
      {
          "identity not handed out yet",
          [](Project &p) { p.scenes.at(0).id = SceneId{p.nextId}; },
          Fault::IdInvalid,
      },
      {
          "unassigned identity",
          [](Project &p) { p.media.at(0).id = MediaId{}; },
          Fault::IdInvalid,
      },
      {
          "unassigned note identity",
          [](Project &p) { midiOf(p).notes.at(0).id = NoteId{}; },
          Fault::IdInvalid,
      },
  });
}

TEST_CASE("project-level values and track lists are checked",
          "[model][validate]") {
  checkEach({
      {
          "loop without length",
          [](Project &p) { p.loop.length = {0}; },
          Fault::ValueOutOfRange,
      },
      {
          "loop before zero",
          [](Project &p) { p.loop.start = {-1}; },
          Fault::ValueOutOfRange,
      },
      {
          "start marker before zero",
          [](Project &p) { p.startMarker = {-1}; },
          Fault::ValueOutOfRange,
      },
      {
          "start marker past the limit",
          [](Project &p) { p.startMarker = {maxTicks + 1}; },
          Fault::ValueOutOfRange,
      },
      {
          "media without path",
          [](Project &p) { p.media.at(0).path.clear(); },
          Fault::ValueOutOfRange,
      },
      {
          "media without sample rate",
          [](Project &p) { p.media.at(0).sampleRate = 0; },
          Fault::ValueOutOfRange,
      },
      {
          "return track among the tracks",
          [](Project &p) { p.tracks.at(0).kind = TrackKind::Return; },
          Fault::WrongTrackKind,
      },
      {
          "audio track among the returns",
          [](Project &p) { p.returns.at(0).kind = TrackKind::Audio; },
          Fault::WrongTrackKind,
      },
      {
          "master of another kind",
          [](Project &p) { p.master.kind = TrackKind::Return; },
          Fault::WrongTrackKind,
      },
  });
}

TEST_CASE("every track has exactly one slot per scene, in scene order",
          "[model][validate]") {
  checkEach({
      {
          "slot missing",
          [](Project &p) { p.tracks.at(1).slots.pop_back(); },
          Fault::SlotsMismatch,
      },
      {
          "slot too many",
          [](Project &p) {
            const Slot extra{
                .id = SlotId{p.nextId++},
                .scene = p.scenes.at(0).id,
                .clip = std::nullopt,
            };
            p.tracks.at(1).slots.push_back(extra);
          },
          Fault::SlotsMismatch,
      },
      {
          "slots out of scene order",
          [](Project &p) {
            std::swap(p.tracks.at(1).slots.at(0), p.tracks.at(1).slots.at(1));
          },
          Fault::SlotsMismatch,
      },
      {
          "return track with a slot",
          [](Project &p) {
            p.returns.at(0).slots.push_back({
                .id = SlotId{p.nextId++},
                .scene = p.scenes.at(0).id,
                .clip = std::nullopt,
            });
          },
          Fault::SlotsMismatch,
      },
      {
          "master with an arrangement clip",
          [](Project &p) {
            Clip clip = makeMidiClip(beat(0), beats(1));
            clip.id = ClipId{p.nextId++};
            p.master.arrangement.push_back(clip);
          },
          Fault::WrongTrackKind,
      },
  });
}

TEST_CASE("mixer strips are checked against the track kind and the returns",
          "[model][validate]") {
  checkEach({
      {
          "send missing",
          [](Project &p) { p.tracks.at(0).mixer.sends.clear(); },
          Fault::SendsMismatch,
      },
      {
          "send to something else",
          [](Project &p) {
            p.tracks.at(0).mixer.sends.at(0).target = p.tracks.at(1).id;
          },
          Fault::SendsMismatch,
      },
      {
          "send too loud",
          [](Project &p) { p.tracks.at(0).mixer.sends.at(0).levelDb = 0.5; },
          Fault::ValueOutOfRange,
      },
      {
          "return track with a send",
          [](Project &p) {
            p.returns.at(0).mixer.sends.push_back(
                {.target = p.returns.at(0).id, .levelDb = silenceDb});
          },
          Fault::SendsMismatch,
      },
      {
          "volume above the top",
          [](Project &p) { p.tracks.at(0).mixer.volumeDb = 6.5; },
          Fault::ValueOutOfRange,
      },
      {
          "volume below silence",
          [](Project &p) { p.master.mixer.volumeDb = -97.0; },
          Fault::ValueOutOfRange,
      },
      {
          "volume not a number",
          [](Project &p) {
            p.tracks.at(0).mixer.volumeDb =
                std::numeric_limits<double>::quiet_NaN();
          },
          Fault::ValueOutOfRange,
      },
      {
          "pan past the right",
          [](Project &p) { p.returns.at(0).mixer.pan = 1.5; },
          Fault::ValueOutOfRange,
      },
      {
          "master panned",
          [](Project &p) { p.master.mixer.pan = 0.5; },
          Fault::WrongTrackKind,
      },
      {
          "master muted",
          [](Project &p) { p.master.mixer.mute = true; },
          Fault::WrongTrackKind,
      },
      {
          "master soloed",
          [](Project &p) { p.master.mixer.solo = true; },
          Fault::WrongTrackKind,
      },
      {
          "return track armed",
          [](Project &p) { p.returns.at(0).mixer.armed = true; },
          Fault::WrongTrackKind,
      },
      {
          "master monitoring",
          [](Project &p) { p.master.mixer.monitoring = Monitoring::On; },
          Fault::WrongTrackKind,
      },
      {
          "track colour out of range",
          [](Project &p) { p.tracks.at(0).colour.rgb = 0x1000000; },
          Fault::ValueOutOfRange,
      },
  });
}

TEST_CASE("clips are checked for extent, kind and placement",
          "[model][validate]") {
  checkEach({
      {
          "clip without length",
          [](Project &p) { firstMidiClip(p).extent.length = {0}; },
          Fault::ValueOutOfRange,
      },
      {
          "clip before zero",
          [](Project &p) { firstMidiClip(p).extent.position = {-1}; },
          Fault::ValueOutOfRange,
      },
      {
          "content start before zero",
          [](Project &p) { firstMidiClip(p).extent.contentStart = {-1}; },
          Fault::ValueOutOfRange,
      },
      {
          "slot clip with a position",
          [](Project &p) { slotClip(p).extent.position = beat(1); },
          Fault::ValueOutOfRange,
      },
      {
          "loop without length",
          [](Project &p) { firstMidiClip(p).loop.length = {0}; },
          Fault::ValueOutOfRange,
      },
      {
          "loop start before zero",
          [](Project &p) { firstMidiClip(p).loop.start = {-1}; },
          Fault::ValueOutOfRange,
      },
      {
          "clip colour out of range",
          [](Project &p) { slotClip(p).colour.rgb = 0x1000000; },
          Fault::ValueOutOfRange,
      },
      {
          "audio clip on a MIDI track",
          [](Project &p) {
            p.tracks.at(1).slots.at(0).clip = slotClip(p);
            p.tracks.at(1).slots.at(0).clip->id = ClipId{p.nextId++};
          },
          Fault::WrongTrackKind,
      },
      {
          "MIDI clip on an audio track",
          [](Project &p) {
            Clip clip = makeMidiClip(beat(0), beats(1));
            clip.id = ClipId{p.nextId++};
            p.tracks.at(0).arrangement.push_back(clip);
          },
          Fault::WrongTrackKind,
      },
      {
          "arrangement out of order",
          [](Project &p) {
            std::swap(p.tracks.at(1).arrangement.at(0),
                      p.tracks.at(1).arrangement.at(1));
          },
          Fault::OrderViolation,
      },
      {
          "arrangement clips overlap",
          [](Project &p) { firstMidiClip(p).extent.length = beats(9); },
          Fault::ClipOverlap,
      },
  });
}

TEST_CASE("arrangement clips may touch but an unstorable extent is not "
          "compared",
          "[model][validate]") {
  Project project = richProject();
  firstMidiClip(project).extent.length = beats(8);
  REQUIRE(validate(project).empty());

  // An extent that cannot be stored is reported once, not also as an overlap.
  firstMidiClip(project).extent.length = {maxTicks + 1};
  const std::vector<Failure> failures = validate(project);
  REQUIRE(failures.size() == 1);
  REQUIRE(failures.front().fault == Fault::ValueOutOfRange);
}

TEST_CASE("notes and controller data are checked", "[model][validate]") {
  checkEach({
      {
          "pitch above 127",
          [](Project &p) { midiOf(p).notes.at(2).pitch = 128; },
          Fault::ValueOutOfRange,
      },
      {
          "velocity zero",
          [](Project &p) { midiOf(p).notes.at(0).velocity = 0; },
          Fault::ValueOutOfRange,
      },
      {
          "velocity above 127",
          [](Project &p) { midiOf(p).notes.at(0).velocity = 128; },
          Fault::ValueOutOfRange,
      },
      {
          "note without length",
          [](Project &p) { midiOf(p).notes.at(0).length = {0}; },
          Fault::ValueOutOfRange,
      },
      {
          "note before zero",
          [](Project &p) { midiOf(p).notes.at(0).start = {-1}; },
          Fault::ValueOutOfRange,
      },
      {
          "notes out of order",
          [](Project &p) {
            std::swap(midiOf(p).notes.at(0), midiOf(p).notes.at(2));
          },
          Fault::OrderViolation,
      },
      {
          "same-pitch notes overlap",
          [](Project &p) { midiOf(p).notes.at(0).length = beats(2); },
          Fault::NoteOverlap,
      },
      {
          "controller number above 127",
          [](Project &p) { midiOf(p).controllers.at(2).controller = 128; },
          Fault::ValueOutOfRange,
      },
      {
          "pitch bend with a controller number",
          [](Project &p) { midiOf(p).controllers.at(0).controller = 1; },
          Fault::ValueOutOfRange,
      },
      {
          "two envelopes for one controller",
          [](Project &p) { midiOf(p).controllers.at(2).controller = 1; },
          Fault::OrderViolation,
      },
      {
          "controller envelopes out of order",
          [](Project &p) {
            std::swap(midiOf(p).controllers.at(0), midiOf(p).controllers.at(1));
          },
          Fault::OrderViolation,
      },
      {
          "controller value above 127",
          [](Project &p) {
            midiOf(p).controllers.at(1).points.at(0).value = 128;
          },
          Fault::ValueOutOfRange,
      },
      {
          "controller value below zero",
          [](Project &p) {
            midiOf(p).controllers.at(1).points.at(0).value = -1;
          },
          Fault::ValueOutOfRange,
      },
      {
          "pitch bend above range",
          [](Project &p) {
            midiOf(p).controllers.at(0).points.at(0).value = 8'192;
          },
          Fault::ValueOutOfRange,
      },
      {
          "pitch bend below range",
          [](Project &p) {
            midiOf(p).controllers.at(0).points.at(0).value = -8'193;
          },
          Fault::ValueOutOfRange,
      },
      {
          "controller point before zero",
          [](Project &p) {
            midiOf(p).controllers.at(0).points.at(0).time = {-1};
          },
          Fault::ValueOutOfRange,
      },
      {
          "controller points out of order",
          [](Project &p) {
            midiOf(p).controllers.at(0).points.at(0).time = beat(2);
          },
          Fault::OrderViolation,
      },
  });
}

TEST_CASE("notes of different pitches may overlap and same-pitch notes may "
          "touch",
          "[model][validate]") {
  Project project = richProject();
  // As built: pitch 64 spans both pitch-60 notes, which touch at beat 1.
  REQUIRE(validate(project).empty());
  // The extremes of pitch and velocity are allowed.
  midiOf(project).notes.at(2).pitch = 0;
  midiOf(project).notes.at(2).velocity = 127;
  midiOf(project).notes.at(1).pitch = 127;
  midiOf(project).notes.at(1).velocity = 1;
  REQUIRE(validate(project).empty());
}

TEST_CASE("audio content is checked", "[model][validate]") {
  checkEach({
      {
          "media reference unknown",
          [](Project &p) { audioOf(p).media = MediaId{p.nextId + 1}; },
          Fault::MediaMissing,
      },
      {
          "empty region",
          [](Project &p) { audioOf(p).region = {.start = 5, .end = 5}; },
          Fault::ValueOutOfRange,
      },
      {
          "region before the file",
          [](Project &p) { audioOf(p).region.start = -1; },
          Fault::ValueOutOfRange,
      },
      {
          "gain above the top",
          [](Project &p) { audioOf(p).gainDb = 24.5; },
          Fault::ValueOutOfRange,
      },
      {
          "gain infinite",
          [](Project &p) {
            audioOf(p).gainDb = -std::numeric_limits<double>::infinity();
          },
          Fault::ValueOutOfRange,
      },
      {
          "transposed too far up",
          [](Project &p) { audioOf(p).transposeCents = 4'801; },
          Fault::ValueOutOfRange,
      },
      {
          "transposed too far down",
          [](Project &p) { audioOf(p).transposeCents = -4'801; },
          Fault::ValueOutOfRange,
      },
      {
          "one warp marker",
          [](Project &p) { audioOf(p).warp.markers.pop_back(); },
          Fault::ValueOutOfRange,
      },
      {
          "warp marker before the file",
          [](Project &p) { audioOf(p).warp.markers.at(0).sample = -1; },
          Fault::ValueOutOfRange,
      },
      {
          "warp marker before zero",
          [](Project &p) { audioOf(p).warp.markers.at(0).tick = {-1}; },
          Fault::ValueOutOfRange,
      },
      {
          "warp samples not increasing",
          [](Project &p) { audioOf(p).warp.markers.at(1).sample = 0; },
          Fault::OrderViolation,
      },
      {
          "warp ticks not increasing",
          [](Project &p) { audioOf(p).warp.markers.at(1).tick = {0}; },
          Fault::OrderViolation,
      },
  });
}

TEST_CASE("device chains are checked", "[model][validate]") {
  checkEach({
      {
          "instrument on an audio track",
          [](Project &p) {
            p.tracks.at(0).devices.at(0).role = DeviceRole::Instrument;
          },
          Fault::DeviceChainOrder,
      },
      {
          "instrument on a return track",
          [](Project &p) {
            p.returns.at(0).devices.at(0).role = DeviceRole::Instrument;
          },
          Fault::DeviceChainOrder,
      },
      {
          "second instrument on a MIDI track",
          [](Project &p) {
            p.tracks.at(1).devices.at(1).role = DeviceRole::Instrument;
          },
          Fault::DeviceChainOrder,
      },
      {
          "parameter without identifier",
          [](Project &p) {
            p.tracks.at(0).devices.at(0).parameters.at(0).id.clear();
          },
          Fault::ValueOutOfRange,
      },
      {
          "parameter value above one",
          [](Project &p) {
            p.tracks.at(0).devices.at(0).parameters.at(0).value = 1.5;
          },
          Fault::ValueOutOfRange,
      },
      {
          "parameter value below zero",
          [](Project &p) {
            p.tracks.at(0).devices.at(0).parameters.at(0).value = -0.5;
          },
          Fault::ValueOutOfRange,
      },
      {
          "parameters out of order",
          [](Project &p) {
            auto &parameters = p.tracks.at(0).devices.at(0).parameters;
            std::swap(parameters.at(0), parameters.at(1));
          },
          Fault::OrderViolation,
      },
      {
          "parameter stored twice",
          [](Project &p) {
            p.tracks.at(0).devices.at(0).parameters.at(1).id = "cutoff";
          },
          Fault::OrderViolation,
      },
  });
}

TEST_CASE("automation envelopes are checked", "[model][validate]") {
  checkEach({
      {
          "device not on the track",
          [](Project &p) {
            p.tracks.at(0).automation.at(2).target = DeviceParameterTarget{
                .device = p.tracks.at(1).devices.at(0).id,
                .parameter = "cutoff",
            };
          },
          Fault::EnvelopeTarget,
      },
      {
          "parameter without identifier",
          [](Project &p) {
            p.tracks.at(0).automation.at(2).target = DeviceParameterTarget{
                .device = p.tracks.at(0).devices.at(0).id,
                .parameter = "",
            };
          },
          Fault::EnvelopeTarget,
      },
      {
          "send to something that is not a return track",
          [](Project &p) {
            p.tracks.at(0).automation.at(1).target =
                SendTarget{p.tracks.at(1).id};
          },
          Fault::EnvelopeTarget,
      },
      {
          "send automation on a return track",
          [](Project &p) {
            p.returns.at(0).automation.push_back({
                .id = EnvelopeId{p.nextId++},
                .target = SendTarget{p.returns.at(0).id},
                .points = {},
            });
          },
          Fault::EnvelopeTarget,
      },
      {
          "pan automation on the master",
          [](Project &p) {
            p.master.automation.push_back({
                .id = EnvelopeId{p.nextId++},
                .target = MixerTarget{MixerParameter::Pan},
                .points = {},
            });
          },
          Fault::EnvelopeTarget,
      },
      {
          "two envelopes for one parameter",
          [](Project &p) {
            p.tracks.at(0).automation.at(1).target =
                MixerTarget{MixerParameter::Volume};
          },
          Fault::EnvelopeTarget,
      },
      {
          "point value not finite",
          [](Project &p) {
            p.tracks.at(0).automation.at(0).points.at(0).value =
                std::numeric_limits<double>::infinity();
          },
          Fault::ValueOutOfRange,
      },
      {
          "point before zero",
          [](Project &p) {
            p.tracks.at(0).automation.at(0).points.at(0).time = {-1};
          },
          Fault::ValueOutOfRange,
      },
      {
          "points out of order",
          [](Project &p) {
            p.tracks.at(0).automation.at(0).points.at(0).time = beat(8);
          },
          Fault::OrderViolation,
      },
  });
}

TEST_CASE("the master and return tracks may have volume automation",
          "[model][validate]") {
  Project project = richProject();
  project.master.automation.push_back({
      .id = EnvelopeId{project.nextId++},
      .target = MixerTarget{MixerParameter::Volume},
      .points = {},
  });
  project.returns.at(0).automation.push_back({
      .id = EnvelopeId{project.nextId++},
      .target = MixerTarget{MixerParameter::Volume},
      .points = {{.time = beat(0), .value = maxVolumeDb}},
  });
  REQUIRE(validate(project).empty());
}

TEST_CASE("enumeration values outside their enumerators are reported",
          "[model][validate]") {
  checkEach({
      {
          "track kind",
          [](Project &p) { p.tracks.at(0).kind = outside<TrackKind>(9); },
          Fault::ValueOutOfRange,
      },
      {
          "return track kind",
          [](Project &p) { p.returns.at(0).kind = outside<TrackKind>(4); },
          Fault::ValueOutOfRange,
      },
      {
          "monitoring",
          [](Project &p) {
            p.tracks.at(0).mixer.monitoring = outside<Monitoring>(77);
          },
          Fault::ValueOutOfRange,
      },
      {
          "project launch quantisation",
          [](Project &p) {
            p.launchQuantisation = outside<LaunchQuantisation>(200);
          },
          Fault::ValueOutOfRange,
      },
      {
          "clip launch quantisation",
          [](Project &p) {
            slotClip(p).launchQuantisation = outside<LaunchQuantisation>(10);
          },
          Fault::ValueOutOfRange,
      },
      {
          "stretch mode",
          [](Project &p) { audioOf(p).stretchMode = outside<StretchMode>(2); },
          Fault::ValueOutOfRange,
      },
      {
          "source tempo origin",
          [](Project &p) {
            audioOf(p).warp.origin = outside<SourceTempoOrigin>(4);
          },
          Fault::ValueOutOfRange,
      },
      {
          "controller kind",
          [](Project &p) {
            midiOf(p).controllers.at(2).kind = outside<ControllerKind>(3);
          },
          Fault::ValueOutOfRange,
      },
      {
          "device role",
          [](Project &p) {
            p.tracks.at(0).devices.at(0).role = outside<DeviceRole>(2);
          },
          Fault::ValueOutOfRange,
      },
      {
          "automated mixer parameter",
          [](Project &p) {
            p.tracks.at(0).automation.at(0).target =
                MixerTarget{outside<MixerParameter>(2)};
          },
          Fault::ValueOutOfRange,
      },
  });
}

TEST_CASE("the last enumerator of each enumeration is accepted",
          "[model][validate]") {
  Project project = richProject();
  project.launchQuantisation = LaunchQuantisation::ThirtySecond;
  slotClip(project).launchQuantisation = LaunchQuantisation::ThirtySecond;
  project.tracks.at(0).mixer.monitoring = Monitoring::Off;
  audioOf(project).stretchMode = StretchMode::Repitch;
  audioOf(project).warp.origin = SourceTempoOrigin::Manual;
  REQUIRE(validate(project).empty());
}

TEST_CASE("track inputs are checked against the kind of track",
          "[model][validate]") {
  checkEach({
      {
          "MIDI device on an audio track",
          [](Project &p) {
            p.tracks.at(0).mixer.input = MidiDeviceInput{"keys"};
          },
          Fault::WrongInputKind,
      },
      {
          "all MIDI inputs on an audio track",
          [](Project &p) { p.tracks.at(0).mixer.input = AllMidiInputs{}; },
          Fault::WrongInputKind,
      },
      {
          "no input on a MIDI track",
          [](Project &p) { p.tracks.at(1).mixer.input = NoInput{}; },
          Fault::WrongInputKind,
      },
      {
          "audio input on a MIDI track",
          [](Project &p) { p.tracks.at(1).mixer.input = AudioInput{}; },
          Fault::WrongInputKind,
      },
      {
          "audio input on a return track",
          [](Project &p) { p.returns.at(0).mixer.input = AudioInput{}; },
          Fault::WrongInputKind,
      },
      {
          "MIDI input on the master",
          [](Project &p) { p.master.mixer.input = AllMidiInputs{}; },
          Fault::WrongInputKind,
      },
      {
          "MIDI device on the master",
          [](Project &p) { p.master.mixer.input = MidiDeviceInput{"keys"}; },
          Fault::WrongInputKind,
      },
      {
          "audio channel past the last",
          [](Project &p) {
            p.tracks.at(0).mixer.input =
                AudioInput{.channel = maxInputChannels, .stereo = false};
          },
          Fault::ValueOutOfRange,
      },
      {
          "stereo pair starting on the last channel",
          [](Project &p) {
            p.tracks.at(0).mixer.input =
                AudioInput{.channel = maxInputChannels - 1, .stereo = true};
          },
          Fault::ValueOutOfRange,
      },
      {
          "MIDI device without a name",
          [](Project &p) { p.tracks.at(1).mixer.input = MidiDeviceInput{""}; },
          Fault::ValueOutOfRange,
      },
  });

  Project project = richProject();
  REQUIRE(project.tracks.at(0).mixer.input == TrackInput{NoInput{}});
  REQUIRE(project.tracks.at(1).mixer.input == TrackInput{AllMidiInputs{}});
  project.tracks.at(0).mixer.input =
      AudioInput{.channel = maxInputChannels - 2, .stereo = true};
  project.tracks.at(1).mixer.input = MidiDeviceInput{"keys"};
  REQUIRE(validate(project).empty());
  project.tracks.at(0).mixer.input =
      AudioInput{.channel = maxInputChannels - 1, .stereo = false};
  REQUIRE(validate(project).empty());
}

TEST_CASE("everything with a start and a length must end inside the storable "
          "range",
          "[model][validate]") {
  checkEach({
      {
          "clip ends past the limit",
          [](Project &p) {
            p.tracks.at(1).arrangement.at(1).extent = {
                .position = {maxTicks},
                .length = {1},
                .contentStart = {0},
            };
          },
          Fault::ValueOutOfRange,
      },
      {
          "clip as long as the range, not at its start",
          [](Project &p) {
            p.tracks.at(1).arrangement.at(1).extent.length = {maxTicks};
          },
          Fault::ValueOutOfRange,
      },
      {
          "note ends past the limit",
          [](Project &p) { midiOf(p).notes.at(2).start = {maxTicks}; },
          Fault::ValueOutOfRange,
      },
      {
          "clip loop ends past the limit",
          [](Project &p) { firstMidiClip(p).loop.start = {maxTicks}; },
          Fault::ValueOutOfRange,
      },
      {
          "project loop ends past the limit",
          [](Project &p) {
            p.loop = {.enabled = true, .start = {1}, .length = {maxTicks}};
          },
          Fault::ValueOutOfRange,
      },
  });

  // Ending exactly on the limit is allowed.
  Project project = richProject();
  project.loop = {.enabled = true, .start = {0}, .length = {maxTicks}};
  project.startMarker = {maxTicks};
  project.tracks.at(1).arrangement.at(1).extent = {
      .position = {maxTicks - 10},
      .length = {10},
      .contentStart = {maxTicks},
  };
  firstMidiClip(project).loop = {
      .enabled = true,
      .start = {maxTicks - 1},
      .length = {1},
  };
  REQUIRE(validate(project).empty());
}

TEST_CASE("negative zero is not a stored value", "[model][validate]") {
  checkEach({
      {
          "pan",
          [](Project &p) { p.tracks.at(0).mixer.pan = -0.0; },
          Fault::ValueOutOfRange,
      },
      {
          "volume",
          [](Project &p) { p.master.mixer.volumeDb = -0.0; },
          Fault::ValueOutOfRange,
      },
      {
          "send level",
          [](Project &p) { p.tracks.at(0).mixer.sends.at(0).levelDb = -0.0; },
          Fault::ValueOutOfRange,
      },
      {
          "clip gain",
          [](Project &p) { audioOf(p).gainDb = -0.0; },
          Fault::ValueOutOfRange,
      },
      {
          "parameter value",
          [](Project &p) {
            p.tracks.at(0).devices.at(0).parameters.at(0).value = -0.0;
          },
          Fault::ValueOutOfRange,
      },
      {
          "automation point",
          [](Project &p) {
            p.tracks.at(0).automation.at(0).points.at(0).value = -0.0;
          },
          Fault::ValueOutOfRange,
      },
  });
}

TEST_CASE("envelopes are kept in identity order with values in the range of "
          "their target",
          "[model][validate]") {
  checkEach({
      {
          "envelopes out of identity order",
          [](Project &p) {
            std::swap(p.tracks.at(0).automation.at(0),
                      p.tracks.at(0).automation.at(1));
          },
          Fault::OrderViolation,
      },
      {
          "volume above the top",
          [](Project &p) {
            p.tracks.at(0).automation.at(0).points.at(1).value = 6.5;
          },
          Fault::ValueOutOfRange,
      },
      {
          "send above the top",
          [](Project &p) {
            p.tracks.at(0).automation.at(1).points.at(1).value = 0.5;
          },
          Fault::ValueOutOfRange,
      },
      {
          "plugin parameter above one",
          [](Project &p) {
            p.tracks.at(0).automation.at(2).points.at(1).value = 1.5;
          },
          Fault::ValueOutOfRange,
      },
      {
          "plugin parameter below zero",
          [](Project &p) {
            p.tracks.at(0).automation.at(2).points.at(0).value = -0.5;
          },
          Fault::ValueOutOfRange,
      },
      {
          "pan past the left",
          [](Project &p) {
            p.returns.at(0).automation.at(0).points.at(0).value = -1.5;
          },
          Fault::ValueOutOfRange,
      },
      {
          "controller envelope without points",
          [](Project &p) { midiOf(p).controllers.at(1).points.clear(); },
          Fault::ValueOutOfRange,
      },
  });
}

} // namespace lina::model::testing
