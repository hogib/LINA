# 13 Architecture

Status: **draft**. This doc is stack-dependent. Decided by the project owner on 2026-11-13: C++23,
CMake, Tracktion Engine on JUCE as a runtime playback engine, Qt 6 Quick for the interface, and
LINA owning its own document. Everything else here is *proposed* unless marked decided.

Evidence referred to below comes from throwaway spikes and a source-level fit check of docs 03 to
10 against Tracktion Engine v3.2.0. The spikes were deleted once their measurements were recorded;
the sources of the two C++ spikes remain in git history at commit `389c082` under `spike/`.

Engine version, decided 2026-11-13: Tracktion Engine release tag v3.2.0, which pins JUCE 8.0.6.
This is what every spike and the fit check used. Its `develop` branch (JUCE 9, unreleased) is not
used; LINA moves when a new release is tagged.

## The central idea

LINA's document is its own data structure. The playback engine is given a projection of it and is
never the source of truth.

```
gesture ──► command ──► document model ──► change set ──┬──► interface (redraw what changed)
                         (exact, undoable)              └──► projection ──► Tracktion edit ──► audio
                                                                               │
engine state (playhead, slot states, meters) ◄─────────────────────────────────┘
        read by the interface every frame; never written into the document
```

Why (decided 2026-11-13): the engine stores positions as floating-point seconds and beats, has
its own file format with user names and timestamps, and gives notes no stored identity. Docs 04
and 10 require the opposite.

## Libraries and their dependencies

Each is a separate CMake target. An arrow means "may include headers of".

```
lina_app ──► lina_ui ──► lina_bridge ──► lina_engine ──► lina_model
                │             │                              ▲
                └─────────────┴──────────────► lina_format ──┘
```

| Library | Contains | May use | Must not use |
|---|---|---|---|
| `lina_model` | Document types, exact musical time, identities, commands, undo, change sets | C++23 standard library | Qt, JUCE, Tracktion |
| `lina_format` | Reading and writing the project document, migrations (doc 16) | `lina_model`, a JSON parser | Qt, JUCE, Tracktion |
| `lina_engine` | Projection into a Tracktion edit, transport and launch control, recording, rendering, plugin hosting, engine-state snapshots | `lina_model`, Tracktion, JUCE | Qt |
| `lina_bridge` | Event-loop integration, QObject controllers, list models over the document | `lina_model`, `lina_engine`'s public headers, Qt | Tracktion or JUCE types in any header QML can see |
| `lina_ui` | QML and custom scene-graph items | `lina_bridge`, `lina_model` (read-only), Qt Quick | Tracktion, JUCE |
| `lina_app` | `main`, command line, start-up | all | |

- The build enforces the "must not use" column: `lina_model` and `lina_format` have no Qt or
  JUCE include paths, so a stray include is a compile error. The Qt Quick spike built its model
  this way without friction.
- `lina_engine`'s public headers expose LINA types only. Tracktion and JUCE types stay in its
  source files.
- The one place where Qt and JUCE meet in a single file is the event-loop integration in
  `lina_bridge`.

## Processes

1. **The application.** One process holds the document, the engine and the interface.
2. **Plugin scanner.** The same executable started with a scanner argument, one plugin at a time,
   so that a plugin crashing while examined is recorded as failed (doc 09). Tracktion provides
   the child-process mechanism; it is off by default and LINA turns it on.
3. Later: plugin host processes, for isolation of running plugins (doc 15).

## Threads

| Thread | Does | Rules |
|---|---|---|
| Main | Qt event loop, which also serves as JUCE's message thread. All document mutation, all Tracktion edit mutation, QML | Nothing here may block for more than a few milliseconds |
| Qt render | Scene-graph rendering. Reads item state during the sync phase while the main thread is blocked | Never touches the engine |
| Audio (several) | Tracktion's node graph: playback, plugins, launch timing | No allocation, locks, I/O or logging (`CLAUDE.md`); never touches the document |
| Workers | Waveform overviews, tempo detection, file copying and hashing, writing saves and recovery copies | Deliver results to the main thread; never mutate the document themselves |
| Recording writer | Writes incoming audio to disk | Tracktion's own |

- **One event loop.** Qt owns it. JUCE's file descriptors are watched with Qt socket notifiers,
  as the first spike did (154 lines, microsecond overhead) and as Zrythm does. This relies on a
  JUCE header marked internal and must be re-checked on every JUCE upgrade.
- **The document is main-thread only.** Custom items read it during the scene-graph sync phase,
  when the main thread is blocked, so no lock is needed. The engine never reads the document; it
  sees only its projection.
- **Known hazard:** code in JUCE or a plugin that runs its own nested event loop (a blocking
  dialog) starves Qt for as long as it is up. Not yet exercised.

## The document model (`lina_model`)

1. **Musical time** is a 64-bit integer count of ticks, 15,120 per quarter note. That number
   divides evenly by 16, 20, 24 and 28, which covers straight, quintuplet, triplet and septuplet
   subdivisions down to 1/64 notes (doc 04). Decided 2026-11-13.
2. **Identities** are 64-bit integers taken from a counter stored in the document, never reused
   and never random, so tests and saves are deterministic (doc 10).
3. **Commands** are plain values (`MoveClip`, `SetTempo`, ...). `apply` is the only way to change
   the document. Each command yields its inverse; undo applies inverses. One user gesture is one
   transaction of one or more commands (doc 12). Decided 2026-11-13: undo by inverse commands,
   not by keeping earlier versions of the document. Tests check, for generated command
   sequences, that applying a command and then its inverse restores the document exactly.
4. **Rules of the model are enforced in `apply`:** clips on a track never overlap, same-pitch
   notes never overlap, every track has one slot per scene (docs 03, 06, 08). The engine is
   never relied on for these.
5. **Change sets.** Applying a command returns a description of what changed (which objects,
   which properties). Both the interface and the projection consume it. The Qt Quick spike used
   a single coarse "document changed" signal and rebuilt everything visible; that is the thing
   to do better.

## The projection (`lina_engine`)

1. Holds a map from LINA identities to Tracktion objects and updates the edit incrementally from
   change sets.
2. Converts ticks to the engine's beats and seconds on the way in. Nothing is converted back
   (doc 04).
3. **Test oracle:** an edit built incrementally through any sequence of commands must equal an
   edit built from scratch from the resulting document. This is checked in tests for randomly
   generated command sequences.
4. Tracktion's own undo manager and file saving are not used.
5. Policies the engine leaves to the application live here, on top of its launch handles: one
   clip per track, scene launch with empty slots stopping, "stop all", the relaunch set, launch
   boundaries computed in ticks, rounding record length to quantisation, return-track and send
   bookkeeping, exclusive solo.
6. **What flows back, and how:**
   - *Engine state* (playhead, slot states, meters, dropout count, total compensated delay) is
     published as snapshots and atomics that the interface polls once per frame.
   - *Engine-produced data* (plugin state, finished recordings, detected tempos) reaches the
     document only as commands issued on the main thread.

## The interface (`lina_ui`, `lina_bridge`)

1. Heavy views are custom scene-graph items that hold retained geometry and move it with
   transforms. Measured in the Qt Quick spike with 10,000 clips on screen: 0.14 ms per frame
   when only the playhead moves, about 7.6 ms on the worst frames (zooming with everything
   visible).
2. Content is built in tiles per track and time range. The spike built one large block and
   measured a 24.7 ms hitch when scrolling out of it.
3. One QML item per document object is forbidden. The spike measured over 100 ms per zoom frame
   that way.
4. Waveforms are built from cached peak data and their geometry is kept between frames.
5. Meters and the playhead are drawn from polled engine state, never through item models or
   per-buffer signals.
6. List models for chrome (track headers, device chains, browser) are thin adapters over the
   document and its change sets.

## Start-up and shutdown

1. Start the Qt application, create JUCE on the main thread, install the event-loop bridge,
   create the engine, then load QML. Plugin scanning starts in the background afterwards.
2. Shutdown stops the audio device, closes plugin windows, destroys the engine, then Qt.

## Open questions

1. How a recovery copy is taken without stalling the main thread on a large project.
