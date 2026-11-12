# LINA design docs

Status values: **planned** (not started), **draft** (written, not yet agreed), **agreed**.
A doc marked stack-independent must not name a language, framework or library.

| # | Doc | Covers | Stack-independent | Status |
|---|---|---|---|---|
| 01 | [Clean-room policy](01-clean-room.md) | What sources we may and may not use, naming, assets, provenance | yes | draft |
| 02 | [Vision and scope](02-vision-and-scope.md) | What LINA is for, who it is for, non-goals, what the first usable release contains | yes | draft |
| 03 | [Glossary and domain model](03-glossary-and-domain-model.md) | LINA's own terms: project, track, clip, scene, slot, device, chain, and how they relate | yes | draft |
| 04 | [Transport and musical time](04-transport-and-musical-time.md) | Tempo, time signature, bars/beats vs seconds, quantisation, looping, tempo changes | yes | draft |
| 05 | [Session workflow](05-session-workflow.md) | Clip slots, launching, launch quantisation, scenes, follow actions, recording into slots | yes | draft |
| 06 | [Arrangement workflow](06-arrangement-workflow.md) | Timeline editing, clip operations, automation lanes, recording session to arrangement | yes | draft |
| 07 | [Audio clips and tempo sync](07-audio-clips-and-tempo-sync.md) | Tempo sync, stretch modes, source tempo, forward compatibility with warp markers | yes | draft |
| 08 | [MIDI clips and note editing](08-midi-clips-and-note-editing.md) | Note model, piano roll operations, velocity and controller data | yes | draft |
| 09 | [Mixer, routing and devices](09-mixer-routing-and-devices.md) | Signal flow, sends/returns, monitoring, device chains, delay compensation | yes | draft |
| 10 | [Project file format: requirements](10-project-file-format-requirements.md) | Versioning, migration, what must round-trip, media references, crash recovery | yes | draft |
| 11 | [Non-functional requirements](11-non-functional-requirements.md) | Dropout-free audio, responsiveness, scale targets, startup time, accessibility | yes | draft |
| 12 | [Interaction principles](12-interaction-principles.md) | Keyboard model, drag and drop, undo semantics, focus, learnability | yes | draft |
| 13 | [Architecture](13-architecture.md) | Processes, threads, libraries, document ownership, projection into the engine | no | draft |
| 14 | [Build, test and CI](14-build-test-and-ci.md) | Toolchain, dependencies, sanitizers, test tiers, coverage ratchet | no | draft |
| 15 | [Plugin hosting](15-plugin-hosting.md) | Formats, scanning, editor windows, plugin state | no | draft |
| 16 | [Project file format: encoding](16-project-file-format-encoding.md) | Project folder layout, canonical JSON, migrations, save sequence | no | draft |

Stack, decided 2026-11-13: C++23, CMake, Tracktion Engine on JUCE as a runtime playback engine,
Qt 6 Quick for the interface. LINA owns its document; see doc 13.

Docs 04 to 09 are behaviour specifications. Each statement in them records its source as required
by the clean-room policy.
