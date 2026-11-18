// SPDX-License-Identifier: GPL-3.0-or-later
//
// Commands on automation envelopes.
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

AddEnvelope adding(TrackId track, AutomationTarget target,
                   std::vector<AutomationPoint> points = {}) {
  return {
      .track = track,
      .envelope =
          {
              .id = EnvelopeId{0},
              .target = std::move(target),
              .points = std::move(points),
          },
  };
}

EnvelopeId add(Document &document, TrackId track, AutomationTarget target,
               std::vector<AutomationPoint> points = {}) {
  const ChangeSet changes =
      applied(document, adding(track, std::move(target), std::move(points)));
  REQUIRE(changes.changes().size() == 1);
  const EnvelopeId id =
      changes.firstAdded<ObjectKind::Envelope>().value_or(EnvelopeId{});
  REQUIRE(changes.find(id)->parent == ObjectRef{track});
  return id;
}

constexpr MixerTarget volume{MixerParameter::Volume};
constexpr MixerTarget pan{MixerParameter::Pan};

} // namespace

TEST_CASE("envelopes are added to the parameters of a track",
          "[model][command][automation]") {
  Fixture fixture = makeFixture();
  Document &document = fixture.document;
  const Project &project = fixture.project();
  const DeviceId filter = applied(document,
                                  AddDevice{
                                      .track = fixture.audio,
                                      .device = makeEffect("filter"),
                                      .index = 0,
                                  })
                              .firstAdded<ObjectKind::Device>()
                              .value_or(DeviceId{});
  const std::vector<AutomationPoint> fade{
      {.time = beat(0), .value = silenceDb},
      {.time = beat(4), .value = maxVolumeDb},
      // A step: two points at one time.
      {.time = beat(4), .value = 0.0},
  };

  const EnvelopeId onVolume = add(document, fixture.audio, volume, fade);
  const EnvelopeId onPan =
      add(document, fixture.audio, pan,
          {{.time = beat(0), .value = -1.0}, {.time = beat(8), .value = 1.0}});
  const EnvelopeId onSend =
      add(document, fixture.audio, SendTarget{fixture.returnTrack},
          {
              {.time = beat(0), .value = silenceDb},
              {.time = beat(8), .value = maxSendDb},
          });
  const EnvelopeId onParameter =
      add(document, fixture.audio,
          DeviceParameterTarget{.device = filter, .parameter = "cutoff"},
          {{.time = beat(0), .value = 0.0}, {.time = beat(8), .value = 1.0}});

  const std::vector<Envelope> &automation = project.tracks.at(0).automation;
  REQUIRE(automation.size() == 4);
  REQUIRE(automation.at(0).id == onVolume);
  REQUIRE(automation.at(0).points == fade);
  REQUIRE(automation.at(1).id == onPan);
  REQUIRE(automation.at(2).id == onSend);
  REQUIRE(automation.at(3).id == onParameter);
  const EnvelopeLocation location = findEnvelope(project, onSend);
  REQUIRE(location.track->id == fixture.audio);
  REQUIRE(location.envelope == &automation.at(2));
  REQUIRE(findEnvelope(project, EnvelopeId{project.nextId}).envelope ==
          nullptr);

  // Return tracks and the master have parameters too.
  add(document, fixture.returnTrack, pan);
  add(document, project.master.id, volume);
}

TEST_CASE("an envelope is refused when its target is not there, is taken or "
          "its points do not fit",
          "[model][command][automation]") {
  Fixture fixture = makeFixture();
  Document &document = fixture.document;
  const Project &project = fixture.project();
  const ObjectRef track{fixture.audio};
  const DeviceId onMidi = applied(document,
                                  AddDevice{
                                      .track = fixture.midi,
                                      .device = makeEffect("delay"),
                                      .index = 0,
                                  })
                              .firstAdded<ObjectKind::Device>()
                              .value_or(DeviceId{});
  const auto point = [](double value) {
    return std::vector<AutomationPoint>{{.time = beat(0), .value = value}};
  };

  REQUIRE(refused(document, adding(TrackId{project.nextId}, volume)) ==
          Fault::UnknownObject);
  // Every failure names the track: the envelope has no identity yet.
  REQUIRE(refusal(document, adding(fixture.audio, SendTarget{fixture.midi})) ==
          Failure{.fault = Fault::EnvelopeTarget, .object = track});
  REQUIRE(refused(document, adding(fixture.returnTrack,
                                   SendTarget{fixture.returnTrack})) ==
          Fault::EnvelopeTarget);
  REQUIRE(refused(document, adding(project.master.id, pan)) ==
          Fault::EnvelopeTarget);
  REQUIRE(
      refused(document, adding(fixture.audio,
                               DeviceParameterTarget{.device = onMidi,
                                                     .parameter = "time"})) ==
      Fault::EnvelopeTarget);
  REQUIRE(refused(document, adding(fixture.midi,
                                   DeviceParameterTarget{.device = onMidi,
                                                         .parameter = ""})) ==
          Fault::EnvelopeTarget);
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): on purpose
  const auto outside = static_cast<MixerParameter>(5);
  REQUIRE(refused(document, adding(fixture.audio, MixerTarget{outside})) ==
          Fault::ValueOutOfRange);

  // Values outside the range of each kind of target.
  REQUIRE(refusal(document, adding(fixture.audio, volume, point(6.5))) ==
          Failure{.fault = Fault::ValueOutOfRange, .object = track});
  REQUIRE(refused(document, adding(fixture.audio, volume, point(-96.5))) ==
          Fault::ValueOutOfRange);
  REQUIRE(refused(document, adding(fixture.audio, pan, point(1.5))) ==
          Fault::ValueOutOfRange);
  REQUIRE(refused(document, adding(fixture.audio, pan, point(-1.5))) ==
          Fault::ValueOutOfRange);
  REQUIRE(
      refused(document, adding(fixture.audio, SendTarget{fixture.returnTrack},
                               point(0.5))) == Fault::ValueOutOfRange);
  REQUIRE(refused(document, adding(fixture.midi,
                                   DeviceParameterTarget{.device = onMidi,
                                                         .parameter = "time"},
                                   point(1.5))) == Fault::ValueOutOfRange);
  REQUIRE(refused(document,
                  adding(fixture.audio, volume,
                         point(std::numeric_limits<double>::quiet_NaN()))) ==
          Fault::ValueOutOfRange);
  REQUIRE(refused(document, adding(fixture.audio, volume,
                                   {{.time = {-1}, .value = 0.0}})) ==
          Fault::ValueOutOfRange);
  REQUIRE(refused(document, adding(fixture.audio, volume,
                                   {{.time = beat(2), .value = 0.0},
                                    {.time = beat(1), .value = 0.0}})) ==
          Fault::OrderViolation);

  AddEnvelope assigned = adding(fixture.audio, volume);
  assigned.envelope.id = EnvelopeId{project.nextId + 30};
  REQUIRE(refusal(document, assigned) ==
          Failure{.fault = Fault::IdInvalid, .object = track});

  // One envelope per parameter.
  add(document, fixture.audio, volume);
  REQUIRE(refusal(document, adding(fixture.audio, volume)) ==
          Failure{.fault = Fault::EnvelopeTarget, .object = track});
  add(document, fixture.midi, volume);
}

TEST_CASE("envelope points are replaced as a whole",
          "[model][command][automation]") {
  Fixture fixture = makeFixture();
  Document &document = fixture.document;
  const EnvelopeId id = add(document, fixture.audio, pan);
  const auto points = [&] {
    return findEnvelope(fixture.project(), id).envelope->points;
  };
  const std::vector<AutomationPoint> sweep{
      {.time = beat(0), .value = -1.0},
      {.time = beat(2), .value = -0.0},
      {.time = beat(4), .value = 1.0},
  };

  const ChangeSet changes =
      applied(document, SetEnvelopePoints{.envelope = id, .points = sweep});
  REQUIRE(changes.changes().size() == 1);
  REQUIRE(says(changes, id, ChangeKind::Changed, Property::Points));
  REQUIRE(changes.find(id)->parent == ObjectRef{fixture.audio});
  REQUIRE(points() == sweep);
  // Negative zero went in as zero, so the same list again is no change.
  REQUIRE(!std::signbit(points().at(1).value));
  REQUIRE(applied(document, SetEnvelopePoints{.envelope = id, .points = sweep})
              .empty());

  applied(document, SetEnvelopePoints{.envelope = id, .points = {}});
  REQUIRE(points().empty());

  REQUIRE(refused(document, SetEnvelopePoints{.envelope = id,
                                              .points = {{.time = beat(0),
                                                          .value = 1.5}}}) ==
          Fault::ValueOutOfRange);
  REQUIRE(
      refused(document,
              SetEnvelopePoints{.envelope = id,
                                .points = {{.time = beat(1), .value = 0.0},
                                           {.time = beat(0), .value = 0.0}}}) ==
      Fault::OrderViolation);
  REQUIRE(refused(document,
                  SetEnvelopePoints{.envelope =
                                        EnvelopeId{fixture.project().nextId},
                                    .points = {}}) == Fault::UnknownObject);
  REQUIRE(points().empty());
}

TEST_CASE("envelopes are removed and undo puts them back in place",
          "[model][command][automation]") {
  Fixture fixture = makeFixture();
  Document &document = fixture.document;
  const Project &project = fixture.project();
  const EnvelopeId first = add(document, fixture.audio, volume);
  const EnvelopeId second =
      add(document, fixture.audio, pan, {{.time = beat(1), .value = 0.5}});
  const EnvelopeId third =
      add(document, fixture.audio, SendTarget{fixture.returnTrack});

  const ChangeSet changes = applied(document, RemoveEnvelope{second});
  REQUIRE(changes.changes().size() == 1);
  REQUIRE(says(changes, second, ChangeKind::Removed));
  REQUIRE(changes.find(second)->parent == ObjectRef{fixture.audio});
  REQUIRE(project.tracks.at(0).automation.size() == 2);
  REQUIRE(findEnvelope(project, second).envelope == nullptr);

  REQUIRE(document.undo().has_value());
  const std::vector<Envelope> &automation = project.tracks.at(0).automation;
  REQUIRE(automation.at(0).id == first);
  REQUIRE(automation.at(1).id == second);
  REQUIRE(automation.at(1).points.size() == 1);
  REQUIRE(automation.at(2).id == third);

  // The parameter is free again once its envelope is gone.
  applied(document, RemoveEnvelope{second});
  add(document, fixture.audio, pan);
  REQUIRE(refused(document, RemoveEnvelope{second}) == Fault::UnknownObject);
}

} // namespace lina::model::testing
