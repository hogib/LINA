// SPDX-License-Identifier: GPL-3.0-or-later
//
// Commands on tracks and their mixer strips.
#include "support/builders.hpp"
#include "support/checks.hpp"

#include "lina/model/document.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

namespace lina::model::testing {

namespace {

SetTrackProperty set(TrackId track, TrackProperty property) {
  return {.track = track, .property = std::move(property)};
}

} // namespace

TEST_CASE("a new audio or MIDI track has a slot per scene and a send per "
          "return",
          "[model][command][track]") {
  Fixture fixture = makeFixture();
  const ChangeSet changes =
      applied(fixture.document,
              AddTrack{.kind = TrackKind::Midi, .name = "Keys", .index = 1});
  const Project &project = fixture.project();
  const TrackId id =
      changes.firstAdded<ObjectKind::Track>().value_or(TrackId{});
  const Track &track = project.tracks.at(1);

  REQUIRE(project.tracks.size() == 3);
  REQUIRE(track.id == id);
  REQUIRE(track.kind == TrackKind::Midi);
  REQUIRE(track.name == "Keys");
  REQUIRE(track.slots.size() == 2);
  REQUIRE(track.slots.at(0).scene == fixture.sceneA);
  REQUIRE(track.slots.at(1).scene == fixture.sceneB);
  REQUIRE(track.mixer.sends == std::vector<Send>{{.target = fixture.returnTrack,
                                                  .levelDb = silenceDb}});
  REQUIRE(track.mixer.volumeDb == 0.0);
  // The track and its two slots.
  REQUIRE(changes.changes().size() == 3);
  REQUIRE(says(changes, id, ChangeKind::Added));
  REQUIRE(says(changes, track.slots.at(0).id, ChangeKind::Added));
  REQUIRE(changes.find(track.slots.at(1).id)->parent == ObjectRef{id});
}

TEST_CASE("a new return track gives every audio and MIDI track a send to it",
          "[model][command][track]") {
  Fixture fixture = makeFixture();
  const ChangeSet changes =
      applied(fixture.document,
              AddTrack{.kind = TrackKind::Return, .name = "Echo", .index = 0});
  const Project &project = fixture.project();
  const TrackId id =
      changes.firstAdded<ObjectKind::Track>().value_or(TrackId{});

  REQUIRE(project.returns.size() == 2);
  REQUIRE(project.returns.at(0).id == id);
  REQUIRE(project.returns.at(0).slots.empty());
  REQUIRE(project.returns.at(0).mixer.sends.empty());
  for (const Track &track : project.tracks) {
    REQUIRE(track.mixer.sends.size() == 2);
    REQUIRE(track.mixer.sends.at(0).target == id);
    REQUIRE(track.mixer.sends.at(0).levelDb == silenceDb);
    REQUIRE(track.mixer.sends.at(1).target == fixture.returnTrack);
    REQUIRE(says(changes, track.id, ChangeKind::Changed, Property::Sends));
  }
  REQUIRE(changes.changes().size() == 3);
}

TEST_CASE("tracks cannot be added as a master or past the end of their list",
          "[model][command][track]") {
  Fixture fixture = makeFixture();
  REQUIRE(refused(fixture.document, AddTrack{.kind = TrackKind::Master,
                                             .name = "Second master",
                                             .index = 0}) ==
          Fault::WrongTrackKind);
  REQUIRE(
      refused(fixture.document,
              AddTrack{.kind = TrackKind::Audio, .name = "x", .index = 3}) ==
      Fault::IndexOutOfRange);
  REQUIRE(
      refused(fixture.document,
              AddTrack{.kind = TrackKind::Return, .name = "x", .index = 2}) ==
      Fault::IndexOutOfRange);
}

TEST_CASE("removing a track lists everything that goes with it",
          "[model][command][track]") {
  Fixture fixture = makeFixture();
  Document &document = fixture.document;
  const ClipId slotClip =
      applied(document,
              AddClip{
                  .container = fixture.slot(fixture.midi, fixture.sceneB),
                  .clip = makeMidiClip(beat(0), beats(4),
                                       {makeNote(60, beat(0), beats(1))}),
              })
          .firstAdded<ObjectKind::Clip>()
          .value_or(ClipId{});
  const ClipId arrangementClip =
      applied(document,
              AddClip{
                  .container = fixture.midi,
                  .clip = makeMidiClip(beat(4), beats(4)),
              })
          .firstAdded<ObjectKind::Clip>()
          .value_or(ClipId{});
  const DeviceId device = applied(document,
                                  AddDevice{
                                      .track = fixture.midi,
                                      .device = makeInstrument("synth"),
                                      .index = 0,
                                  })
                              .firstAdded<ObjectKind::Device>()
                              .value_or(DeviceId{});
  const SlotId slotB = fixture.slot(fixture.midi, fixture.sceneB);
  const NoteId note =
      std::get<MidiContent>(findClip(fixture.project(), slotClip).clip->content)
          .notes.at(0)
          .id;

  const ChangeSet changes = applied(document, RemoveTrack{fixture.midi});
  // Track, two slots, two clips, one note, one device.
  REQUIRE(changes.changes().size() == 7);
  REQUIRE(says(changes, fixture.midi, ChangeKind::Removed));
  REQUIRE(says(changes, slotB, ChangeKind::Removed));
  REQUIRE(says(changes, slotClip, ChangeKind::Removed));
  REQUIRE(says(changes, note, ChangeKind::Removed));
  REQUIRE(says(changes, arrangementClip, ChangeKind::Removed));
  REQUIRE(changes.find(arrangementClip)->parent == ObjectRef{fixture.midi});
  REQUIRE(says(changes, device, ChangeKind::Removed));
  REQUIRE(fixture.project().tracks.size() == 1);
  REQUIRE(findTrack(fixture.project(), fixture.midi) == nullptr);

  REQUIRE(document.undo().has_value());
  REQUIRE(fixture.project().tracks.at(1).id == fixture.midi);
  REQUIRE(findClip(fixture.project(), slotClip).slot->id == slotB);
  REQUIRE(findDevice(fixture.project(), device).track->id == fixture.midi);
}

TEST_CASE("removing a return track removes the sends to it and undo restores "
          "their levels",
          "[model][command][track]") {
  Fixture fixture = makeFixture();
  Document &document = fixture.document;
  applied(document,
          AddTrack{.kind = TrackKind::Return, .name = "Echo", .index = 1});
  applied(document, set(fixture.audio, SendValue{
                                           {
                                               .target = fixture.returnTrack,
                                               .levelDb = -12.0,
                                           },
                                       }));
  applied(document, set(fixture.midi, SendValue{
                                          {
                                              .target = fixture.returnTrack,
                                              .levelDb = -3.0,
                                          },
                                      }));

  const ChangeSet changes = applied(document, RemoveTrack{fixture.returnTrack});
  REQUIRE(says(changes, fixture.returnTrack, ChangeKind::Removed));
  REQUIRE(says(changes, fixture.audio, ChangeKind::Changed, Property::Sends));
  REQUIRE(says(changes, fixture.midi, ChangeKind::Changed, Property::Sends));
  REQUIRE(fixture.project().returns.size() == 1);
  REQUIRE(fixture.project().tracks.at(0).mixer.sends.size() == 1);

  REQUIRE(document.undo().has_value());
  REQUIRE(fixture.project().returns.at(0).id == fixture.returnTrack);
  REQUIRE(fixture.project().tracks.at(0).mixer.sends.at(0) ==
          Send{.target = fixture.returnTrack, .levelDb = -12.0});
  REQUIRE(fixture.project().tracks.at(1).mixer.sends.at(0) ==
          Send{.target = fixture.returnTrack, .levelDb = -3.0});
}

TEST_CASE("tracks move inside their own list", "[model][command][track]") {
  Fixture fixture = makeFixture();
  Document &document = fixture.document;
  const Project &project = fixture.project();
  applied(document,
          AddTrack{.kind = TrackKind::Audio, .name = "Third", .index = 2});
  const TrackId third = project.tracks.at(2).id;

  ChangeSet changes = applied(document, MoveTrack{.track = third, .index = 0});
  REQUIRE(changes.changes().size() == 1);
  REQUIRE(says(changes, projectRef, ChangeKind::Changed, Property::TrackOrder));
  REQUIRE(project.tracks.at(0).id == third);
  REQUIRE(project.tracks.at(1).id == fixture.audio);
  REQUIRE(project.tracks.at(2).id == fixture.midi);

  changes = applied(document, MoveTrack{.track = third, .index = 1});
  REQUIRE(project.tracks.at(0).id == fixture.audio);
  REQUIRE(project.tracks.at(1).id == third);

  REQUIRE(applied(document, MoveTrack{.track = third, .index = 1}).empty());
  REQUIRE(refused(document, MoveTrack{.track = third, .index = 3}) ==
          Fault::IndexOutOfRange);
  REQUIRE(refused(document, MoveTrack{.track = project.master.id,
                                      .index = 0}) == Fault::WrongTrackKind);
  REQUIRE(refused(document, MoveTrack{.track = TrackId{project.nextId},
                                      .index = 0}) == Fault::UnknownObject);
}

TEST_CASE("moving a return track keeps every track's sends in return order",
          "[model][command][track]") {
  Fixture fixture = makeFixture();
  Document &document = fixture.document;
  const Project &project = fixture.project();
  applied(document,
          AddTrack{.kind = TrackKind::Return, .name = "Echo", .index = 1});
  const TrackId echo = project.returns.at(1).id;
  applied(document,
          set(fixture.audio, SendValue{{.target = echo, .levelDb = -20.0}}));

  const ChangeSet changes =
      applied(document, MoveTrack{.track = echo, .index = 0});
  REQUIRE(changes.changes().size() == 1);
  REQUIRE(
      says(changes, projectRef, ChangeKind::Changed, Property::ReturnOrder));
  REQUIRE(project.returns.at(0).id == echo);
  REQUIRE(project.tracks.at(0).mixer.sends.at(0) ==
          Send{.target = echo, .levelDb = -20.0});
  REQUIRE(project.tracks.at(0).mixer.sends.at(1).target == fixture.returnTrack);
  REQUIRE(refused(document, MoveTrack{.track = echo, .index = 2}) ==
          Fault::IndexOutOfRange);
}

TEST_CASE("mixer and header properties of a track are set, reported and "
          "undone",
          "[model][command][mixer]") {
  Fixture fixture = makeFixture();
  Document &document = fixture.document;
  const TrackId track = fixture.audio;
  const auto strip = [&fixture] -> const MixerStrip & {
    return fixture.project().tracks.at(0).mixer;
  };

  REQUIRE(says(applied(document, set(track, NameValue{"Drums"})), track,
               ChangeKind::Changed, Property::Name));
  REQUIRE(fixture.project().tracks.at(0).name == "Drums");
  REQUIRE(says(applied(document, set(track, ColourValue{{0x112233}})), track,
               ChangeKind::Changed, Property::Colour));
  REQUIRE(fixture.project().tracks.at(0).colour.rgb == 0x112233);
  REQUIRE(says(applied(document, set(track, VolumeValue{-6.5})), track,
               ChangeKind::Changed, Property::Volume));
  REQUIRE(strip().volumeDb == -6.5);
  REQUIRE(says(applied(document, set(track, PanValue{-0.25})), track,
               ChangeKind::Changed, Property::Pan));
  REQUIRE(strip().pan == -0.25);
  REQUIRE(says(applied(document, set(track, MuteValue{true})), track,
               ChangeKind::Changed, Property::Mute));
  REQUIRE(strip().mute);
  REQUIRE(says(applied(document, set(track, SoloValue{true})), track,
               ChangeKind::Changed, Property::Solo));
  REQUIRE(strip().solo);
  REQUIRE(says(applied(document, set(track, ArmedValue{true})), track,
               ChangeKind::Changed, Property::Armed));
  REQUIRE(strip().armed);
  REQUIRE(says(applied(document, set(track, MonitoringValue{Monitoring::On})),
               track, ChangeKind::Changed, Property::Monitoring));
  REQUIRE(strip().monitoring == Monitoring::On);
  const Send send{.target = fixture.returnTrack, .levelDb = -18.0};
  REQUIRE(says(applied(document, set(track, SendValue{send})), track,
               ChangeKind::Changed, Property::Sends));
  REQUIRE(strip().sends.at(0) == send);

  // The limits themselves are allowed.
  applied(document, set(track, VolumeValue{maxVolumeDb}));
  applied(document, set(track, VolumeValue{silenceDb}));
  applied(document, set(track, PanValue{1.0}));
  applied(document, set(track, PanValue{-1.0}));
  applied(document, set(track, SendValue{
                                   {
                                       .target = fixture.returnTrack,
                                       .levelDb = maxSendDb,
                                   },
                               }));

  REQUIRE(applied(document, set(track, MuteValue{true})).empty());
  REQUIRE(applied(document, set(track, SendValue{{.target = fixture.returnTrack,
                                                  .levelDb = maxSendDb}}))
              .empty());
}

TEST_CASE("mixer values outside their range are refused",
          "[model][command][mixer]") {
  Fixture fixture = makeFixture();
  Document &document = fixture.document;
  const TrackId track = fixture.audio;
  REQUIRE(refused(document, set(track, VolumeValue{6.01})) ==
          Fault::ValueOutOfRange);
  REQUIRE(refused(document, set(track, VolumeValue{-96.01})) ==
          Fault::ValueOutOfRange);
  REQUIRE(refused(document, set(track, PanValue{1.01})) ==
          Fault::ValueOutOfRange);
  REQUIRE(refused(document, set(track, PanValue{-1.01})) ==
          Fault::ValueOutOfRange);
  REQUIRE(refused(document, set(track, ColourValue{{0x1000000}})) ==
          Fault::ValueOutOfRange);
  REQUIRE(refused(document, set(track, SendValue{{.target = fixture.returnTrack,
                                                  .levelDb = 0.01}})) ==
          Fault::ValueOutOfRange);
  REQUIRE(refused(document, set(track, SendValue{{.target = fixture.midi,
                                                  .levelDb = -6.0}})) ==
          Fault::UnknownObject);
  REQUIRE(refused(document, set(TrackId{fixture.project().nextId},
                                VolumeValue{0.0})) == Fault::UnknownObject);
}

TEST_CASE("the master and return tracks have fewer controls",
          "[model][command][mixer]") {
  Fixture fixture = makeFixture();
  Document &document = fixture.document;
  const TrackId master = fixture.project().master.id;
  const TrackId returnTrack = fixture.returnTrack;

  REQUIRE(says(applied(document, set(master, VolumeValue{-3.0})), master,
               ChangeKind::Changed, Property::Volume));
  REQUIRE(says(applied(document, set(master, NameValue{"Main"})), master,
               ChangeKind::Changed, Property::Name));
  REQUIRE(says(applied(document, set(master, ColourValue{{0x00ff00}})), master,
               ChangeKind::Changed, Property::Colour));
  REQUIRE(refused(document, set(master, PanValue{0.5})) ==
          Fault::WrongTrackKind);
  REQUIRE(refused(document, set(master, MuteValue{true})) ==
          Fault::WrongTrackKind);
  REQUIRE(refused(document, set(master, SoloValue{true})) ==
          Fault::WrongTrackKind);
  REQUIRE(refused(document, set(master, ArmedValue{true})) ==
          Fault::WrongTrackKind);

  REQUIRE(says(applied(document, set(returnTrack, PanValue{0.5})), returnTrack,
               ChangeKind::Changed, Property::Pan));
  REQUIRE(says(applied(document, set(returnTrack, MuteValue{true})),
               returnTrack, ChangeKind::Changed, Property::Mute));
  REQUIRE(says(applied(document, set(returnTrack, SoloValue{true})),
               returnTrack, ChangeKind::Changed, Property::Solo));
  REQUIRE(refused(document, set(returnTrack, ArmedValue{true})) ==
          Fault::WrongTrackKind);
  REQUIRE(
      refused(document, set(returnTrack, MonitoringValue{Monitoring::Off})) ==
      Fault::WrongTrackKind);
  REQUIRE(refused(document, set(returnTrack, SendValue{{.target = returnTrack,
                                                        .levelDb = -6.0}})) ==
          Fault::WrongTrackKind);
}

TEST_CASE("the master and unknown tracks cannot be removed",
          "[model][command][track]") {
  Fixture fixture = makeFixture();
  REQUIRE(refused(fixture.document, RemoveTrack{fixture.project().master.id}) ==
          Fault::WrongTrackKind);
  REQUIRE(refused(fixture.document,
                  RemoveTrack{TrackId{fixture.project().nextId}}) ==
          Fault::UnknownObject);
}

TEST_CASE("removing a return track removes the automation of the sends to it, "
          "and undo brings it back",
          "[model][command][track]") {
  Fixture fixture = makeFixture();
  Document &document = fixture.document;
  const Project &project = fixture.project();
  const auto envelopeOn = [&](TrackId track, AutomationTarget target) {
    return applied(document,
                   AddEnvelope{
                       .track = track,
                       .envelope =
                           {
                               .id = EnvelopeId{0},
                               .target = std::move(target),
                               .points = {{.time = beat(1), .value = -6.0}},
                           },
                   })
        .firstAdded<ObjectKind::Envelope>()
        .value_or(EnvelopeId{});
  };
  const EnvelopeId audioSend =
      envelopeOn(fixture.audio, SendTarget{fixture.returnTrack});
  const EnvelopeId audioVolume =
      envelopeOn(fixture.audio, MixerTarget{MixerParameter::Volume});
  const EnvelopeId midiSend =
      envelopeOn(fixture.midi, SendTarget{fixture.returnTrack});
  const EnvelopeId returnVolume =
      envelopeOn(fixture.returnTrack, MixerTarget{MixerParameter::Volume});
  const Project before = project;

  const ChangeSet changes = applied(document, RemoveTrack{fixture.returnTrack});
  // The return track and its own envelope, both send envelopes, and the sends
  // of both tracks.
  REQUIRE(changes.changes().size() == 6);
  REQUIRE(says(changes, fixture.returnTrack, ChangeKind::Removed));
  REQUIRE(says(changes, returnVolume, ChangeKind::Removed));
  REQUIRE(says(changes, audioSend, ChangeKind::Removed));
  REQUIRE(changes.find(audioSend)->parent == ObjectRef{fixture.audio});
  REQUIRE(says(changes, midiSend, ChangeKind::Removed));
  REQUIRE(changes.find(midiSend)->parent == ObjectRef{fixture.midi});
  REQUIRE(says(changes, fixture.audio, ChangeKind::Changed, Property::Sends));
  REQUIRE(findEnvelope(project, audioSend).envelope == nullptr);
  REQUIRE(findEnvelope(project, audioVolume).track->id == fixture.audio);
  REQUIRE(project.tracks.at(1).automation.empty());

  REQUIRE(document.undo().has_value());
  REQUIRE(sameContent(project, before));
  REQUIRE(findEnvelope(project, midiSend).track->id == fixture.midi);
  REQUIRE(project.tracks.at(0).automation.at(0).id == audioSend);
}

TEST_CASE("a track's input is set within what its kind allows",
          "[model][command][mixer]") {
  Fixture fixture = makeFixture();
  Document &document = fixture.document;
  const Project &project = fixture.project();
  REQUIRE(project.tracks.at(0).mixer.input == TrackInput{NoInput{}});
  REQUIRE(project.tracks.at(1).mixer.input == TrackInput{AllMidiInputs{}});

  const TrackInput pair = AudioInput{.channel = 2, .stereo = true};
  ChangeSet changes = applied(document, set(fixture.audio, InputValue{pair}));
  REQUIRE(says(changes, fixture.audio, ChangeKind::Changed, Property::Input));
  REQUIRE(project.tracks.at(0).mixer.input == pair);
  REQUIRE(applied(document, set(fixture.audio, InputValue{pair})).empty());
  applied(document, set(fixture.audio, InputValue{NoInput{}}));

  const TrackInput keys = MidiDeviceInput{"Keystation 49"};
  changes = applied(document, set(fixture.midi, InputValue{keys}));
  REQUIRE(says(changes, fixture.midi, ChangeKind::Changed, Property::Input));
  REQUIRE(project.tracks.at(1).mixer.input == keys);
  applied(document, set(fixture.midi, InputValue{AllMidiInputs{}}));

  REQUIRE(refused(document, set(fixture.audio, InputValue{keys})) ==
          Fault::WrongInputKind);
  REQUIRE(refused(document, set(fixture.audio, InputValue{AllMidiInputs{}})) ==
          Fault::WrongInputKind);
  REQUIRE(refused(document, set(fixture.midi, InputValue{pair})) ==
          Fault::WrongInputKind);
  REQUIRE(refused(document, set(fixture.midi, InputValue{NoInput{}})) ==
          Fault::WrongInputKind);
  REQUIRE(refused(document, set(fixture.audio, InputValue{AudioInput{
                                                   .channel = maxInputChannels,
                                                   .stereo = false}})) ==
          Fault::ValueOutOfRange);
  REQUIRE(
      refused(document, set(fixture.midi, InputValue{MidiDeviceInput{""}})) ==
      Fault::ValueOutOfRange);
  REQUIRE(refused(document, set(fixture.returnTrack, InputValue{pair})) ==
          Fault::WrongTrackKind);
  REQUIRE(refused(document, set(project.master.id, InputValue{NoInput{}})) ==
          Fault::WrongTrackKind);
}

TEST_CASE("negative zero is stored as zero and is no change where zero "
          "already is",
          "[model][command][mixer]") {
  Fixture fixture = makeFixture();
  Document &document = fixture.document;
  const auto strip = [&fixture] -> const MixerStrip & {
    return fixture.project().tracks.at(0).mixer;
  };

  // Pan and volume are 0.0: asking for -0.0 changes nothing, not even the
  // sign, and leaves no undo step.
  const bool couldUndo = document.canUndo();
  const std::string_view undoName = document.undoName().value_or("");
  REQUIRE(applied(document, set(fixture.audio, PanValue{-0.0})).empty());
  REQUIRE(applied(document, set(fixture.audio, VolumeValue{-0.0})).empty());
  REQUIRE(!std::signbit(strip().pan));
  REQUIRE(!std::signbit(strip().volumeDb));
  REQUIRE(document.canUndo() == couldUndo);
  REQUIRE(document.undoName() == undoName);

  applied(document, set(fixture.audio, PanValue{0.5}));
  applied(document, set(fixture.audio, PanValue{-0.0}));
  REQUIRE(strip().pan == 0.0);
  REQUIRE(!std::signbit(strip().pan));

  applied(document, set(fixture.audio, SendValue{
                                           {
                                               .target = fixture.returnTrack,
                                               .levelDb = -0.0,
                                           },
                                       }));
  REQUIRE(!std::signbit(strip().sends.at(0).levelDb));
  REQUIRE(applied(document,
                  set(fixture.audio, SendValue{{.target = fixture.returnTrack,
                                                .levelDb = -0.0}}))
              .empty());
  REQUIRE(validate(fixture.project()).empty());
}

TEST_CASE("values that are not numbers or not enumerators are refused",
          "[model][command][mixer]") {
  Fixture fixture = makeFixture();
  Document &document = fixture.document;
  constexpr double nan = std::numeric_limits<double>::quiet_NaN();
  constexpr double infinity = std::numeric_limits<double>::infinity();
  REQUIRE(refused(document, set(fixture.audio, VolumeValue{nan})) ==
          Fault::ValueOutOfRange);
  REQUIRE(refused(document, set(fixture.audio, VolumeValue{-infinity})) ==
          Fault::ValueOutOfRange);
  REQUIRE(refused(document, set(fixture.audio, PanValue{nan})) ==
          Fault::ValueOutOfRange);
  REQUIRE(refused(document,
                  set(fixture.audio, SendValue{{.target = fixture.returnTrack,
                                                .levelDb = nan}})) ==
          Fault::ValueOutOfRange);

  // NOLINTBEGIN(clang-analyzer-optin.core.EnumCastOutOfRange): on purpose
  const auto monitoring = static_cast<Monitoring>(77);
  const auto kind = static_cast<TrackKind>(9);
  // NOLINTEND(clang-analyzer-optin.core.EnumCastOutOfRange)
  REQUIRE(refused(document, set(fixture.audio, MonitoringValue{monitoring})) ==
          Fault::ValueOutOfRange);
  REQUIRE(refusal(document, AddTrack{.kind = kind, .name = "x", .index = 0}) ==
          Failure{.fault = Fault::ValueOutOfRange, .object = projectRef});
  REQUIRE(fixture.project().returns.size() == 1);
}

} // namespace lina::model::testing
