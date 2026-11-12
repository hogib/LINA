# 05 Session workflow

Status: **draft**. Terms are from doc 03, timing rules from doc 04, source tags from doc 01.
**(R1)** marks what the first release needs. Items marked *proposed* are suggestions waiting for a
yes or no. Unless tagged otherwise, statements are `[own design]`: they describe the clip-launching
workflow common to several DAWs and hardware loopers, written for people who have not used it.

## What the session is for

The session lets a musician try combinations of loops and phrases without committing to an order.
Each track offers a column of clips; the musician picks which one plays on each track, and LINA
keeps them all in time.

## Structure

1. **(R1)** The session is a table. Columns are tracks, rows are scenes, cells are slots.
2. **(R1)** The user can add, delete, duplicate, reorder and rename scenes. Adding a scene adds
   one empty slot to every track.
3. **(R1)** Return tracks and the master track appear as columns for mixing but have no slots.

## Slot and clip states

Every slot shows which of these states it is in, at a glance and without relying on colour alone:

| State | Meaning |
|---|---|
| Empty | No clip. |
| Stopped | Has a clip that is not playing. |
| Queued | Launch requested; waiting for the launch boundary. |
| Playing | The clip is sounding. Shows progress through the clip. |
| Stopping | Stop requested; waiting for the launch boundary. |
| Record queued | Recording requested; waiting for the launch boundary. |
| Recording | Material is being recorded into the slot. |

**(R1)** All seven states.

## Launching and stopping

1. **(R1)** Launching a clip puts it in the queued state. At the next launch boundary (doc 04) it
   starts playing from its start position.
2. **(R1)** A track plays one clip at a time. If another clip on the same track is playing when a
   queued clip reaches its boundary, the playing clip stops at that boundary and the new one
   starts, with no gap and no overlap.
3. **(R1)** Launching a clip that is already queued does nothing. Launching a clip that is
   already playing restarts it at the next boundary.
4. **(R1)** Each track has a stop control that stops whatever clip is playing on it at the next
   boundary. A "stop all clips" control does the same for every track.
5. **(R1)** A queued launch can be cancelled before its boundary by stopping the track.
6. **(R1)** The start of a launched clip is sample-accurate on the boundary. Interface load, disk
   activity or a slow plugin window never move it.
7. **(R1)** If the transport is stopped, launching a clip starts the transport and the clip
   starts immediately at the start marker position.
8. Later: launch modes other than "start and keep playing", such as playing only while held, or
   toggling on repeated launches.
9. Later: starting a newly launched clip at the position the previous clip had reached, instead
   of from its start, so that switching clips mid-phrase keeps the phrase position.

## Clip playback settings

1. **(R1)** A clip has a start position, a loop switch, and a loop start and loop length.
2. **(R1)** With the loop switch on, the clip repeats its loop range until stopped or replaced.
3. **(R1)** With the loop switch off, the clip plays once to its end and the track then falls
   silent; the slot returns to the stopped state.
4. **(R1)** A clip may override the project's launch quantisation with its own value.
5. **(R1)** Changing a playing clip's loop range takes effect without stopping the clip.

## Scenes

1. **(R1)** Launching a scene launches every clip in its row at the same launch boundary, using
   the project's launch quantisation. Per-clip overrides are ignored for scene launches so that
   the row starts together. *Proposed.*
2. **(R1)** For tracks whose slot in that scene is empty, launching the scene stops the clip
   currently playing on that track. A scene means "exactly this row". Decided 2026-11-13.
3. **(R1)** After a scene is launched, the next scene becomes selected, so that repeatedly
   pressing one key walks down the rows. *Proposed.*
4. Later: a scene may carry a tempo and time signature (doc 04).

## Session and arrangement on the same track

1. **(R1)** A track plays from the session or from the arrangement, never both (doc 03, rule 4).
2. **(R1)** When a session clip starts on a track, that track's arrangement playback is
   suspended. Other tracks are unaffected.
3. **(R1)** Stopping the track's session clip leaves the track silent; it does not hand the track
   back to the arrangement by itself. `[public: unverified]` that Live behaves this way; kept
   here because it prevents arrangement material from appearing unexpectedly mid-performance.
4. **(R1)** A clearly visible "back to arrangement" control appears whenever any track is
   suspended. It exists per track and for all tracks at once. Using it resumes arrangement
   playback at the current playhead position.
5. **(R1)** The interface always makes it plain which tracks are suspended. A newcomer who hears
   silence where they expect their arrangement must be able to see why.

## Transport stop and restart

1. **(R1)** Stopping the transport stops all session clips.
2. **(R1)** LINA remembers which clips were playing when the transport stopped. Starting the
   transport again relaunches them from their start positions. "Stop all clips" clears that
   memory. *Proposed*; see open question 2.

## Editing in the session

1. **(R1)** Clips can be moved and copied between slots by dragging, duplicated, deleted,
   renamed and given a colour. Dropping a clip on an occupied slot replaces the clip there.
2. **(R1)** A clip can be dragged from the session into the arrangement and the other way.
   The result is an independent copy (doc 03, rule 1).
3. **(R1)** Dropping an audio file on a slot of an audio track creates an audio clip there.
4. **(R1)** An empty MIDI clip can be created in an empty slot of a MIDI track.
5. **(R1)** Selecting a clip shows its contents in the clip editor (docs 07 and 08).
6. **(R1)** Every one of these edits is undoable.

## Recording into slots

1. **(R1)** A track must be armed to record. Arming chooses its input (doc 09).
2. **(R1)** Requesting record on an empty slot of an armed track puts the slot in the record
   queued state; recording starts at the next launch boundary. If the transport is stopped, the
   count-in (doc 04) plays first.
3. **(R1)** Ending the recording, by launching the slot again or stopping the track, takes
   effect at the next launch boundary. The new clip's length is therefore a whole number of
   launch quantisation units, and it starts looping immediately if it was ended by launching.
   *Proposed.*
4. **(R1)** With launch quantisation set to none, recording starts and ends at the moment of the
   request, and the clip's length is whatever was played.
5. **(R1)** Recorded audio is written to disk as it arrives. A crash during recording loses at
   most the last few seconds (doc 10).
6. Later: overdubbing more notes into a playing MIDI clip.
7. Later: recording into several armed tracks' slots in one scene with one action.

## Later

- Follow actions (doc 03).
- Capturing a session performance into the arrangement: recording which clips were launched and
  when, as arrangement clips. Decided 2026-11-13: not in the first release; dragging clips
  between the views is the bridge until then.
- Launching from hardware controllers.

## Open questions

1. Should restarting the transport relaunch the clips that were playing (proposed), or start with
   every track silent until something is launched?
