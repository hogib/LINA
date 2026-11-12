# 06 Arrangement workflow

Status: **draft**. Terms are from doc 03, timing rules from doc 04, source tags from doc 01.
**(R1)** marks what the first release needs. Items marked *proposed* are suggestions waiting for a
yes or no. Unless tagged otherwise, statements are `[own design]` and describe timeline editing
as it is commonly done across DAWs.

## What the arrangement is for

The arrangement is where a piece gets its final order. Clips sit at fixed positions on tracks and
play from left to right. It is also what gets exported.

## Structure and navigation

1. **(R1)** Tracks are rows, time runs left to right, with a ruler showing bars and beats.
2. **(R1)** Zoom horizontally around the pointer; scroll in both directions; resize track height.
3. **(R1)** An option makes the view follow the playhead during playback. Any manual scroll
   pauses following until playback restarts or the user re-enables it.
4. **(R1)** Clicking in the arrangement sets the start marker (doc 04).
5. Later: named markers the user can jump between.

## Tracks

1. **(R1)** Add, delete, duplicate, reorder, rename and colour tracks. These are the same tracks
   as the session's columns; a change in one view is a change in both.
2. Later: group tracks (doc 03, open question 2).

## Clips in the arrangement

1. **(R1)** Clips on one track do not overlap (doc 03, rule 3). Placing or moving a clip so that
   it covers part of another removes the covered part of the other clip. The underlying audio
   file or notes outside the covered part are not affected.
2. **(R1)** Operations on one or several selected clips: move (also across tracks of the same
   kind), trim the start or end, split at a position, duplicate, copy and paste, delete, rename,
   colour.
3. **(R1)** Trimming an audio clip only changes which part of the file is heard; it can be
   trimmed back out again. Nothing in the arrangement modifies audio files on disk.
4. **(R1)** Extending the end of a clip whose loop switch is on repeats its loop range to fill
   the new length. *Proposed.*
5. **(R1)** Every audio clip edge gets a very short automatic fade so that cuts do not click.
6. **(R1)** Dropping an audio file on an audio track creates a clip at the drop position.
7. **(R1)** An empty MIDI clip can be created over a selected time range on a MIDI track.
8. **(R1)** Edit snapping (doc 04) applies to all of the above and can be bypassed while
   dragging with a modifier key.
9. Later: user-adjustable fades and crossfades between adjacent audio clips.
10. Later: operations on time itself across all tracks (insert silence, delete time, duplicate
    time).

## Selection

1. **(R1)** Clips can be selected individually, by rubber band, and by extending with a modifier.
2. **(R1)** A time range can be selected on one or several tracks. Operations then apply to the
   part of each clip inside the range, splitting at the range edges where needed.
3. **(R1)** A command sets the loop range to the current selection.

## Automation

1. **(R1)** Any parameter of a mixer strip or device on a track can have one envelope in the
   arrangement, shown in a lane under the track.
2. **(R1)** An envelope is a list of points in musical time joined by straight segments. Points
   can be added, moved and deleted, with edit snapping.
3. **(R1)** During playback the parameter follows its envelope. Stepped parameters (switches,
   choices) change at the point, without interpolation.
4. **(R1)** If the user moves an automated control by hand, that parameter's automation is
   suspended and the control keeps the hand-set value. The interface shows that the parameter is
   overridden and offers a control to re-enable its automation. `[own design]`, following common
   DAW practice.
5. **(R1)** Moving, splitting or deleting clips does not move automation. Automation stays with
   the time, not with the clip. Decided 2026-11-13.
6. Later: recording automation by moving controls during playback.
7. Later: curved segments, and automation inside session clips.

## Recording into the arrangement

1. **(R1)** With the arrangement record switch on and at least one track armed, starting the
   transport records into the armed tracks from the playhead position, after the count-in.
2. **(R1)** Audio recording creates a new clip and a new audio file. Where it covers existing
   clips, rule 1 of "Clips in the arrangement" applies; the old audio files stay on disk and the
   operation can be undone.
3. **(R1)** MIDI recording creates a new clip containing the notes played.
4. **(R1)** Recording stops when the transport stops or the record switch is turned off.
5. **(R1)** Recorded audio is placed so that it lines up with what the musician heard while
   playing, compensating for the measured input and output delay (doc 11).
6. Later: recording MIDI into an existing clip without replacing it; loop recording with several
   takes; punch-in and punch-out (doc 04).

## Relation to the session

1. **(R1)** Tracks suspended by session clips do not play their arrangement content until handed
   back (doc 05).
2. **(R1)** Clips can be dragged between the two views; each drag makes an independent copy.

## Export

1. **(R1)** Export renders the arrangement to an audio file: either the loop range, or from the
   start to the end of the last clip plus a user-set tail for reverbs and delays to ring out.
2. **(R1)** Export always renders the arrangement. Session clips, and any tracks currently
   suspended by them, do not change what is exported. *Proposed.*
3. **(R1)** Choices: file format (at least uncompressed WAV and lossless FLAC), sample rate, bit
   depth, with dithering applied when reducing bit depth.
4. **(R1)** Export runs offline, as fast as the machine allows, and its result does not depend on
   how fast that is. Exporting the same project twice gives identical files, provided the plugins
   it uses are themselves deterministic.
5. **(R1)** The metronome is never in an export (doc 04).
6. Later: exporting each track to its own file; compressed formats.

## Open questions

1. Is extending a looped clip to repeat it (proposed) wanted, or should clips in the arrangement
   only ever be as long as their content, with repetition done by duplicating?
2. Are group tracks needed in the first release? They are in the glossary but not in the agreed
   first-release list.
