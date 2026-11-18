// SPDX-License-Identifier: GPL-3.0-or-later
#include "lina/model/change_set.hpp"

#include <catch2/catch_test_macros.hpp>

namespace lina::model {

namespace {

constexpr ObjectRef trackA{TrackId{10}};
constexpr ObjectRef trackB{TrackId{11}};
constexpr ObjectRef clip{ClipId{20}};
constexpr ObjectRef note{NoteId{5}};

} // namespace

TEST_CASE("a property set holds the properties put into it",
          "[model][changes]") {
  PropertySet set;
  REQUIRE(set.empty());
  set |= Property::Volume;
  set |= Property::Path;
  REQUIRE(!set.empty());
  REQUIRE(set.contains(Property::Volume));
  REQUIRE(set.contains(Property::Path));
  REQUIRE(!set.contains(Property::Pan));
  REQUIRE(set == (PropertySet{Property::Path} | Property::Volume));
  REQUIRE(PropertySet::all().contains(Property::Name));
  REQUIRE(PropertySet::all().contains(Property::Path));
}

TEST_CASE("a change set is sorted by kind then identity and finds its entries",
          "[model][changes]") {
  ChangeSet changes;
  REQUIRE(changes.empty());
  changes.added(note, clip);
  changes.added(clip, trackB);
  changes.added(trackB, projectRef);
  changes.changed(trackA, projectRef, Property::Name);
  changes.changed(projectRef, projectRef, Property::Tempo);

  // Containers come before their contents although the identities say
  // otherwise.
  REQUIRE(changes.changes().size() == 5);
  REQUIRE(changes.changes().at(0).object == projectRef);
  REQUIRE(changes.changes().at(1).object == trackA);
  REQUIRE(changes.changes().at(2).object == trackB);
  REQUIRE(changes.changes().at(3).object == clip);
  REQUIRE(changes.changes().at(4).object == note);

  const Change *found = changes.find(clip);
  REQUIRE(found != nullptr);
  REQUIRE(found->kind == ChangeKind::Added);
  REQUIRE(found->parent == trackB);
  REQUIRE(found->previousParent == trackB);
  REQUIRE(found->properties.empty());
  REQUIRE(changes.find(ObjectRef{ClipId{21}}) == nullptr);
  REQUIRE(changes.find(ObjectRef{NoteId{99}}) == nullptr);

  REQUIRE(changes.firstAdded<ObjectKind::Track>() == TrackId{11});
  REQUIRE(changes.firstAdded<ObjectKind::Note>() == NoteId{5});
  REQUIRE(!changes.firstAdded<ObjectKind::Scene>().has_value());
}

TEST_CASE("changes to one object fold into one entry", "[model][changes]") {
  ChangeSet changes;

  SECTION("changed twice: the properties add up") {
    changes.changed(trackA, projectRef, Property::Volume);
    changes.changed(trackA, projectRef, Property::Pan);
    REQUIRE(changes.changes().size() == 1);
    const Change *found = changes.find(trackA);
    REQUIRE(found != nullptr);
    REQUIRE(found->kind == ChangeKind::Changed);
    REQUIRE(found->properties ==
            (PropertySet{Property::Volume} | Property::Pan));
  }

  SECTION("added then changed: still added, in its latest container") {
    changes.added(clip, trackA);
    changes.moved(clip, trackA, trackB, Property::Container);
    const Change *found = changes.find(clip);
    REQUIRE(found != nullptr);
    REQUIRE(found->kind == ChangeKind::Added);
    REQUIRE(found->parent == trackB);
    REQUIRE(found->previousParent == trackB);
    REQUIRE(found->properties.empty());
  }

  SECTION("added then removed: as if it never was") {
    changes.added(clip, trackA);
    changes.removed(clip, trackA);
    REQUIRE(changes.empty());
  }

  SECTION("changed then removed: removed from where it was at first") {
    changes.moved(clip, trackA, trackB, Property::Container);
    changes.removed(clip, trackB);
    const Change *found = changes.find(clip);
    REQUIRE(found != nullptr);
    REQUIRE(found->kind == ChangeKind::Removed);
    REQUIRE(found->parent == trackA);
    REQUIRE(found->previousParent == trackA);
    REQUIRE(found->properties.empty());
  }

  SECTION("removed then put back: changed in every respect") {
    changes.removed(clip, trackA);
    changes.added(clip, trackB);
    const Change *found = changes.find(clip);
    REQUIRE(found != nullptr);
    REQUIRE(found->kind == ChangeKind::Changed);
    REQUIRE(found->previousParent == trackA);
    REQUIRE(found->parent == trackB);
    REQUIRE(found->properties == PropertySet::all());
  }

  SECTION("moved twice: first container to last") {
    const ObjectRef trackC{TrackId{12}};
    changes.moved(clip, trackA, trackB, Property::Container);
    changes.moved(clip, trackB, trackC,
                  PropertySet{Property::Container} | Property::Extent);
    const Change *found = changes.find(clip);
    REQUIRE(found != nullptr);
    REQUIRE(found->previousParent == trackA);
    REQUIRE(found->parent == trackC);
    REQUIRE(found->properties ==
            (PropertySet{Property::Container} | Property::Extent));
  }
}

TEST_CASE("merging folds a later change set into an earlier one",
          "[model][changes]") {
  ChangeSet earlier;
  earlier.added(clip, trackA);
  earlier.changed(trackA, projectRef, Property::Name);

  ChangeSet later;
  later.removed(clip, trackA);
  later.changed(trackA, projectRef, Property::Colour);
  later.added(note, clip);

  earlier.merge(later);
  REQUIRE(earlier.find(clip) == nullptr);
  REQUIRE(earlier.changes().size() == 2);
  const Change *track = earlier.find(trackA);
  REQUIRE(track != nullptr);
  REQUIRE(track->properties ==
          (PropertySet{Property::Name} | Property::Colour));
  REQUIRE(earlier.find(note) != nullptr);

  ChangeSet same;
  same.changed(trackA, projectRef,
               PropertySet{Property::Name} | Property::Colour);
  same.added(note, clip);
  REQUIRE(earlier == same);
}

} // namespace lina::model
