# 01 Clean-room policy

Status: **draft**. This is an engineering policy, not legal advice. Items under "Open questions"
need a decision from the project owner, and some of them may need a lawyer.

## Purpose

LINA aims to offer the workflow that Ableton Live users know, on Linux, as free software. It must
do so without using anything that belongs to Ableton: code, assets, text, names or visual design.
This doc says what we may draw on, what we may not, and how we show which is which.

## What LINA reimplements, and what it does not

- **In:** workflow concepts and observable behaviour. A grid of clip slots launched in time, a
  linear arrangement, warping audio to tempo, chains of devices on tracks. Ideas and methods of
  operation of this kind are shared across many DAWs.
- **Out:** Ableton's implementation, its expression of those ideas (graphics, layout details,
  wording), its content, and its brand.

## Permitted sources

1. Ableton's published user documentation and public marketing material, read for facts about
   behaviour. Facts are restated in our own words; text and figures are never copied.
2. Independent public material: books, tutorials, talks, forum discussions, academic papers.
3. General audio and DSP literature, and published standards (MIDI, plugin format specifications).
4. Other free-software DAWs and libraries, used under their licences and with attribution.
5. Black-box observation of a lawfully licensed copy of Live: doing something in the UI and noting
   what is seen and heard. See open question 1 before relying on this.

## Prohibited sources and actions

1. Ableton source code, in any form and from any origin, including leaks.
2. Disassembling, decompiling, debugging, tracing or instrumenting Ableton binaries, or reading
   anyone else's output from doing so.
3. Copying any Ableton asset: samples, presets, device or instrument content, icons, fonts,
   colour themes, UI graphics, screenshots used as design templates, manual text.
4. Using non-public Ableton material obtained under NDA, beta agreement or employment.
5. Anyone who has seen material under 1, 2 or 4 does not write the affected part of LINA.

## Naming and branding

- LINA uses its own names for features, devices and views wherever the common name is specific to
  Ableton. Generic industry terms (track, clip, scene, send, automation, warp) are fine.
- **Devices and tooling get GNU-style silly names** (decided 2026-11-13): recursive acronyms,
  puns and jokes in the tradition of "GNU's Not Unix", `bison` and `less`. This applies to LINA's
  own implementations of devices that Live users know and to the project's tools. It does not
  apply to concept terms (track, clip, scene, and the rest of doc 03), which stay plain so that
  newcomers can follow them.
- A silly name may wink at what the thing does. It does not contain the name of the Live device
  it stands in for, or any other Ableton trademark.
- No Ableton trademark appears in LINA's UI, binaries or package names.
- Documentation may mention Ableton Live by name only to describe compatibility or comparison,
  factually and without implying endorsement.

## Visual and interaction design

- LINA's visual design is original: its own layout proportions, palette, iconography and
  typography. "A session grid next to a mixer" is a concept; a pixel-level likeness is not
  something we produce.
- Design work starts from LINA's own interaction principles (doc 12), not from screenshots.

## Provenance

- Every statement in a behaviour spec (docs 04 to 09) carries a source tag: `[manual]`,
  `[public: <reference>]`, `[observed]`, `[literature: <reference>]` or `[own design]`.
- Where LINA deliberately differs from Live, the spec says so and marks it `[own design]`.
- A behaviour whose only available source is a prohibited one is left out, or designed
  independently and marked `[own design]`.

## Use of AI assistants

AI assistants are used to draft specs and code. An assistant's general knowledge of how DAWs
behave comes from public material and counts as source type 2, with the limitation that it cannot
cite where a given detail came from. Behaviour claims that rest only on an assistant's recall are
therefore tagged `[public: unverified]` until checked against a citable source. Assistants are not
given any material that the "Prohibited" section rules out.

## Open questions

1. **Black-box observation and the EULA.** Does Ableton's licence agreement restrict using the
   product to build a competing one, or restrict analysis of its behaviour? Until answered,
   `[observed]` should be used sparingly and not as the sole source for anything.
2. **The name.** "Lina is not Ableton" puts Ableton's trademark in the expansion of the project's
   name. Keep the expansion, keep "LINA" and drop the expansion from public material, or rename?
3. **Contributors.** Is a signed statement of compliance with this policy required from
   contributors, or is a line in the contributing guide enough?

## Decided (2026-11-13)

- **Project file import is deferred.** No work on reading Live's project files until the owner
  reopens the question; the format is not studied in the meantime.
- **Device parity is a goal.** LINA will build functional equivalents of Live's devices after the
  first release. Each one gets its own name, its own interface design and independently written
  DSP, specified from permitted sources only. Parameter layouts, preset content and factory
  sounds are not carried over.
