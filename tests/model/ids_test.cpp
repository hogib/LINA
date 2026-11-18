// SPDX-License-Identifier: GPL-3.0-or-later
#include "lina/model/ids.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <type_traits>

namespace lina::model {

TEST_CASE("identities are typed by the kind of object they name",
          "[model][ids]") {
  // A clip's identity cannot stand in for a track's, in either direction.
  STATIC_REQUIRE(!std::is_convertible_v<ClipId, TrackId>);
  STATIC_REQUIRE(!std::is_convertible_v<TrackId, ClipId>);
  STATIC_REQUIRE(!std::is_convertible_v<std::uint64_t, NoteId>);
  STATIC_REQUIRE(sizeof(TrackId) == sizeof(std::uint64_t));

  REQUIRE(!TrackId{}.assigned());
  REQUIRE(TrackId{7}.assigned());
  REQUIRE(TrackId{7} == TrackId{7});
  REQUIRE(TrackId{7} != TrackId{8});
  REQUIRE(TrackId{7} < TrackId{8});
}

TEST_CASE("an erased identity keeps the kind and orders by kind first",
          "[model][ids]") {
  const std::uint64_t number = 5;
  const ObjectRef erased{ClipId{number}};
  REQUIRE(erased.kind == ObjectKind::Clip);
  REQUIRE(erased.id == number);
  REQUIRE(erased == ObjectRef{ObjectKind::Clip, number});
  // The same number under another kind is another object.
  REQUIRE(erased != ObjectRef{ObjectKind::Note, number});

  // Containers sort before their contents whatever the numbers are.
  REQUIRE(ObjectRef{TrackId{90}} < ObjectRef{SlotId{3}});
  REQUIRE(ObjectRef{SlotId{90}} < ObjectRef{ClipId{3}});
  REQUIRE(ObjectRef{ClipId{90}} < ObjectRef{NoteId{3}});
  REQUIRE(ObjectRef{ClipId{3}} < ObjectRef{ClipId{4}});
  REQUIRE(projectRef < ObjectRef{MediaId{1}});
  REQUIRE(ObjectRef{} == projectRef);
}

} // namespace lina::model
