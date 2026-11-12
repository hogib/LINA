# 11 Non-functional requirements

Status: **draft**. Every number in this doc is *proposed*: a starting point to argue with, not a
measurement and not a promise the stack has been shown to keep. **(R1)** marks what the first
release needs. All statements are `[own design]`.

## Reference points

Targets need something to be measured on.

- **Reference machine** (decided 2026-11-13: the project owner's development machine): AMD
  Ryzen 5 5600, 6 cores and 12 threads, 32 GB of memory, Arch Linux. Audio hardware present:
  onboard HD-Audio and a USB audio interface (Burr-Brown USB Audio CODEC). The audio targets are
  measured on the USB interface.
- This machine is faster than what many first-release users will have. Targets met here say
  nothing about a four-core laptop with 8 GB; a second, weaker reference machine should be added
  once one is available to test on.
- **Reference project:** 32 tracks, 8 scenes, 500 arrangement clips, 16 of the tracks playing
  audio clips that follow the tempo, 16 playing MIDI through a lightweight instrument plugin,
  two effects per track, two return tracks. *Proposed.*
- **Large project:** 256 tracks, 10,000 arrangement clips, MIDI clips of up to 10,000 notes.

## Audio

1. **(R1)** The reference project plays on the reference machine at 48 kHz with a 256-sample
   buffer without dropouts.
2. **(R1)** A dropout is never hidden. LINA counts them and shows that one happened.
3. **(R1)** When the machine cannot keep up, audio degrades by dropping out and recovers by
   itself. It never freezes the interface, loses edits or stops the transport.
4. **(R1)** Nothing the user does in the interface causes a dropout: not opening a menu, loading
   a file, saving, resizing the window or opening a plugin's editor.
5. **(R1)** LINA adds no delay of its own between input and output beyond the audio device's
   buffers and what devices in the chain report (doc 09).
6. **(R1)** Recorded audio and notes are placed within one millisecond of where they were played
   relative to what was heard, using the delays the audio system reports, with a user-adjustable
   correction for interfaces that report wrongly.
7. **(R1)** Sample rates from 44.1 kHz to 192 kHz; buffer sizes from 32 to 4096 samples.
8. **(R1)** Processing uses several processor cores when tracks can be computed independently.
9. **(R1)** Clip launches, loop points, automation and notes are placed to the exact sample
   (docs 04, 05 and 08), whatever the buffer size.

## Working with the Linux audio system

1. **(R1)** On a current Linux desktop LINA produces sound on first start without the user
   configuring anything, and coexists with other applications that are playing audio.
2. **(R1)** Losing the audio device (unplugged interface, suspended laptop) does not crash LINA
   or lose work. LINA says what happened and resumes when a device is available.
3. **(R1)** MIDI devices can be connected and disconnected while LINA runs.

## Interface responsiveness

1. **(R1)** With the reference project playing, the interface draws at the display's refresh
   rate, at least 60 frames per second.
2. **(R1)** A click or key press shows its effect within 50 milliseconds.
3. **(R1)** With the large project, scrolling, zooming and editing stay interactive: at least 30
   frames per second with everything visible, and the display's full rate at ordinary zoom
   levels.
4. **(R1)** No operation blocks the interface for more than 100 milliseconds without showing
   progress and offering a way to cancel.
5. **(R1)** When stopped and untouched, LINA uses close to no processor time, so it does not
   drain a laptop battery in the background.

## Starting and loading

1. **(R1)** From launch to a usable empty project in under 3 seconds on the reference machine.
2. **(R1)** Looking for installed plugins happens in the background. A broken plugin cannot
   prevent LINA from starting, and each plugin is examined in a way that survives its crashing.
3. **(R1)** The reference project opens in under 5 seconds, not counting the time its plugins
   take to load. Audio files are not read completely before the project is usable.

## Reliability

1. **(R1)** Losing or corrupting the user's work is the most severe class of defect, ahead of
   any audio or display problem.
2. **(R1)** Guarantees on saving, crash recovery and plugin failure are in docs 09 and 10.
3. **(R1)** Exporting the same project twice gives identical files (doc 06).

## Accessibility and display

1. **(R1)** Every command can be reached from the keyboard (doc 12).
2. **(R1)** No information is carried by colour alone (doc 05, slot states).
3. **(R1)** The interface scales cleanly on high-density displays, including fractional scale
   factors, and the user can make everything larger or smaller.
4. **(R1)** Light and dark appearance, following the desktop's setting by default.
5. **(R1)** Controls carry names and values that assistive technology can read. Full use with a
   screen reader is a goal for later, but the first release must not build in obstacles to it.
6. **(R1)** Animation is modest and respects the desktop's reduced-motion setting.

## Language

1. **(R1)** All interface text is translatable from the first release. English is the only
   language required at first.
2. **(R1)** Names of projects, tracks, clips and files can use any script.
3. **(R1)** Behaviour does not depend on the system's language settings: saved files (doc 10),
   sorting of identifiers and letter-case handling are the same everywhere.
4. Later: right-to-left layout.

## Desktop integration

1. **(R1)** Works in both Wayland and X11 sessions.
2. **(R1)** Follows the desktop's conventions for where settings, caches and user data are
   stored, for file dialogs, and for drag and drop from the file manager.

## Privacy

1. **(R1)** No telemetry. LINA makes no network connection that the user did not ask for.

## Open questions

1. When can a second, weaker reference machine be added?
2. Is the reference project the right size for "what a first-release user will actually make"?
3. Are 256 tracks and 10,000 clips the right ceiling to design for, or is that too generous for
   now?
