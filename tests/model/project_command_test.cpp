// SPDX-License-Identifier: GPL-3.0-or-later
//
// Commands on the project's own properties, media references and scenes.
#include "support/builders.hpp"
#include "support/checks.hpp"

#include "lina/model/document.hpp"

#include <catch2/catch_test_macros.hpp>

#include <vector>

namespace lina::model::testing {

namespace {

// Puts a MIDI clip with two notes into the MIDI track's slot for scene A and
// returns the clip.
ClipId addSlotClip(Fixture &fixture) {
  const ChangeSet changes =
      applied(fixture.document,
              AddClip{
                  .container = fixture.slot(fixture.midi, fixture.sceneA),
                  .clip = makeMidiClip(beat(0), beats(4),
                                       {
                                           makeNote(60, beat(0), beats(1)),
                                           makeNote(62, beat(1), beats(1)),
                                       }),
              });
  return changes.firstAdded<ObjectKind::Clip>().value_or(ClipId{});
}

} // namespace

TEST_CASE("project properties are set, reported and undone",
          "[model][command][project]") {
  Document document;
  const Tempo tempo = Tempo::fromHundredths(9'050).value_or(Tempo{});
  const TimeSignature signature =
      TimeSignature::make(7, 8).value_or(TimeSignature{});
  REQUIRE(tempo.hundredths() == 9'050);
  REQUIRE(signature.numerator() == 7);

  ChangeSet changes = applied(document, SetProjectProperty{TempoValue{tempo}});
  REQUIRE(says(changes, projectRef, ChangeKind::Changed, Property::Tempo));
  REQUIRE(document.project().tempo == tempo);

  changes =
      applied(document, SetProjectProperty{TimeSignatureValue{signature}});
  REQUIRE(
      says(changes, projectRef, ChangeKind::Changed, Property::TimeSignature));
  REQUIRE(document.project().timeSignature == signature);

  const LoopRange loop{.enabled = true, .start = beat(4), .length = beats(8)};
  changes = applied(document, SetProjectProperty{LoopRangeValue{loop}});
  REQUIRE(says(changes, projectRef, ChangeKind::Changed, Property::LoopRange));
  REQUIRE(document.project().loop == loop);

  changes = applied(document, SetProjectProperty{StartMarkerValue{beat(2)}});
  REQUIRE(
      says(changes, projectRef, ChangeKind::Changed, Property::StartMarker));
  REQUIRE(document.project().startMarker == beat(2));

  changes = applied(document, SetProjectProperty{
                                  DefaultLaunchValue{
                                      LaunchQuantisation::Quarter,
                                  },
                              });
  REQUIRE(says(changes, projectRef, ChangeKind::Changed,
               Property::LaunchQuantisation));
  REQUIRE(document.project().launchQuantisation == LaunchQuantisation::Quarter);
}

TEST_CASE("setting a property to the value it has changes nothing and leaves "
          "no undo step",
          "[model][command][project]") {
  Document document;
  const ChangeSet changes =
      applied(document, SetProjectProperty{TempoValue{Tempo{}}});
  REQUIRE(changes.empty());
  REQUIRE(!document.canUndo());
}

TEST_CASE("project properties outside their range are refused",
          "[model][command][project]") {
  Document document;
  REQUIRE(
      refused(document, SetProjectProperty{LoopRangeValue{
                            {.enabled = true, .start = {0}, .length = {0}}}}) ==
      Fault::ValueOutOfRange);
  REQUIRE(refused(document,
                  SetProjectProperty{LoopRangeValue{
                      {.enabled = true, .start = {-1}, .length = {1}}}}) ==
          Fault::ValueOutOfRange);
  REQUIRE(refused(document, SetProjectProperty{StartMarkerValue{{-1}}}) ==
          Fault::ValueOutOfRange);
}

TEST_CASE("media references are added, relinked and removed",
          "[model][command][media]") {
  Document document;
  ChangeSet changes = applied(document, AddMedia{makeMedia("media/a.wav")});
  const MediaId a = changes.firstAdded<ObjectKind::Media>().value_or(MediaId{});
  REQUIRE(a.assigned());
  REQUIRE(says(changes, a, ChangeKind::Added));
  changes = applied(document, AddMedia{makeMedia("media/b.wav")});
  const MediaId b = changes.firstAdded<ObjectKind::Media>().value_or(MediaId{});
  changes = applied(document, AddMedia{makeMedia("media/c.wav")});
  REQUIRE(b.value > a.value);
  REQUIRE(document.project().media.size() == 3);
  REQUIRE(findMedia(document.project(), b)->path == "media/b.wav");

  changes = applied(document, RelinkMedia{.media = b, .path = "moved/b.wav"});
  REQUIRE(says(changes, b, ChangeKind::Changed, Property::Path));
  REQUIRE(findMedia(document.project(), b)->path == "moved/b.wav");
  REQUIRE(applied(document, RelinkMedia{.media = b, .path = "moved/b.wav"})
              .empty());

  // Removing the middle one and undoing puts it back in the middle (checked by
  // `applied`).
  changes = applied(document, RemoveMedia{b});
  REQUIRE(says(changes, b, ChangeKind::Removed));
  REQUIRE(findMedia(document.project(), b) == nullptr);
  REQUIRE(document.project().media.size() == 2);
  REQUIRE(document.undo().has_value());
  REQUIRE(document.project().media.at(1).id == b);
}

TEST_CASE("media commands are refused when they break a rule",
          "[model][command][media]") {
  Fixture fixture = makeFixture();
  Document &document = fixture.document;
  const MediaId unknown{fixture.project().nextId + 3};

  MediaReference assigned = makeMedia("media/x.wav");
  assigned.id = fixture.media;
  REQUIRE(refused(document, AddMedia{assigned}) == Fault::IdInvalid);
  REQUIRE(refused(document, AddMedia{makeMedia("")}) == Fault::ValueOutOfRange);
  MediaReference noRate = makeMedia("media/x.wav");
  noRate.sampleRate = 0;
  REQUIRE(refused(document, AddMedia{noRate}) == Fault::ValueOutOfRange);

  REQUIRE(refused(document, RemoveMedia{unknown}) == Fault::UnknownObject);
  REQUIRE(refused(document, RelinkMedia{.media = unknown, .path = "x"}) ==
          Fault::UnknownObject);
  REQUIRE(refused(document, RelinkMedia{.media = fixture.media, .path = ""}) ==
          Fault::ValueOutOfRange);

  SECTION("in use by a clip in a slot") {
    applied(document,
            AddClip{
                .container = fixture.slot(fixture.audio, fixture.sceneB),
                .clip = makeAudioClip(fixture.media, beat(0), beats(4)),
            });
    REQUIRE(refused(document, RemoveMedia{fixture.media}) == Fault::MediaInUse);
  }
  SECTION("in use by a clip in the arrangement") {
    applied(document,
            AddClip{
                .container = fixture.audio,
                .clip = makeAudioClip(fixture.media, beat(0), beats(4)),
            });
    REQUIRE(refused(document, RemoveMedia{fixture.media}) == Fault::MediaInUse);
  }
  SECTION("not in use while only MIDI clips exist") {
    addSlotClip(fixture);
    applied(document, AddClip{
                          .container = fixture.midi,
                          .clip = makeMidiClip(beat(0), beats(1)),
                      });
    REQUIRE(says(applied(document, RemoveMedia{fixture.media}), fixture.media,
                 ChangeKind::Removed));
  }
}

TEST_CASE("adding a scene gives every audio and MIDI track a slot in the same "
          "row",
          "[model][command][scene]") {
  Fixture fixture = makeFixture();
  const ChangeSet changes =
      applied(fixture.document, AddScene{.name = "Middle", .index = 1});
  const Project &project = fixture.project();
  const SceneId scene =
      changes.firstAdded<ObjectKind::Scene>().value_or(SceneId{});

  REQUIRE(project.scenes.size() == 3);
  REQUIRE(project.scenes.at(1).id == scene);
  REQUIRE(project.scenes.at(1).name == "Middle");
  REQUIRE(findScene(project, scene) == &project.scenes.at(1));
  // One scene and one slot per audio and MIDI track; nothing for the return or
  // the master.
  REQUIRE(changes.changes().size() == 3);
  for (const Track &track : project.tracks) {
    REQUIRE(track.slots.size() == 3);
    REQUIRE(track.slots.at(1).scene == scene);
    REQUIRE(!track.slots.at(1).clip.has_value());
    const Change *slot = changes.find(ObjectRef{track.slots.at(1).id});
    REQUIRE(slot != nullptr);
    REQUIRE(slot->kind == ChangeKind::Added);
    REQUIRE(slot->parent == ObjectRef{track.id});
  }
  REQUIRE(project.returns.at(0).slots.empty());
  REQUIRE(project.master.slots.empty());

  REQUIRE(refused(fixture.document, AddScene{.name = "Far", .index = 4}) ==
          Fault::IndexOutOfRange);
}

TEST_CASE("removing a scene removes its slots and their clips, and undo brings "
          "them back",
          "[model][command][scene]") {
  Fixture fixture = makeFixture();
  const ClipId clip = addSlotClip(fixture);
  const SlotId slot = fixture.slot(fixture.midi, fixture.sceneA);
  const std::vector<Note> notes =
      std::get<MidiContent>(findClip(fixture.project(), clip).clip->content)
          .notes;

  const ChangeSet changes =
      applied(fixture.document, RemoveScene{fixture.sceneA});
  REQUIRE(says(changes, fixture.sceneA, ChangeKind::Removed));
  REQUIRE(says(changes, slot, ChangeKind::Removed));
  REQUIRE(says(changes, clip, ChangeKind::Removed));
  REQUIRE(changes.find(clip)->parent == ObjectRef{slot});
  for (const Note &note : notes) {
    REQUIRE(says(changes, note.id, ChangeKind::Removed));
    REQUIRE(changes.find(note.id)->parent == ObjectRef{clip});
  }
  // Scene, two slots, clip, two notes.
  REQUIRE(changes.changes().size() == 6);
  REQUIRE(fixture.project().scenes.size() == 1);
  REQUIRE(findClip(fixture.project(), clip).clip == nullptr);
  REQUIRE(findSlot(fixture.project(), slot).slot == nullptr);

  REQUIRE(fixture.document.undo().has_value());
  REQUIRE(fixture.project().scenes.at(0).id == fixture.sceneA);
  REQUIRE(findClip(fixture.project(), clip).slot->id == slot);

  REQUIRE(refused(fixture.document,
                  RemoveScene{SceneId{fixture.project().nextId}}) ==
          Fault::UnknownObject);
}

TEST_CASE("moving a scene moves the row in every track",
          "[model][command][scene]") {
  Fixture fixture = makeFixture();
  const ClipId clip = addSlotClip(fixture);
  applied(fixture.document, AddScene{.name = "C", .index = 2});
  const SceneId sceneC = fixture.project().scenes.at(2).id;

  ChangeSet changes =
      applied(fixture.document, MoveScene{.scene = fixture.sceneA, .index = 2});
  REQUIRE(changes.changes().size() == 1);
  REQUIRE(says(changes, projectRef, ChangeKind::Changed, Property::SceneOrder));
  const Project &project = fixture.project();
  REQUIRE(project.scenes.at(0).id == fixture.sceneB);
  REQUIRE(project.scenes.at(1).id == sceneC);
  REQUIRE(project.scenes.at(2).id == fixture.sceneA);
  REQUIRE(findClip(project, clip).slot == &project.tracks.at(1).slots.at(2));

  changes = applied(fixture.document, MoveScene{.scene = sceneC, .index = 0});
  REQUIRE(project.scenes.at(0).id == sceneC);
  REQUIRE(project.scenes.at(1).id == fixture.sceneB);

  REQUIRE(applied(fixture.document, MoveScene{.scene = sceneC, .index = 0})
              .empty());
  REQUIRE(refused(fixture.document, MoveScene{.scene = sceneC, .index = 3}) ==
          Fault::IndexOutOfRange);
  REQUIRE(refused(fixture.document,
                  MoveScene{.scene = SceneId{project.nextId}, .index = 0}) ==
          Fault::UnknownObject);
}

TEST_CASE("scenes are renamed", "[model][command][scene]") {
  Fixture fixture = makeFixture();
  const ChangeSet changes = applied(
      fixture.document, RenameScene{.scene = fixture.sceneA, .name = "Intro"});
  REQUIRE(says(changes, fixture.sceneA, ChangeKind::Changed, Property::Name));
  REQUIRE(findScene(fixture.project(), fixture.sceneA)->name == "Intro");
  REQUIRE(applied(fixture.document,
                  RenameScene{.scene = fixture.sceneA, .name = "Intro"})
              .empty());
  REQUIRE(refused(fixture.document,
                  RenameScene{.scene = SceneId{fixture.project().nextId},
                              .name = "x"}) == Fault::UnknownObject);
}

TEST_CASE("a launch quantisation that is not an enumerator is refused",
          "[model][command][project]") {
  Document document;
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): on purpose
  const auto outside = static_cast<LaunchQuantisation>(200);
  REQUIRE(refused(document, SetProjectProperty{DefaultLaunchValue{outside}}) ==
          Fault::ValueOutOfRange);
  REQUIRE(applied(document, SetProjectProperty{DefaultLaunchValue{
                                LaunchQuantisation::None}})
              .changes()
              .size() == 1);
}

TEST_CASE("ranges may end on the last storable tick but not past it",
          "[model][command][project]") {
  Document document;
  applied(document,
          SetProjectProperty{
              LoopRangeValue{
                  {.enabled = true, .start = {0}, .length = {maxTicks}},
              },
          });
  applied(document,
          SetProjectProperty{
              LoopRangeValue{
                  {.enabled = true, .start = {maxTicks - 1}, .length = {1}},
              },
          });
  REQUIRE(
      refused(document,
              SetProjectProperty{LoopRangeValue{
                  {.enabled = true, .start = {maxTicks}, .length = {1}}}}) ==
      Fault::ValueOutOfRange);
  REQUIRE(
      refused(document,
              SetProjectProperty{LoopRangeValue{
                  {.enabled = true, .start = {1}, .length = {maxTicks}}}}) ==
      Fault::ValueOutOfRange);
  applied(document, SetProjectProperty{StartMarkerValue{{maxTicks}}});
  REQUIRE(refused(document, SetProjectProperty{StartMarkerValue{
                                {maxTicks + 1}}}) == Fault::ValueOutOfRange);
}

TEST_CASE("a refused media reference names the project",
          "[model][command][media]") {
  Document document;
  MediaReference assigned = makeMedia("media/x.wav");
  assigned.id = MediaId{document.project().nextId + 9};
  REQUIRE(refusal(document, AddMedia{assigned}) ==
          Failure{.fault = Fault::IdInvalid, .object = projectRef});
  REQUIRE(refusal(document, AddMedia{makeMedia("")}) ==
          Failure{.fault = Fault::ValueOutOfRange, .object = projectRef});
}

TEST_CASE("an identity once used is never handed out again, whatever is "
          "undone",
          "[model][command][project]") {
  Fixture fixture = makeFixture();
  Document &document = fixture.document;
  const ClipId clip = applied(document,
                              AddClip{
                                  .container = fixture.midi,
                                  .clip = makeMidiClip(beat(0), beats(4)),
                              })
                          .firstAdded<ObjectKind::Clip>()
                          .value_or(ClipId{});
  applied(document, RemoveClip{clip});
  REQUIRE(document.undo().has_value());
  REQUIRE(document.undo().has_value());
  // The clip's identity is retired; what is added next gets a fresh one.
  const MediaId media = applied(document, AddMedia{makeMedia("media/y.wav")})
                            .firstAdded<ObjectKind::Media>()
                            .value_or(MediaId{});
  REQUIRE(media.value > clip.value);
}

} // namespace lina::model::testing
