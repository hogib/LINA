# 02 Vision and scope

Status: **draft**. Decisions recorded here were made by the project owner on 2026-11-13 unless a
line is marked *proposed*, which means it is a suggestion waiting for a yes or no.

## What LINA is

LINA is a free (GPLv3) digital audio workstation for Linux built around two linked ways of
working: a grid of clips that are launched in time with each other for sketching and performing,
and a linear timeline for arranging and finishing. Linux has capable timeline-first DAWs; it does
not have a free one where the clip grid is the centre of the design. That is the gap LINA fills.

## Who it is for

Linux musicians who have **not** used Ableton Live.

Consequences:

- LINA may not assume the user knows Live's concepts, vocabulary or shortcuts. Every concept is
  introduced in LINA's own terms (doc 03).
- Matching Live's conventions is not a goal in itself. Where a Live convention is awkward or only
  makes sense through habit, LINA is free to do something clearer, and the spec marks it
  `[own design]`.
- Migration from Live is not a driver of the design. This is also why Live project import is
  deferred.
- LINA should feel at home on a Linux desktop and work with the Linux audio setup the user
  already has, without a separate configuration ritual.

## First usable release

The smallest thing that counts as LINA:

- Audio tracks and MIDI tracks.
- The session grid: clip slots, launching and stopping clips, quantised launching, scenes.
- A basic arrangement: clips on a timeline, with the usual move, trim, split and duplicate.
- A mixer: volume, pan, mute, solo, sends and return tracks, a master.
- Hosting of third-party plugins as instruments and effects.
- **No built-in instruments or effects.**

Also in the first release (confirmed 2026-11-13):

- Recording audio and MIDI into clip slots and into the arrangement.
- Saving and loading projects, with the format guarantees of doc 10.
- Undo and redo for every edit.
- Exporting the arrangement to an audio file.
- Tempo-synchronised playback of audio clips: a clip with a known tempo plays in time at the
  project tempo. Without this the session grid does not work for audio. Editable warp markers
  are not part of the first release; tempo sync is enough.
- Tempo detection for imported audio, so that a clip's tempo is estimated instead of typed in
  (doc 07).
- A piano roll sufficient to enter and edit notes, with a basic quantise command (doc 08).
- Automation of mixer and plugin parameters in the arrangement.

## After the first release

- **Built-in devices.** LINA will grow its own instruments and effects, including functional
  equivalents of the devices Live users rely on, under LINA's own names and with LINA's own
  designs and DSP, as the clean-room policy requires.
- Full warp editing, follow actions, and the rest of docs 05 to 09 beyond the first-release
  subset. Each of those docs marks which parts belong to the first release.

## Non-goals

- Windows, in any release.
- macOS for now; a port may be considered later and is not designed for today.
- Importing Live projects: deferred, not rejected. Nothing in the first release depends on it.
- Looking like Live. The visual design is original.

## Open questions

1. **A scripting or patching environment for user-made devices** (the role Max for Live plays):
   long-term goal, or out?
2. **Hardware controllers.** Generic MIDI mapping is assumed eventually; is grid-controller
   support (launching clips from pads) a goal, and for which release?
3. **Tempo sync with other applications and devices** (MIDI clock, network session sync): goal or
   not, and when?
4. **Video, notation, and comping of multiple takes:** in or out of the long-term picture?
