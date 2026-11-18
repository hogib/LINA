// SPDX-License-Identifier: GPL-3.0-or-later
//
// Commands on the notes of a MIDI clip.
#include "support/builders.hpp"
#include "support/checks.hpp"

#include "lina/model/document.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace lina::model::testing {

namespace {

// The fixture with an empty MIDI clip in the arrangement and an audio clip in a
// slot.
struct NoteFixture {
  Fixture fixture;
  ClipId clip;
  ClipId audioClip;

  [[nodiscard]] const std::vector<Note> &notes() const {
    return std::get<MidiContent>(
               findClip(fixture.project(), clip).clip->content)
        .notes;
  }
};

NoteFixture makeNoteFixture() {
  NoteFixture made{
      .fixture = makeFixture(),
      .clip = ClipId{0},
      .audioClip = ClipId{0},
  };
  Document &document = made.fixture.document;
  made.clip = applied(document,
                      AddClip{
                          .container = made.fixture.midi,
                          .clip = makeMidiClip(beat(0), beats(8)),
                      })
                  .firstAdded<ObjectKind::Clip>()
                  .value_or(ClipId{});
  made.audioClip =
      applied(document,
              AddClip{
                  .container = made.fixture.slot(made.fixture.audio,
                                                 made.fixture.sceneA),
                  .clip = makeAudioClip(made.fixture.media, beat(0), beats(4)),
              })
          .firstAdded<ObjectKind::Clip>()
          .value_or(ClipId{});
  return made;
}

// Adds notes that must be accepted and returns their identities in the order
// given.
std::vector<NoteId> add(NoteFixture &made, const std::vector<Note> &notes) {
  const std::uint64_t firstId = made.fixture.project().nextId;
  const ChangeSet changes = applied(
      made.fixture.document, AddNotes{.clip = made.clip, .notes = notes});
  REQUIRE(changes.changes().size() == notes.size());
  std::vector<NoteId> ids;
  for (std::size_t i = 0; i < notes.size(); ++i) {
    ids.push_back(NoteId{firstId + i});
    REQUIRE(says(changes, ids.back(), ChangeKind::Added));
    REQUIRE(changes.find(ids.back())->parent == ObjectRef{made.clip});
  }
  return ids;
}

} // namespace

TEST_CASE("added notes get identities in the order given and are stored by "
          "start then pitch",
          "[model][command][note]") {
  NoteFixture made = makeNoteFixture();
  const std::vector<NoteId> ids =
      add(made, {
                    makeNote(64, beat(2), beats(1), 90),
                    makeNote(67, beat(0), beats(1), 80),
                    makeNote(60, beat(0), beats(1), 70),
                });

  const std::vector<Note> &notes = made.notes();
  REQUIRE(notes.size() == 3);
  REQUIRE(notes.at(0) == Note{.id = ids.at(2),
                              .pitch = 60,
                              .start = beat(0),
                              .length = beats(1),
                              .velocity = 70});
  REQUIRE(notes.at(1).id == ids.at(1));
  REQUIRE(notes.at(2).id == ids.at(0));
  REQUIRE(made.fixture.project().nextId == ids.at(2).value + 1);

  // A second batch merges into place.
  const std::vector<NoteId> more = add(made, {makeNote(62, beat(1), beats(1))});
  REQUIRE(made.notes().at(2).id == more.at(0));
  REQUIRE(made.notes().size() == 4);
}

TEST_CASE("same-pitch notes may touch, other pitches may overlap",
          "[model][command][note]") {
  NoteFixture made = makeNoteFixture();
  add(made, {makeNote(60, beat(1), beats(1))});
  add(made, {
                makeNote(60, beat(0), beats(1)),
                makeNote(60, beat(2), beats(1)),
                makeNote(61, beat(0), beats(4)),
            });
  REQUIRE(made.notes().size() == 4);
}

TEST_CASE("adding notes is refused when it breaks a rule",
          "[model][command][note]") {
  NoteFixture made = makeNoteFixture();
  Document &document = made.fixture.document;
  add(made, {makeNote(60, beat(1), beats(2))});
  const auto adding = [&made](std::vector<Note> notes) {
    return AddNotes{.clip = made.clip, .notes = std::move(notes)};
  };

  // Overlapping the existing note from the left, from inside, and exactly.
  REQUIRE(refused(document, adding({makeNote(60, beat(0), beats(2))})) ==
          Fault::NoteOverlap);
  REQUIRE(refused(document, adding({makeNote(60, beat(2), beats(4))})) ==
          Fault::NoteOverlap);
  REQUIRE(refused(document, adding({makeNote(60, beat(1), beats(2))})) ==
          Fault::NoteOverlap);
  // Overlapping each other.
  REQUIRE(refused(document, adding({makeNote(70, beat(0), beats(2)),
                                    makeNote(70, beat(1), beats(2))})) ==
          Fault::NoteOverlap);
  // A good note does not get in with a bad one.
  REQUIRE(refused(document, adding({makeNote(72, beat(0), beats(1)),
                                    makeNote(128, beat(0), beats(1))})) ==
          Fault::ValueOutOfRange);
  REQUIRE(refused(document, adding({makeNote(60, beat(4), beats(1), 0)})) ==
          Fault::ValueOutOfRange);
  REQUIRE(refused(document, adding({makeNote(60, beat(4), beats(1), 128)})) ==
          Fault::ValueOutOfRange);
  REQUIRE(refused(document, adding({makeNote(60, beat(4), {0})})) ==
          Fault::ValueOutOfRange);
  REQUIRE(refused(document, adding({makeNote(60, {-1}, beats(1))})) ==
          Fault::ValueOutOfRange);

  Note withId = makeNote(60, beat(4), beats(1));
  withId.id = NoteId{3};
  REQUIRE(refused(document, adding({withId})) == Fault::IdInvalid);

  REQUIRE(
      refused(document, AddNotes{.clip = made.audioClip,
                                 .notes = {makeNote(60, beat(0), beats(1))}}) ==
      Fault::WrongClipKind);
  REQUIRE(
      refused(document, AddNotes{.clip = ClipId{made.fixture.project().nextId},
                                 .notes = {makeNote(60, beat(0), beats(1))}}) ==
      Fault::UnknownObject);

  REQUIRE(applied(document, adding({})).empty());
  REQUIRE(made.notes().size() == 1);
}

TEST_CASE("notes are removed and undo puts them back",
          "[model][command][note]") {
  NoteFixture made = makeNoteFixture();
  Document &document = made.fixture.document;
  const std::vector<NoteId> ids = add(made, {
                                                makeNote(60, beat(0), beats(1)),
                                                makeNote(62, beat(1), beats(1)),
                                                makeNote(64, beat(2), beats(1)),
                                                makeNote(65, beat(3), beats(1)),
                                            });

  const ChangeSet changes =
      applied(document,
              RemoveNotes{.clip = made.clip, .notes = {ids.at(3), ids.at(1)}});
  REQUIRE(changes.changes().size() == 2);
  REQUIRE(says(changes, ids.at(1), ChangeKind::Removed));
  REQUIRE(says(changes, ids.at(3), ChangeKind::Removed));
  REQUIRE(changes.find(ids.at(1))->parent == ObjectRef{made.clip});
  REQUIRE(made.notes().size() == 2);
  REQUIRE(made.notes().at(0).id == ids.at(0));
  REQUIRE(made.notes().at(1).id == ids.at(2));

  // Already gone, named twice, or never there.
  REQUIRE(refused(document, RemoveNotes{.clip = made.clip,
                                        .notes = {ids.at(0), ids.at(1)}}) ==
          Fault::UnknownObject);
  REQUIRE(refused(document, RemoveNotes{.clip = made.clip,
                                        .notes = {ids.at(0), ids.at(0)}}) ==
          Fault::IdInvalid);
  REQUIRE(refused(document,
                  RemoveNotes{.clip = made.audioClip, .notes = {ids.at(0)}}) ==
          Fault::WrongClipKind);
  REQUIRE(refused(document, RemoveNotes{.clip = ClipId{ids.at(0).value},
                                        .notes = {ids.at(0)}}) ==
          Fault::UnknownObject);
  REQUIRE(
      applied(document, RemoveNotes{.clip = made.clip, .notes = {}}).empty());
}

TEST_CASE("notes are changed together, and only the result has to be free of "
          "overlaps",
          "[model][command][note]") {
  NoteFixture made = makeNoteFixture();
  Document &document = made.fixture.document;
  const std::vector<NoteId> ids = add(made, {
                                                makeNote(60, beat(0), beats(1)),
                                                makeNote(60, beat(1), beats(1)),
                                                makeNote(64, beat(0), beats(1)),
                                            });
  const auto note = [&made](NoteId id) {
    return *findNote(*findClip(made.fixture.project(), made.clip).clip, id);
  };

  // Swap the two pitch-60 notes: either move alone would land on the other.
  Note first = note(ids.at(0));
  Note second = note(ids.at(1));
  first.start = beat(1);
  second.start = beat(0);
  // The third is passed unchanged and must not be reported.
  ChangeSet changes =
      applied(document, SetNotes{
                            .clip = made.clip,
                            .notes = {first, second, note(ids.at(2))},
                        });
  REQUIRE(changes.changes().size() == 2);
  REQUIRE(says(changes, ids.at(0), ChangeKind::Changed, Property::NoteData));
  REQUIRE(says(changes, ids.at(1), ChangeKind::Changed, Property::NoteData));
  REQUIRE(changes.find(ids.at(0))->parent == ObjectRef{made.clip});
  REQUIRE(note(ids.at(0)).start == beat(1));
  // Stored order follows the new starts.
  REQUIRE(made.notes().at(0).id == ids.at(1));

  Note louder = note(ids.at(2));
  louder.velocity = 127;
  louder.pitch = 72;
  louder.length = beats(3);
  changes = applied(document, SetNotes{.clip = made.clip, .notes = {louder}});
  REQUIRE(note(ids.at(2)) == louder);

  REQUIRE(applied(document, SetNotes{.clip = made.clip, .notes = {louder}})
              .empty());
}

TEST_CASE("changing notes is refused when it breaks a rule",
          "[model][command][note]") {
  NoteFixture made = makeNoteFixture();
  Document &document = made.fixture.document;
  const std::vector<NoteId> ids = add(
      made, {makeNote(60, beat(0), beats(1)), makeNote(60, beat(1), beats(1))});
  const std::vector<Note> stored = made.notes();

  Note longer = stored.at(0);
  longer.length = beats(2);
  REQUIRE(refused(document, SetNotes{.clip = made.clip, .notes = {longer}}) ==
          Fault::NoteOverlap);
  Note silent = stored.at(0);
  silent.velocity = 0;
  REQUIRE(refused(document, SetNotes{.clip = made.clip, .notes = {silent}}) ==
          Fault::ValueOutOfRange);
  REQUIRE(refused(document, SetNotes{.clip = made.clip,
                                     .notes = {stored.at(0), stored.at(0)}}) ==
          Fault::IdInvalid);
  Note stranger = stored.at(0);
  stranger.id = NoteId{made.fixture.project().nextId};
  REQUIRE(refused(document, SetNotes{.clip = made.clip, .notes = {stranger}}) ==
          Fault::UnknownObject);
  stranger.id = NoteId{1};
  REQUIRE(refused(document, SetNotes{.clip = made.clip, .notes = {stranger}}) ==
          Fault::UnknownObject);
  REQUIRE(refused(document, SetNotes{.clip = made.audioClip,
                                     .notes = stored}) == Fault::WrongClipKind);
  REQUIRE(refused(document, SetNotes{.clip = ClipId{ids.at(0).value},
                                     .notes = stored}) == Fault::UnknownObject);
}

TEST_CASE("refused notes name the clip, never an identity that was not "
          "handed out",
          "[model][command][note]") {
  NoteFixture made = makeNoteFixture();
  Document &document = made.fixture.document;
  const ObjectRef clip{made.clip};
  const std::vector<NoteId> ids = add(made, {makeNote(60, beat(1), beats(2))});

  REQUIRE(
      refusal(document, AddNotes{.clip = made.clip,
                                 .notes = {makeNote(60, beat(0), beats(2))}}) ==
      Failure{.fault = Fault::NoteOverlap, .object = clip});
  REQUIRE(refusal(document,
                  AddNotes{.clip = made.clip,
                           .notes = {makeNote(128, beat(0), beats(2))}}) ==
          Failure{.fault = Fault::ValueOutOfRange, .object = clip});
  Note assigned = makeNote(61, beat(0), beats(1));
  assigned.id = NoteId{made.fixture.project().nextId + 50};
  REQUIRE(refusal(document, AddNotes{.clip = made.clip, .notes = {assigned}}) ==
          Failure{.fault = Fault::IdInvalid, .object = clip});

  const NoteId stranger{made.fixture.project().nextId + 50};
  REQUIRE(refusal(document, RemoveNotes{.clip = made.clip,
                                        .notes = {ids.at(0), stranger}}) ==
          Failure{.fault = Fault::UnknownObject, .object = clip});
  REQUIRE(refusal(document, RemoveNotes{.clip = made.clip,
                                        .notes = {stranger, stranger}}) ==
          Failure{.fault = Fault::IdInvalid, .object = clip});
  Note unknown = made.notes().at(0);
  unknown.id = stranger;
  REQUIRE(refusal(document, SetNotes{.clip = made.clip, .notes = {unknown}}) ==
          Failure{.fault = Fault::UnknownObject, .object = clip});
  REQUIRE(refusal(document,
                  SetNotes{.clip = made.clip, .notes = {unknown, unknown}}) ==
          Failure{.fault = Fault::IdInvalid, .object = clip});
}

TEST_CASE("a note may end on the last storable tick but not past it",
          "[model][command][note]") {
  NoteFixture made = makeNoteFixture();
  Document &document = made.fixture.document;
  add(made, {makeNote(60, TickPosition{maxTicks - 10}, TickDuration{10})});
  REQUIRE(refused(document,
                  AddNotes{.clip = made.clip,
                           .notes = {makeNote(61, TickPosition{maxTicks - 10},
                                              TickDuration{11})}}) ==
          Fault::ValueOutOfRange);
  REQUIRE(refused(document,
                  AddNotes{.clip = made.clip,
                           .notes = {makeNote(61, TickPosition{maxTicks},
                                              TickDuration{maxTicks})}}) ==
          Fault::ValueOutOfRange);
}

TEST_CASE("many notes added one at a time stay in order",
          "[model][command][note]") {
  NoteFixture made = makeNoteFixture();
  Document &document = made.fixture.document;
  // Back to front, so that every note goes in before all the others.
  for (std::int64_t step = 199; step >= 0; --step) {
    const auto changes = document.perform(
        "Add",
        AddNotes{
            .clip = made.clip,
            .notes =
                {
                    makeNote(60, TickPosition{step * 10}, TickDuration{10}),
                },
        });
    REQUIRE(changes.has_value());
  }
  REQUIRE(made.notes().size() == 200);
  REQUIRE(made.notes().front().start == TickPosition{0});
  REQUIRE(made.notes().back().start == TickPosition{1'990});
  REQUIRE(validate(made.fixture.project()).empty());
}

TEST_CASE("controller data is set, replaced and removed per controller",
          "[model][command][controller]") {
  NoteFixture made = makeNoteFixture();
  Document &document = made.fixture.document;
  const auto controllers = [&made] -> const std::vector<ControllerEnvelope> & {
    return std::get<MidiContent>(
               findClip(made.fixture.project(), made.clip).clip->content)
        .controllers;
  };
  const auto data = [](ControllerKind kind, std::uint8_t controller,
                       std::vector<ControllerPoint> points) {
    return ControllerEnvelope{
        .kind = kind,
        .controller = controller,
        .points = std::move(points),
    };
  };
  const auto setting = [&made](ControllerEnvelope envelope) {
    return SetControllerData{.clip = made.clip, .data = std::move(envelope)};
  };

  const ControllerEnvelope sustain =
      data(ControllerKind::ControlChange, 64,
           {{.time = beat(0), .value = 127}, {.time = beat(2), .value = 0}});
  const ChangeSet changes = applied(document, setting(sustain));
  REQUIRE(changes.changes().size() == 1);
  REQUIRE(says(changes, made.clip, ChangeKind::Changed, Property::Controllers));
  REQUIRE(changes.find(made.clip)->parent == ObjectRef{made.fixture.midi});
  REQUIRE(controllers() == std::vector<ControllerEnvelope>{sustain});

  // Kept in order of kind, then controller number.
  const ControllerEnvelope modulation =
      data(ControllerKind::ControlChange, 1, {{.time = beat(1), .value = 64}});
  const ControllerEnvelope bend = data(
      ControllerKind::PitchBend, 0,
      {{.time = beat(0), .value = -8'192}, {.time = beat(1), .value = 8'191}});
  const ControllerEnvelope pressure =
      data(ControllerKind::ChannelPressure, 0, {{.time = beat(0), .value = 9}});
  applied(document, setting(modulation));
  applied(document, setting(bend));
  applied(document, setting(pressure));
  REQUIRE(controllers() ==
          std::vector<ControllerEnvelope>{bend, pressure, modulation, sustain});

  // Replacing one leaves the others.
  const ControllerEnvelope lessSustain =
      data(ControllerKind::ControlChange, 64, {{.time = beat(4), .value = 1}});
  applied(document, setting(lessSustain));
  REQUIRE(controllers().at(3) == lessSustain);
  REQUIRE(applied(document, setting(lessSustain)).empty());

  // Empty data removes the envelope; removing what is not there is no change.
  applied(document, setting(data(ControllerKind::ControlChange, 1, {})));
  REQUIRE(controllers() ==
          std::vector<ControllerEnvelope>{bend, pressure, lessSustain});
  REQUIRE(applied(document, setting(data(ControllerKind::ControlChange, 1, {})))
              .empty());
}

TEST_CASE("controller data is refused when it breaks a rule",
          "[model][command][controller]") {
  NoteFixture made = makeNoteFixture();
  Document &document = made.fixture.document;
  const auto setting = [&made](ControllerKind kind, std::uint8_t controller,
                               std::vector<ControllerPoint> points) {
    return SetControllerData{
        .clip = made.clip,
        .data =
            {
                .kind = kind,
                .controller = controller,
                .points = std::move(points),
            },
    };
  };
  const ControllerKind change = ControllerKind::ControlChange;

  REQUIRE(refused(document,
                  setting(change, 128, {{.time = beat(0), .value = 1}})) ==
          Fault::ValueOutOfRange);
  REQUIRE(refused(document, setting(ControllerKind::PitchBend, 1,
                                    {{.time = beat(0), .value = 1}})) ==
          Fault::ValueOutOfRange);
  REQUIRE(refused(document,
                  setting(change, 1, {{.time = beat(0), .value = 128}})) ==
          Fault::ValueOutOfRange);
  REQUIRE(refused(document, setting(ControllerKind::PitchBend, 0,
                                    {{.time = beat(0), .value = 8'192}})) ==
          Fault::ValueOutOfRange);
  REQUIRE(refused(document, setting(change, 1, {{.time = {-1}, .value = 1}})) ==
          Fault::ValueOutOfRange);
  REQUIRE(refused(document, setting(change, 1,
                                    {{.time = beat(2), .value = 1},
                                     {.time = beat(1), .value = 2}})) ==
          Fault::OrderViolation);
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): on purpose
  const auto outside = static_cast<ControllerKind>(7);
  REQUIRE(
      refused(document, setting(outside, 0, {{.time = beat(0), .value = 1}})) ==
      Fault::ValueOutOfRange);

  SetControllerData elsewhere =
      setting(change, 1, {{.time = beat(0), .value = 1}});
  elsewhere.clip = made.audioClip;
  REQUIRE(refused(document, elsewhere) == Fault::WrongClipKind);
  elsewhere.clip = ClipId{made.fixture.project().nextId};
  REQUIRE(refused(document, elsewhere) == Fault::UnknownObject);
}

} // namespace lina::model::testing
