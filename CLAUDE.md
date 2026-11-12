# LINA

LINA ("Lina is not Ableton") is a clean-room, GPLv3 DAW for Linux with a Live-style workflow.

## Language
- C++23, built with CMake (JUCE and Tracktion Engine support no other build system).
- Playback engine: Tracktion Engine v3.2.0 (pins JUCE 8.0.6), used as a runtime engine only (see
  Maintainability). Time-stretch backend: its bundled SoundTouch.
- Approved third-party dependencies beyond those: Qt 6.12 LTS, Catch2, nlohmann/json (parsing only).
- CI runs on GitHub Actions.
- UI: Qt 6 Quick. Heavy views (arrangement, session, note editor, meters) are custom scene-graph
  items with retained geometry; ordinary QML is for chrome, lists and device chains. Never create
  one QML item per document object.
- Design docs marked stack-independent never name a language, framework or library.

## Formatting
- C++ follows LLVM style, enforced by the `.clang-format` at the repo root (`BasedOnStyle: LLVM`).
  Run `clang-format` on every C++ file you touch; do not hand-format against it.

## Testing
- Every change ships with its tests. A bug fix starts with a test that fails before the fix.
- Logic stays where it can be tested headless: QML holds layout and bindings only, and the audio
  callback holds no decisions that are not covered by an offline test. Everything else lives in
  plain C++ libraries that run without a window or an audio device.
- Audio is tested by offline render: render a project to a buffer and compare it against a
  checked-in reference within a stated tolerance.
- The project file format has golden files: every format version keeps a sample project that must
  still load, and save-then-load must round-trip.
- Tests are deterministic: no real audio device, wall clock, sleeps, network or unseeded randomness.
- Coverage is a ratchet, not a target: CI measures line and branch coverage with `llvm-cov` and
  fails if it drops. Do not write tests that execute code without checking anything.
- The suite also runs under ASan with UBSan, under TSan, and under RealtimeSanitizer (Clang) for
  functions marked real-time.
- Frameworks: Catch2 for C++, Qt Quick Test run offscreen for the QML layer.

## Maintainability
- LINA owns the document: its own model with exact integer musical time, its own identities,
  undo and file format. The Tracktion edit is built from that model at runtime, one way. It is
  never saved, and no position or structure is ever read back from it into the document. Data
  the engine produces (plugin state, recorded audio, analysis results) enters the document only
  through explicit commands.
- Do not patch or fork Tracktion Engine or JUCE. Where the engine and a design doc disagree,
  raise it with the user.
- Dependencies point one way: UI -> bridge -> engine. Qt types never appear in engine code, and
  JUCE or Tracktion types never appear in QML-facing code outside the bridge.
- Every edit to a project goes through an undoable command. Nothing mutates the document directly.
- Warnings are errors, and `clang-tidy` runs with the checked-in config.
- Ownership is explicit: no raw `new`/`delete`, no owning raw pointers, and no new singletons or
  mutable globals beyond what the frameworks force.
- Real-time code is marked and restricted: no allocation, locks, I/O or logging on the audio thread.
- No dead code: no commented-out blocks, and no `TODO` without a tracked issue.
- A new third-party dependency needs the user's approval before it is added.
- Design docs change in the same commit as the behaviour they describe.

## Naming
- LINA's own implementations of Live-style devices, and the project's tooling, get GNU-style silly
  names (recursive acronyms, puns). Propose a few candidates and let the user pick.
- Concept terms (track, clip, scene, ...) stay plain; see `docs/design/03-glossary-and-domain-model.md`.
- No name contains an Ableton trademark or the name of the Live device it stands in for.

## Platforms
- Linux is the only shipping target.
- A macOS port may come later: do not go out of the way to block it, but do not build or test for it now.
- Windows is not supported and will not be. No Windows code paths, `#ifdef _WIN32` branches,
  Windows build configuration or portability layers for its sake.
