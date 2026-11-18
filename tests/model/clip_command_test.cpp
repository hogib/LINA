// SPDX-License-Identifier: GPL-3.0-or-later
//
// Commands on clips: adding, removing, restoring, moving and setting
// properties.
#include "support/builders.hpp"
#include "support/checks.hpp"

#include "lina/model/document.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace lina::model::testing {

namespace {

SetClipProperty set(ClipId clip, ClipProperty property) {
  return {.clip = clip, .property = std::move(property)};
}

ClipId add(Document &document, ClipContainer container, Clip clip) {
  return applied(document,
                 AddClip{.container = container, .clip = std::move(clip)})
      .firstAdded<ObjectKind::Clip>()
      .value_or(ClipId{});
}

// The fixture with a second MIDI track, for moves between tracks of one kind.
struct TwoMidiTracks {
  Fixture fixture;
  TrackId second;
};

TwoMidiTracks makeTwoMidiTracks() {
  TwoMidiTracks tracks{.fixture = makeFixture(), .second = {}};
  tracks.second =
      applied(tracks.fixture.document,
              AddTrack{.kind = TrackKind::Midi, .name = "Second", .index = 2})
          .firstAdded<ObjectKind::Track>()
          .value_or(TrackId{});
  return tracks;
}

std::vector<ClipId> arrangementIds(const Project &project, TrackId track) {
  std::vector<ClipId> ids;
  for (const Clip &clip : findTrack(project, track)->arrangement) {
    ids.push_back(clip.id);
  }
  return ids;
}

} // namespace

TEST_CASE("a clip added to a slot gets identities for itself and its notes",
          "[model][command][clip]") {
  Fixture fixture = makeFixture();
  const SlotId slot = fixture.slot(fixture.midi, fixture.sceneA);
  const std::uint64_t firstId = fixture.project().nextId;
  // Given out of order: stored by start, then pitch; identities follow the
  // stored order.
  const ChangeSet changes =
      applied(fixture.document,
              AddClip{
                  .container = slot,
                  .clip = makeMidiClip(beat(0), beats(4),
                                       {
                                           makeNote(64, beat(2), beats(1)),
                                           makeNote(62, beat(0), beats(1)),
                                           makeNote(60, beat(0), beats(1)),
                                       }),
              });
  const ClipLocation location = findClip(fixture.project(), ClipId{firstId});
  REQUIRE(location.clip != nullptr);
  REQUIRE(location.slot->id == slot);
  REQUIRE(location.track->id == fixture.midi);

  const std::vector<Note> &notes =
      std::get<MidiContent>(location.clip->content).notes;
  REQUIRE(notes.size() == 3);
  REQUIRE(notes.at(0).pitch == 60);
  REQUIRE(notes.at(1).pitch == 62);
  REQUIRE(notes.at(2).pitch == 64);
  REQUIRE(notes.at(0).id == NoteId{firstId + 1});
  REQUIRE(notes.at(2).id == NoteId{firstId + 3});
  REQUIRE(fixture.project().nextId == firstId + 4);
  REQUIRE(findNote(*location.clip, notes.at(1).id) == &notes.at(1));
  REQUIRE(findNote(*location.clip, NoteId{firstId + 9}) == nullptr);

  REQUIRE(changes.changes().size() == 4);
  REQUIRE(says(changes, ClipId{firstId}, ChangeKind::Added));
  REQUIRE(changes.find(ClipId{firstId})->parent == ObjectRef{slot});
  REQUIRE(changes.find(notes.at(0).id)->parent == ObjectRef{ClipId{firstId}});
}

TEST_CASE("arrangement clips are kept in position order and may touch",
          "[model][command][clip]") {
  Fixture fixture = makeFixture();
  Document &document = fixture.document;
  const ClipId late =
      add(document, fixture.midi, makeMidiClip(beat(8), beats(4)));
  const ClipId early =
      add(document, fixture.midi, makeMidiClip(beat(0), beats(4)));
  const ClipId middle =
      add(document, fixture.midi, makeMidiClip(beat(4), beats(4)));
  REQUIRE(arrangementIds(fixture.project(), fixture.midi) ==
          std::vector<ClipId>{early, middle, late});
  const ClipLocation location = findClip(fixture.project(), middle);
  REQUIRE(location.slot == nullptr);
  REQUIRE(location.track->id == fixture.midi);
  REQUIRE(location.clip->end() == beat(8));
}

TEST_CASE("adding a clip is refused when it breaks a rule",
          "[model][command][clip]") {
  Fixture fixture = makeFixture();
  Document &document = fixture.document;
  const Clip midi = makeMidiClip(beat(4), beats(4));
  const Clip audio = makeAudioClip(fixture.media, beat(4), beats(4));
  const SlotId midiSlot = fixture.slot(fixture.midi, fixture.sceneA);
  const std::uint64_t unknown = fixture.project().nextId + 5;

  REQUIRE(refused(document, AddClip{.container = SlotId{unknown},
                                    .clip = midi}) == Fault::UnknownObject);
  REQUIRE(refused(document, AddClip{.container = TrackId{unknown},
                                    .clip = midi}) == Fault::UnknownObject);
  REQUIRE(refused(document, AddClip{.container = fixture.returnTrack,
                                    .clip = audio}) == Fault::WrongTrackKind);
  REQUIRE(refused(document, AddClip{.container = fixture.project().master.id,
                                    .clip = audio}) == Fault::WrongTrackKind);
  REQUIRE(refused(document, AddClip{.container = fixture.midi,
                                    .clip = audio}) == Fault::WrongTrackKind);
  REQUIRE(refused(document, AddClip{.container = fixture.audio,
                                    .clip = midi}) == Fault::WrongTrackKind);

  Clip withId = midi;
  withId.id = ClipId{unknown};
  REQUIRE(refused(document, AddClip{.container = fixture.midi,
                                    .clip = withId}) == Fault::IdInvalid);
  Clip withNoteId =
      makeMidiClip(beat(4), beats(4), {makeNote(60, beat(0), beats(1))});
  std::get<MidiContent>(withNoteId.content).notes.at(0).id = NoteId{unknown};
  REQUIRE(refused(document, AddClip{.container = fixture.midi,
                                    .clip = withNoteId}) == Fault::IdInvalid);
  // Both at once.
  withNoteId.id = ClipId{unknown + 1};
  REQUIRE(refused(document, AddClip{.container = fixture.midi,
                                    .clip = withNoteId}) == Fault::IdInvalid);

  REQUIRE(refused(document, AddClip{.container = fixture.midi,
                                    .clip = makeMidiClip(beat(4), {0})}) ==
          Fault::ValueOutOfRange);
  // A clip in a slot has no position of its own.
  REQUIRE(refused(document, AddClip{.container = midiSlot, .clip = midi}) ==
          Fault::ValueOutOfRange);
  REQUIRE(refused(document, AddClip{.container = fixture.midi,
                                    .clip = makeMidiClip(
                                        beat(0), beats(4),
                                        {makeNote(60, beat(0), beats(2)),
                                         makeNote(60, beat(1), beats(1))})}) ==
          Fault::NoteOverlap);
  REQUIRE(refused(document, AddClip{.container = fixture.audio,
                                    .clip = makeAudioClip(
                                        MediaId{unknown}, beat(0),
                                        beats(4))}) == Fault::MediaMissing);

  add(document, midiSlot, makeMidiClip(beat(0), beats(4)));
  REQUIRE(refused(document, AddClip{.container = midiSlot,
                                    .clip = makeMidiClip(beat(0), beats(4))}) ==
          Fault::SlotOccupied);

  add(document, fixture.midi, midi);
  // Overlapping from the left, from the right, inside and around.
  REQUIRE(refused(document, AddClip{.container = fixture.midi,
                                    .clip = makeMidiClip(beat(2), beats(3))}) ==
          Fault::ClipOverlap);
  REQUIRE(refused(document, AddClip{.container = fixture.midi,
                                    .clip = makeMidiClip(beat(7), beats(3))}) ==
          Fault::ClipOverlap);
  REQUIRE(refused(document, AddClip{.container = fixture.midi,
                                    .clip = makeMidiClip(beat(5), beats(1))}) ==
          Fault::ClipOverlap);
  REQUIRE(
      refused(document, AddClip{.container = fixture.midi,
                                .clip = makeMidiClip(beat(0), beats(16))}) ==
      Fault::ClipOverlap);
}

TEST_CASE("removing a clip lists its notes and undo puts it back where it was",
          "[model][command][clip]") {
  Fixture fixture = makeFixture();
  Document &document = fixture.document;
  const SlotId slot = fixture.slot(fixture.midi, fixture.sceneB);
  const ClipId inSlot =
      add(document, slot,
          makeMidiClip(beat(0), beats(4), {makeNote(60, beat(0), beats(1))}));
  const ClipId inArrangement = add(
      document, fixture.audio, makeAudioClip(fixture.media, beat(4), beats(4)));

  ChangeSet changes = applied(document, RemoveClip{inSlot});
  REQUIRE(changes.changes().size() == 2);
  REQUIRE(says(changes, inSlot, ChangeKind::Removed));
  REQUIRE(changes.find(inSlot)->parent == ObjectRef{slot});
  REQUIRE(findClip(fixture.project(), inSlot).clip == nullptr);
  REQUIRE(!findSlot(fixture.project(), slot).slot->clip.has_value());

  changes = applied(document, RemoveClip{inArrangement});
  REQUIRE(changes.changes().size() == 1);
  REQUIRE(changes.find(inArrangement)->parent == ObjectRef{fixture.audio});
  REQUIRE(fixture.project().tracks.at(0).arrangement.empty());

  REQUIRE(refused(document, RemoveClip{inSlot}) == Fault::UnknownObject);
}

TEST_CASE("a clip moves inside its arrangement", "[model][command][clip]") {
  Fixture fixture = makeFixture();
  Document &document = fixture.document;
  const ClipId first =
      add(document, fixture.midi, makeMidiClip(beat(0), beats(4)));
  const ClipId second =
      add(document, fixture.midi, makeMidiClip(beat(4), beats(4)));

  const ChangeSet changes = applied(
      document,
      MoveClip{.clip = first, .container = fixture.midi, .position = beat(8)});
  REQUIRE(changes.changes().size() == 1);
  REQUIRE(says(changes, first, ChangeKind::Changed, Property::Extent));
  REQUIRE(changes.find(first)->parent == ObjectRef{fixture.midi});
  REQUIRE(changes.find(first)->previousParent == ObjectRef{fixture.midi});
  REQUIRE(arrangementIds(fixture.project(), fixture.midi) ==
          std::vector<ClipId>{second, first});
  REQUIRE(findClip(fixture.project(), first).clip->extent.position == beat(8));

  // Onto itself, partly: only other clips are in the way.
  applied(
      document,
      MoveClip{.clip = first, .container = fixture.midi, .position = beat(9)});
  REQUIRE(applied(document, MoveClip{.clip = first,
                                     .container = fixture.midi,
                                     .position = beat(9)})
              .empty());
  REQUIRE(refused(document, MoveClip{.clip = first,
                                     .container = fixture.midi,
                                     .position = beat(6)}) ==
          Fault::ClipOverlap);
  REQUIRE(refused(document, MoveClip{.clip = first,
                                     .container = fixture.midi,
                                     .position = {-1}}) ==
          Fault::ValueOutOfRange);
}

TEST_CASE("a clip moves between tracks of one kind, and between arrangement "
          "and slots",
          "[model][command][clip]") {
  TwoMidiTracks tracks = makeTwoMidiTracks();
  Fixture &fixture = tracks.fixture;
  Document &document = fixture.document;
  const SlotId slotA = fixture.slot(fixture.midi, fixture.sceneA);
  const SlotId otherSlot = fixture.slot(tracks.second, fixture.sceneB);
  const ClipId clip =
      add(document, fixture.midi,
          makeMidiClip(beat(4), beats(4), {makeNote(60, beat(0), beats(1))}));
  const PropertySet both = PropertySet{Property::Container} | Property::Extent;

  // To the other track's arrangement, same position: only the container
  // changes.
  ChangeSet changes = applied(
      document,
      MoveClip{.clip = clip, .container = tracks.second, .position = beat(4)});
  REQUIRE(changes.changes().size() == 1);
  REQUIRE(says(changes, clip, ChangeKind::Changed, Property::Container));
  REQUIRE(changes.find(clip)->previousParent == ObjectRef{fixture.midi});
  REQUIRE(changes.find(clip)->parent == ObjectRef{tracks.second});
  REQUIRE(findClip(fixture.project(), clip).track->id == tracks.second);

  // Into a slot: the position becomes zero.
  changes =
      applied(document,
              MoveClip{.clip = clip, .container = slotA, .position = beat(0)});
  REQUIRE(says(changes, clip, ChangeKind::Changed, both));
  REQUIRE(changes.find(clip)->parent == ObjectRef{slotA});
  REQUIRE(findClip(fixture.project(), clip).slot->id == slotA);
  REQUIRE(findTrack(fixture.project(), tracks.second)->arrangement.empty());

  // Slot to slot on another track.
  changes = applied(
      document,
      MoveClip{.clip = clip, .container = otherSlot, .position = beat(0)});
  REQUIRE(says(changes, clip, ChangeKind::Changed, Property::Container));
  REQUIRE(changes.find(clip)->previousParent == ObjectRef{slotA});
  REQUIRE(!findSlot(fixture.project(), slotA).slot->clip.has_value());

  // To the slot it is in: nothing happens.
  REQUIRE(applied(document, MoveClip{.clip = clip,
                                     .container = otherSlot,
                                     .position = beat(0)})
              .empty());

  // Back out to an arrangement.
  changes = applied(
      document,
      MoveClip{.clip = clip, .container = fixture.midi, .position = beat(2)});
  REQUIRE(says(changes, clip, ChangeKind::Changed, both));
  REQUIRE(findClip(fixture.project(), clip).slot == nullptr);
  // The notes went along and are not reported: they did not change.
  REQUIRE(std::get<MidiContent>(findClip(fixture.project(), clip).clip->content)
              .notes.size() == 1);
}

TEST_CASE("moving a clip is refused when it breaks a rule",
          "[model][command][clip]") {
  TwoMidiTracks tracks = makeTwoMidiTracks();
  Fixture &fixture = tracks.fixture;
  Document &document = fixture.document;
  const SlotId slotA = fixture.slot(fixture.midi, fixture.sceneA);
  const SlotId slotB = fixture.slot(fixture.midi, fixture.sceneB);
  const ClipId clip =
      add(document, fixture.midi, makeMidiClip(beat(4), beats(4)));
  const ClipId inSlot = add(document, slotA, makeMidiClip(beat(0), beats(4)));
  add(document, tracks.second, makeMidiClip(beat(0), beats(8)));
  const std::uint64_t unknown = fixture.project().nextId + 5;

  REQUIRE(refused(document, MoveClip{.clip = ClipId{unknown},
                                     .container = fixture.midi,
                                     .position = beat(0)}) ==
          Fault::UnknownObject);
  REQUIRE(refused(document, MoveClip{.clip = clip,
                                     .container = SlotId{unknown},
                                     .position = beat(0)}) ==
          Fault::UnknownObject);
  REQUIRE(refused(document, MoveClip{.clip = clip,
                                     .container = fixture.returnTrack,
                                     .position = beat(0)}) ==
          Fault::WrongTrackKind);
  REQUIRE(refused(document, MoveClip{.clip = clip,
                                     .container = fixture.audio,
                                     .position = beat(0)}) ==
          Fault::WrongTrackKind);
  REQUIRE(
      refused(document,
              MoveClip{.clip = clip,
                       .container = fixture.slot(fixture.audio, fixture.sceneA),
                       .position = beat(0)}) == Fault::WrongTrackKind);
  REQUIRE(refused(document, MoveClip{.clip = clip,
                                     .container = slotB,
                                     .position = beat(1)}) ==
          Fault::ValueOutOfRange);
  REQUIRE(refused(document, MoveClip{.clip = clip,
                                     .container = slotA,
                                     .position = beat(0)}) ==
          Fault::SlotOccupied);
  REQUIRE(refused(document, MoveClip{.clip = clip,
                                     .container = tracks.second,
                                     .position = beat(4)}) ==
          Fault::ClipOverlap);
  REQUIRE(refused(document, MoveClip{.clip = inSlot,
                                     .container = fixture.midi,
                                     .position = beat(6)}) ==
          Fault::ClipOverlap);
}

TEST_CASE("clip properties are set, reported and undone",
          "[model][command][clip]") {
  Fixture fixture = makeFixture();
  Document &document = fixture.document;
  const ClipId id = add(document, fixture.audio,
                        makeAudioClip(fixture.media, beat(4), beats(4)));
  const auto clip = [&] -> const Clip & {
    return *findClip(fixture.project(), id).clip;
  };
  const auto audio = [&] -> const AudioContent & {
    return std::get<AudioContent>(clip().content);
  };
  const auto check = [&](const ClipProperty &property, Property expected) {
    const ChangeSet changes = applied(document, set(id, property));
    REQUIRE(changes.changes().size() == 1);
    REQUIRE(says(changes, id, ChangeKind::Changed, expected));
    REQUIRE(changes.find(id)->parent == ObjectRef{fixture.audio});
  };

  check(NameValue{"Break"}, Property::Name);
  REQUIRE(clip().name == "Break");
  check(ColourValue{{0xabcdef}}, Property::Colour);
  REQUIRE(clip().colour.rgb == 0xabcdef);
  const ClipExtent extent{
      .position = beat(6),
      .length = beats(2),
      .contentStart = beat(1),
  };
  check(ExtentValue{extent}, Property::Extent);
  REQUIRE(clip().extent == extent);
  const LoopSettings loop{
      .enabled = true,
      .start = beat(1),
      .length = beats(2),
  };
  check(LoopValue{loop}, Property::Loop);
  REQUIRE(clip().loop == loop);
  check(LaunchOverrideValue{LaunchQuantisation::Eighth},
        Property::LaunchQuantisation);
  REQUIRE(clip().launchQuantisation == LaunchQuantisation::Eighth);
  check(LaunchOverrideValue{std::nullopt}, Property::LaunchQuantisation);
  REQUIRE(!clip().launchQuantisation.has_value());
  check(GainValue{-4.5}, Property::Gain);
  REQUIRE(audio().gainDb == -4.5);
  check(FollowTempoValue{false}, Property::FollowTempo);
  REQUIRE(!audio().followTempo);
  check(StretchModeValue{StretchMode::Repitch}, Property::StretchMode);
  REQUIRE(audio().stretchMode == StretchMode::Repitch);
  check(TransposeValue{-1'200}, Property::Transpose);
  REQUIRE(audio().transposeCents == -1'200);
  const WarpMap warp{
      .markers =
          {
              {.sample = 0, .tick = {}},
              {.sample = 48'000, .tick = beat(2)},
              {.sample = 96'000, .tick = beat(8)},
          },
      .origin = SourceTempoOrigin::Manual,
  };
  check(WarpValue{warp}, Property::Warp);
  REQUIRE(audio().warp == warp);
  check(RegionValue{{.start = 100, .end = 50'000}}, Property::Region);
  REQUIRE(audio().region == SampleRange{.start = 100, .end = 50'000});

  REQUIRE(applied(document, set(id, GainValue{-4.5})).empty());
  REQUIRE(applied(document, set(id, NameValue{"Break"})).empty());
}

TEST_CASE("a new extent keeps the arrangement in order and may not overlap",
          "[model][command][clip]") {
  Fixture fixture = makeFixture();
  Document &document = fixture.document;
  const ClipId first =
      add(document, fixture.midi, makeMidiClip(beat(0), beats(4)));
  const ClipId second =
      add(document, fixture.midi, makeMidiClip(beat(4), beats(4)));

  // Trimming the start: later position, shorter, content start moved along.
  applied(document, set(first, ExtentValue{
                                   {
                                       .position = beat(1),
                                       .length = beats(3),
                                       .contentStart = beat(1),
                                   },
                               }));
  applied(document, set(first, ExtentValue{
                                   {
                                       .position = beat(8),
                                       .length = beats(3),
                                       .contentStart = beat(1),
                                   },
                               }));
  REQUIRE(arrangementIds(fixture.project(), fixture.midi) ==
          std::vector<ClipId>{second, first});

  REQUIRE(refused(document, set(first, ExtentValue{{.position = beat(7),
                                                    .length = beats(3),
                                                    .contentStart = {}}})) ==
          Fault::ClipOverlap);
  REQUIRE(refused(document, set(second, ExtentValue{{.position = beat(4),
                                                     .length = beats(5),
                                                     .contentStart = {}}})) ==
          Fault::ClipOverlap);
  REQUIRE(refused(document, set(second, ExtentValue{{.position = beat(4),
                                                     .length = {0},
                                                     .contentStart = {}}})) ==
          Fault::ValueOutOfRange);
  REQUIRE(refused(document, set(second, ExtentValue{{.position = beat(4),
                                                     .length = beats(1),
                                                     .contentStart = {-1}}})) ==
          Fault::ValueOutOfRange);
}

TEST_CASE("a clip in a slot keeps position zero but can change length",
          "[model][command][clip]") {
  Fixture fixture = makeFixture();
  Document &document = fixture.document;
  const SlotId slot = fixture.slot(fixture.midi, fixture.sceneA);
  const ClipId id = add(document, slot, makeMidiClip(beat(0), beats(4)));

  const ChangeSet changes =
      applied(document, set(id, ExtentValue{
                                    {
                                        .position = beat(0),
                                        .length = beats(8),
                                        .contentStart = beat(2),
                                    },
                                }));
  REQUIRE(changes.find(id)->parent == ObjectRef{slot});
  REQUIRE(refused(document, set(id, ExtentValue{{.position = beat(1),
                                                 .length = beats(8),
                                                 .contentStart = {}}})) ==
          Fault::ValueOutOfRange);
}

TEST_CASE("clip properties are refused when out of range or of the wrong kind",
          "[model][command][clip]") {
  Fixture fixture = makeFixture();
  Document &document = fixture.document;
  const ClipId audio = add(document, fixture.audio,
                           makeAudioClip(fixture.media, beat(0), beats(4)));
  const ClipId midi =
      add(document, fixture.midi, makeMidiClip(beat(0), beats(4)));

  REQUIRE(refused(document, set(ClipId{fixture.project().nextId},
                                NameValue{"x"})) == Fault::UnknownObject);
  REQUIRE(refused(document, set(audio, ColourValue{{0x1000000}})) ==
          Fault::ValueOutOfRange);
  REQUIRE(
      refused(document,
              set(audio,
                  LoopValue{{.enabled = true, .start = {0}, .length = {0}}})) ==
      Fault::ValueOutOfRange);
  REQUIRE(refused(document, set(audio, GainValue{24.01})) ==
          Fault::ValueOutOfRange);
  REQUIRE(refused(document, set(audio, GainValue{-96.01})) ==
          Fault::ValueOutOfRange);
  REQUIRE(refused(document, set(audio, TransposeValue{4'801})) ==
          Fault::ValueOutOfRange);
  REQUIRE(
      refused(document, set(audio, RegionValue{{.start = 10, .end = 10}})) ==
      Fault::ValueOutOfRange);
  REQUIRE(
      refused(document,
              set(audio, WarpValue{{.markers = {{.sample = 0, .tick = {}}},
                                    .origin = SourceTempoOrigin::Manual}})) ==
      Fault::ValueOutOfRange);
  REQUIRE(
      refused(document,
              set(audio, WarpValue{{.markers = {{.sample = 10, .tick = beat(1)},
                                                {.sample = 5, .tick = beat(2)}},
                                    .origin = SourceTempoOrigin::Manual}})) ==
      Fault::OrderViolation);

  REQUIRE(refused(document, set(midi, GainValue{0.0})) == Fault::WrongClipKind);
  REQUIRE(refused(document, set(midi, FollowTempoValue{false})) ==
          Fault::WrongClipKind);
  REQUIRE(
      refused(document, set(midi, StretchModeValue{StretchMode::Repitch})) ==
      Fault::WrongClipKind);
  REQUIRE(refused(document, set(midi, TransposeValue{100})) ==
          Fault::WrongClipKind);
  REQUIRE(refused(document, set(midi, WarpValue{})) == Fault::WrongClipKind);
  REQUIRE(refused(document, set(midi, RegionValue{})) == Fault::WrongClipKind);
}

TEST_CASE("a refused clip names the slot or track it was meant for",
          "[model][command][clip]") {
  Fixture fixture = makeFixture();
  Document &document = fixture.document;
  const SlotId slot = fixture.slot(fixture.midi, fixture.sceneA);
  const ObjectRef track{fixture.midi};

  REQUIRE(refusal(document, AddClip{.container = fixture.midi,
                                    .clip = makeMidiClip(beat(0), {0})}) ==
          Failure{.fault = Fault::ValueOutOfRange, .object = track});
  REQUIRE(refusal(document, AddClip{.container = slot,
                                    .clip = makeMidiClip(beat(2), beats(1))}) ==
          Failure{.fault = Fault::ValueOutOfRange, .object = ObjectRef{slot}});
  REQUIRE(refusal(document, AddClip{.container = fixture.midi,
                                    .clip = makeMidiClip(
                                        beat(0), beats(4),
                                        {makeNote(60, beat(0), beats(2)),
                                         makeNote(60, beat(1), beats(1))})}) ==
          Failure{.fault = Fault::NoteOverlap, .object = track});
  Clip assigned = makeMidiClip(beat(0), beats(4));
  assigned.id = ClipId{fixture.project().nextId + 20};
  REQUIRE(
      refusal(document, AddClip{.container = fixture.midi, .clip = assigned}) ==
      Failure{.fault = Fault::IdInvalid, .object = track});

  applied(document, AddClip{
                        .container = fixture.midi,
                        .clip = makeMidiClip(beat(0), beats(4)),
                    });
  REQUIRE(refusal(document, AddClip{.container = fixture.midi,
                                    .clip = makeMidiClip(beat(2), beats(4))}) ==
          Failure{.fault = Fault::ClipOverlap, .object = track});
}

TEST_CASE("a clip may end on the last storable tick but not past it",
          "[model][command][clip]") {
  Fixture fixture = makeFixture();
  Document &document = fixture.document;
  const ClipId id =
      add(document, fixture.midi,
          makeMidiClip(TickPosition{maxTicks - 100}, TickDuration{100}));
  REQUIRE(findClip(fixture.project(), id).clip->end() ==
          TickPosition{maxTicks});

  REQUIRE(refused(document, AddClip{.container = fixture.midi,
                                    .clip = makeMidiClip(TickPosition{maxTicks},
                                                         TickDuration{1})}) ==
          Fault::ValueOutOfRange);
  REQUIRE(refused(document, MoveClip{.clip = id,
                                     .container = fixture.midi,
                                     .position = {maxTicks - 99}}) ==
          Fault::ValueOutOfRange);
  REQUIRE(refused(document, set(id, ExtentValue{{.position = {maxTicks - 100},
                                                 .length = {101},
                                                 .contentStart = {0}}})) ==
          Fault::ValueOutOfRange);
  REQUIRE(refused(document, set(id, LoopValue{{.enabled = true,
                                               .start = {maxTicks},
                                               .length = {1}}})) ==
          Fault::ValueOutOfRange);
  applied(document, set(id, LoopValue{
                                {
                                    .enabled = true,
                                    .start = {maxTicks - 1},
                                    .length = {1},
                                },
                            }));
  applied(document,
          MoveClip{.clip = id, .container = fixture.midi, .position = {0}});
}

TEST_CASE("clip values that are negative zero, not numbers or not enumerators",
          "[model][command][clip]") {
  Fixture fixture = makeFixture();
  Document &document = fixture.document;
  Clip quiet = makeAudioClip(fixture.media, beat(0), beats(4));
  std::get<AudioContent>(quiet.content).gainDb = -0.0;
  const ClipId id = add(document, fixture.audio, quiet);
  const auto gain = [&] {
    return std::get<AudioContent>(findClip(fixture.project(), id).clip->content)
        .gainDb;
  };
  REQUIRE(!std::signbit(gain()));
  REQUIRE(applied(document, set(id, GainValue{-0.0})).empty());
  applied(document, set(id, GainValue{3.0}));
  applied(document, set(id, GainValue{-0.0}));
  REQUIRE(!std::signbit(gain()));

  REQUIRE(
      refused(document,
              set(id, GainValue{std::numeric_limits<double>::quiet_NaN()})) ==
      Fault::ValueOutOfRange);
  // NOLINTBEGIN(clang-analyzer-optin.core.EnumCastOutOfRange): on purpose
  const auto mode = static_cast<StretchMode>(5);
  const auto origin = static_cast<SourceTempoOrigin>(9);
  const auto quantisation = static_cast<LaunchQuantisation>(99);
  // NOLINTEND(clang-analyzer-optin.core.EnumCastOutOfRange)
  REQUIRE(refused(document, set(id, StretchModeValue{mode})) ==
          Fault::ValueOutOfRange);
  WarpMap warp = std::get<AudioContent>(quiet.content).warp;
  warp.origin = origin;
  REQUIRE(refused(document, set(id, WarpValue{warp})) ==
          Fault::ValueOutOfRange);
  REQUIRE(refused(document, set(id, LaunchOverrideValue{quantisation})) ==
          Fault::ValueOutOfRange);
}

} // namespace lina::model::testing
