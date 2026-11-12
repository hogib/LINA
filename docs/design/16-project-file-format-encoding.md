# 16 Project file format: encoding

Status: **draft**. Stack-dependent. Requirements come from doc 10; this doc says how they are
encoded on disk. Items are *proposed* unless marked decided.

## Project folder

```
My Song/
├── project.lina            the document (text)
├── view.lina               zoom, scroll, which view is showing (text)
├── media/                  imported copies and recordings
│   └── kick-3f9a1c2e.wav
├── state/                  plugin state, one file per distinct state
│   └── 8d41…e07.bin
├── backup/
│   └── project.lina        the previous saved document
├── recovery/               present only while there are unsaved changes
│   └── project.lina
├── cache/                  waveform overviews, detected tempos; safe to delete
└── .lock                   present while the project is open
```

- View state is a separate file so that scrolling and zooming do not change the document and do
  not show up when the document is compared between versions. Doc 10 requires view state to be
  saved; it does not require it to be in the same file.

## Encoding of the document

1. **JSON, UTF-8, Unix line endings**, written by LINA's own writer in a fixed canonical layout:
   - one property per line, two-space indentation, a newline at the end of the file;
   - object keys in the order the format defines for that object, never alphabetical or
     insertion order;
   - lists of tracks, scenes, clips and devices in document order; notes sorted by start, then
     pitch, then identity; automation points sorted by time.
2. **Numbers.**
   - Musical time, sample positions, identities and counts are integers.
   - Other values (gain, pan, tempo) are written as the shortest decimal that reads back to the
     same value, using the C++ standard library's locale-independent conversions.
   - No value is ever written in a way that depends on the system's language settings.
3. **Reading** is strict: an unknown key, a missing required key or a value of the wrong type is
   an error naming the place in the file, never a silent default.
4. **Parsing** uses nlohmann/json (MIT licence, header-only), approved as a dependency on
   2026-11-13 and used for parsing only. Writing uses LINA's own canonical writer.

Illustration only; the real schema is written with the model code:

```json
{
  "format": 1,
  "next_id": 1042,
  "tempo": 120.0,
  "time_signature": [4, 4],
  "scenes": [
    { "id": 1003, "name": "Intro" }
  ],
  "tracks": [
    {
      "id": 1001,
      "kind": "audio",
      "name": "Drums",
      "slots": [
        {
          "scene": 1003,
          "clip": {
            "id": 1010,
            "name": "kick loop",
            "media": "media/kick-3f9a1c2e.wav",
            "region": [0, 352800],
            "follow_tempo": true,
            "warp": [[0, 0], [352800, 120960]]
          }
        }
      ]
    }
  ]
}
```

## Versions and migration

1. `"format"` is a single integer at the top of the document.
2. A migration is a function from the parsed JSON of version N to the parsed JSON of version
   N+1. Migrations work on the JSON tree, not on model classes, so old shapes never need old
   classes kept alive.
3. Loading: parse, check the version, refuse if it is newer than this build knows, apply
   migrations in order, then build the model strictly from the current shape.
4. When a document was migrated, the first save moves the original to `backup/` with its version
   number in the name (doc 10).

## Identities

1. Every track, scene, slot, clip, note, device, automation envelope and media reference has an
   integer `id`.
2. `next_id` in the document is the counter. An identity is assigned when the object is created
   by a command, not when it is saved, and is never reused within a project.

## Media

1. A file copied into the project is stored in `media/` under its original name plus the first
   eight hexadecimal digits of its content hash. Importing the same content twice finds the
   existing file (doc 10).
2. A media reference in the document holds: the path (relative when inside the project folder),
   the file size, and the full content hash as a hexadecimal string.
3. Hashing is SHA-256, computed on a worker thread by an import service that sits above the
   format library. `lina_format` only stores and compares the string (doc 13).
4. Paths in a document that point above the project folder through relative segments are
   rejected on load (doc 10, untrusted projects).

## Plugin state

1. A plugin's opaque state is stored as a binary file in `state/`, named by the SHA-256 of its
   content. The device in the document refers to it by that name.
2. Reasons: megabytes of encoded data inside the text document would defeat reading and
   comparing it; and files named by content never need rewriting, which makes saving safe
   (below). See open question 2.

## Saving

1. Order of operations:
   1. write any new files in `media/` and `state/` (names are content-derived, so existing files
      are never modified);
   2. write the new document to a temporary file in the project folder and flush it to disk;
   3. move the current `project.lina` to `backup/`;
   4. rename the temporary file to `project.lina`;
   5. remove `recovery/`;
   6. delete files in `state/` that neither the new document nor the backup refers to.
2. A crash at any step leaves a loadable project: either the old document with all its files, or
   the new one with all of its.
3. Serialising happens on the main thread into memory; writing and flushing happen on a worker.

## Recovery copy

1. Same encoding, written to `recovery/project.lina` by the same steps 1 and 2, at most once a
   minute while there are unsaved changes (doc 10; interval is open there).
2. On opening a project that has a `recovery/` document newer than `project.lina`, LINA offers
   it (doc 10).

## Lock

1. `.lock` holds the process identifier and host name of the instance that has the project open.
   A second instance that finds a live lock warns the user. A stale lock (no such process) is
   taken over silently.
2. The lock file is the one place a host name is written. It is not part of the saved project
   and is removed on close.

## Testing

1. Golden files: `tests/golden/v<N>/` holds at least one project per format version (doc 14).
2. Round trip: for generated documents, save, load, save gives identical bytes.
3. A fuzz target feeds arbitrary bytes to the loader; any crash is a defect (doc 10).

## Open questions

1. Plugin state in separate binary files (proposed), or encoded inside the document so that a
   project is a single text file plus media?
2. File extension and names: `project.lina` and `view.lina` (proposed)?
3. JSON was chosen over XML and TOML for being simple to write canonically and widely readable.
   Any objection?
