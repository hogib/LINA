# 14 Build, test and CI

Status: **draft**. Stack-dependent. This doc turns the testing and maintainability rules in
`CLAUDE.md` into concrete tooling. Items are *proposed* unless marked decided.

## Toolchain

| Item | Choice | Note |
|---|---|---|
| Language | C++23 (decided) | Headers, not modules. Qt's `moc` handles module units only from Qt 6.13 with CMake 4.5, and not yet with GCC. Revisit then. |
| Build system | CMake with Ninja (decided) | JUCE and Tracktion Engine support nothing else. |
| Presets | `CMakePresets.json` | One preset per configuration below, so that local runs and CI use the same commands. |
| Compilers | GCC and Clang, both required to build cleanly | The spikes used GCC 16 only. **Clang is untested with this stack** and is needed for the sanitizers and coverage. |
| Formatting | `clang-format`, LLVM style (decided) | Checked in CI. |
| Static analysis | `clang-tidy` with a checked-in `.clang-tidy` | Our targets only. |

## Dependencies

| Dependency | How it is obtained | Status |
|---|---|---|
| Tracktion Engine v3.2.0, with JUCE 8.0.6 as its submodule | Git submodule pinned to the release tag | Decided 2026-11-13 |
| Qt 6.12 LTS (Core, Gui, Qml, Quick, QuickControls2, QuickTest) | System packages | Decided as a dependency |
| Catch2 | System package or pinned fetch | Approved in `CLAUDE.md` |
| nlohmann/json | System package or pinned fetch; parsing only (doc 16) | Approved 2026-11-13 |
| SoundTouch | Bundled with Tracktion Engine | Decided 2026-11-13 as the time-stretch backend |

- JUCE and Tracktion compile once into a static library with their warnings silenced, and are
  included as system headers so that our warning flags do not fire on them.
- No third-party source is patched (`CLAUDE.md`). Tracktion's vendored `nanorange.hpp` has a
  typo that GCC 14 and later reject; the workaround is the flag `-Wno-template-body` on the
  third-party target only.
- Build options set for JUCE: web browser and curl off, JACK off for now, VST3 and LV2 hosting
  on, LADSPA off.

### Time-stretch

Doc 07 requires stretching in the first release. At the Tracktion tag used so far, all stretch
backends are off by default and the choices are:

| Backend | Bundled with Tracktion | Licence as stated in the checkout | Note |
|---|---|---|---|
| SoundTouch | yes | LGPL 2.1 or later | No new dependency. Its tempo detector is already compiled in. |
| Rubber Band | no | GPL, licensed separately | A new system dependency. Generally regarded as higher quality. |
| Elastique | no | proprietary | Not usable in a GPL project. |

Decided 2026-11-13: SoundTouch for the first release, since it adds no dependency. Rubber Band is
reconsidered if its quality turns out not to be good enough.

## Our compile flags

- `-Wall -Wextra -Wpedantic -Werror` on every LINA target (`CLAUDE.md`: warnings are errors).
- `QT_NO_KEYWORDS`, because Tracktion has identifiers named `slots`.
- Functions that run on the audio thread are marked `[[clang::nonblocking]]`.

## Configurations

| Preset | Compiler | Purpose |
|---|---|---|
| `dev` | GCC, debug | Everyday work |
| `release` | GCC, optimised | What ships; performance benches |
| `asan` | Clang, ASan + UBSan | Whole test suite |
| `tsan` | Clang, TSan | Whole test suite |
| `rtsan` | Clang 20+, RealtimeSanitizer | Tests that drive the audio callback |
| `coverage` | Clang, source-based coverage | Coverage ratchet |

- **Risk:** RealtimeSanitizer will also see Tracktion's and JUCE's audio-thread code. The fit
  check found at least one place where the engine takes a lock related to the audio callback
  (tempo changes). Expect to need a suppression list for third-party frames, kept in the repo
  and reviewed when the engine is upgraded.

## Test tiers

| Tier | Covers | Framework | Needs |
|---|---|---|---|
| Model | Commands, inverses, model rules, exact time arithmetic | Catch2 | Nothing; no Qt, no JUCE |
| Format | Round trip, byte-stable save, migrations, golden files per format version, damaged input | Catch2, plus a fuzz target | Nothing |
| Projection | Incremental projection equals a from-scratch build, for generated command sequences (doc 13) | Catch2 | Engine, no audio device |
| Audio | Offline render of small projects compared with checked-in reference audio within a stated tolerance | Catch2 | Engine, no audio device |
| Session timing | Launch boundaries, scene launch, stop, record rounding, at the exact sample | Catch2 | Engine driven block by block |
| Interface | QML components and custom items with synthesized input | Qt Quick Test, offscreen | Qt; `QT_QUICK_BACKEND=rhi` must be set or the offscreen platform silently picks the software renderer |
| Benches | The targets of doc 11 on the reference machine | Own harness | Not part of the gating suite at first |

- Tests never open a real audio device, read the wall clock, sleep, use the network or use
  unseeded randomness (`CLAUDE.md`). The engine runs on a null device or is driven by hand.
- Reference audio comparisons state their tolerance per test. A test that depends on a
  third-party plugin is not allowed in the gating suite; a small test instrument and effect of
  our own are built for the purpose.
- Golden project files live in `tests/golden/v<N>/` and are never edited after the format
  version that wrote them is released.

## Coverage ratchet

1. The `coverage` preset runs the gating suite and produces line and branch coverage with
   `llvm-cov`, for LINA's own sources only.
2. The current figures are stored in a small checked-in file. CI fails if either figure is lower
   than the stored one; a change that raises coverage updates the file in the same commit.
3. Excluded from the figure: generated code (`moc`, QML type registration) and third-party code.

## Continuous integration

1. Every push runs: format check, `dev` build with GCC, `asan`, `tsan`, `rtsan`, `coverage`
   with the ratchet, `clang-tidy`.
2. All jobs run in a container image that pins the compiler, CMake and Qt versions, built from a
   file in the repo.
3. The third-party static library is cached between runs, keyed by the pinned commit and the
   compiler version.
4. CI runs on GitHub Actions (decided 2026-11-13). The workflow is added with the first buildable
   code, not before.

## Repository layout

```
CMakeLists.txt  CMakePresets.json  .clang-format  .clang-tidy
src/model/  src/format/  src/engine/  src/bridge/  src/ui/  src/app/
qml/
tests/model/  tests/format/  tests/engine/  tests/ui/  tests/golden/  tests/audio-refs/
third_party/tracktion_engine/        (submodule)
docs/design/
spike/                               (throwaway; not built by the main project)
```

## Open questions

1. The sources of the two C++ spikes are still under `spike/`. Keep them in the repository as a
   starting point and record, or delete them once the real code has replaced them?
