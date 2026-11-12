# 03 Glossary and domain model

Status: **draft**. The names "session" and "arrangement" were decided by the project owner on
2026-11-13. Every other name is *proposed* until agreed. The audience has not used Live (doc 02),
so concept terms are chosen to be self-explanatory or standard across DAWs. Names marked
**(name open)** are placeholders. Device and tool names follow the silly-names rule in doc 01 and
are not listed here.

This doc defines what things are and how they contain each other. How they behave is specified in
docs 04 to 09.

## Containment

```
Project
├── Transport settings (tempo map, time signature map, loop range)
├── Tracks (ordered)
│   ├── Slots (one per scene)        → each holds at most one Clip
│   ├── Arrangement clips (ordered by position, non-overlapping per track)
│   ├── Device chain (ordered Devices)
│   ├── Mixer strip (volume, pan, mute, solo, sends, routing)
│   └── Automation (one envelope per automated Parameter)
├── Scenes (ordered)
├── Return tracks
├── Master track
└── Media references (audio files the project uses)
```

## Terms

### Project level

- **Project.** Everything saved together as one piece of work. The unit of save, load and undo
  history.
- **Transport.** The project's playback state: playing or stopped, current position, tempo, time
  signature, loop range, metronome.
- **Musical time.** Position measured in bars and beats. It maps to seconds through the tempo map.
- **Media reference.** A pointer from the project to an audio file on disk, with enough
  information to detect that the file is missing or has changed.

### Tracks

- **Track.** A horizontal lane in the arrangement and a column in the session, with one device
  chain and one mixer strip. A track plays at most one clip at any moment.
- **Audio track.** A track whose clips contain audio and whose input is an audio signal.
- **MIDI track.** A track whose clips contain notes. It produces sound only if its device chain
  starts with an instrument.
- **Group track.** A track that contains other tracks and receives their summed output.
- **Return track.** A track with no clips that receives signal from other tracks' sends, for
  shared effects.
- **Master track.** The final track that every other track's output reaches unless routed
  elsewhere.

### Clips

- **Clip.** A bounded piece of musical material with its own length, loop settings and name.
  A clip lives either in a slot or in the arrangement.
- **Audio clip.** A clip that plays a region of a media reference, with settings that decide how
  it follows the project tempo.
- **MIDI clip.** A clip that contains notes and per-note data.
- **Note.** Pitch, start, length and velocity inside a MIDI clip.
- **Warp marker.** A point that pins a position in an audio file to a position in musical time.
  Not editable in the first release (doc 02).

### The session

- **Session.** The view and data structure of slots arranged as tracks by scenes, used for
  launching clips in time with each other.
- **Slot.** One cell of the session, belonging to one track and one scene. Empty, or holding one
  clip.
- **Scene.** One row of the session. Launching a scene launches every clip in that row.
- **Launch.** To ask a clip, or a scene, to start playing. The start happens at the next launch
  boundary, not necessarily at once.
- **Launch quantisation.** The musical interval (for example one bar) whose boundaries launches
  wait for.
- **Follow action.** A rule on a clip that launches another clip automatically when it has played
  for a set time.

### The arrangement

- **Arrangement.** The view and data structure of clips placed at fixed positions in musical
  time, played from left to right.
- **Start marker.** The position in the arrangement that play starts from.
- **Automation.** A parameter's value recorded or drawn as a function of time.
- **Envelope.** The curve that stores automation for one parameter.

### Devices and mixing

- **Device.** Anything in a track's chain that produces or processes signal: a built-in device or
  a hosted plugin. LINA treats both the same way from the track's point of view.
- **Instrument.** A device that turns notes into audio.
- **Effect.** A device that processes audio (audio effect) or notes (note effect).
- **Device chain.** The ordered list of devices on a track. Signal flows from first to last.
- **Device group (name open).** A device that contains parallel chains and exposes a small set of
  mapped controls for them.
- **Parameter.** A single controllable value of a device or mixer strip. Parameters can be
  automated and mapped to controllers.
- **Mixer strip.** A track's volume, pan, mute, solo, send levels and input/output routing.
- **Send.** A level control that feeds a copy of a track's signal to a return track.

## Rules of the model

These are structural rules the rest of the docs rely on. All `[own design]` unless tagged.

1. A clip belongs to exactly one container: one slot, or one track's arrangement. Copying between
   the session and the arrangement produces an independent clip.
2. Every track has exactly as many slots as the project has scenes.
3. Arrangement clips on one track do not overlap.
4. A track plays from the session or from the arrangement at any moment, never both.
   `[public: unverified]` as a description of how Live resolves this; the details are in docs
   05 and 06.
5. Tracks, scenes, clips and devices have stable identities that survive reordering and renaming,
   so that automation, routing and undo refer to the thing and not to its position.
6. Nothing outside the project file and its media references is needed to reopen a project,
   apart from the plugins it uses.

## Open questions

1. A name for the parallel-chain container ("device group" above).
2. Do group tracks have slots of their own (acting as a launcher for their children's row), or
   none?
3. Are note effects (devices that transform notes before the instrument) part of the model from
   the start?
