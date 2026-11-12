# 15 Plugin hosting

Status: **draft**. Stack-dependent. Requirements come from doc 09; this doc says how they are met
with JUCE and Tracktion Engine. Items are *proposed* unless marked decided.

## Formats

| Format | First release | Basis |
|---|---|---|
| VST3 | yes | Hosted by JUCE in-tree. One VST3 was loaded, played and had its editor opened on a Wayland session in the first spike. |
| LV2 | yes, with a known risk | Hosted by JUCE in-tree. In the first spike an LV2 plugin loaded but logged eight JUCE assertions that have not been investigated. LV2 matters on Linux, so the risk is worth carrying; see open question 1. |
| CLAP | later | Neither JUCE nor Tracktion hosts it. It would be our own host code. |
| LADSPA, VST2 | no | |

## Scanning

1. Scanning runs in child processes, one plugin at a time, using Tracktion's scanner mechanism,
   which LINA switches on. The LINA executable recognises the scanner command line at start-up
   and acts as the child.
2. A plugin that crashes or times out while being scanned is recorded as failed, with the
   reason, and is not scanned again until the file changes or the user asks.
3. Results are cached with each plugin file's size and modification time. Start-up reads the
   cache and never waits for a scan (doc 11).
4. The user can see the list of found and failed plugins and trigger a rescan.
5. LV2 plugins are identified by their URI, not by bundle path.

## Running plugins

1. **First release (decided 2026-11-13):** plugins run inside the LINA process. A plugin that
   crashes takes LINA down; the recovery copy limits the loss (docs 09 and 10).
2. **Later:** isolation of running plugins. Tracktion lets the application replace the function
   that creates plugin instances, which is the place to attach a bridge to a host process. The
   bridge itself is a substantial component and is not designed here.
3. Instruments and effects are told apart from the plugin's own description. A MIDI track
   accepts one instrument at the head of its chain (doc 09).

## Editor windows

1. A plugin's editor is a JUCE-created top-level X11 window. On a Wayland session it runs through
   Xwayland, next to the native Wayland main window. Confirmed working by the owner for one VST3
   on 2026-11-13: the window opened, responded, and the main window stayed live.
2. Not yet checked, and to be tested before the first release:
   - whether an editor stays above the main window and is grouped with it;
   - scaling on high-density and fractionally scaled displays, where Qt and JUCE take different
     paths;
   - where keyboard input goes: an editor that has focus swallows the transport keys;
   - plugins that open blocking dialogs of their own, which can starve Qt's event loop while
     open (doc 13);
   - LV2 plugin interfaces.
3. Each editor window is titled with the track and device name. Closing the main window closes
   all editors.
4. A plugin without an editor, or whose editor fails to open, gets a generic parameter panel
   built in QML from its parameter list.
5. Embedding an editor inside the main window is not planned; it would require running Qt on
   X11 as well.

## Parameters and automation

1. A plugin's parameters are exposed to the document by the plugin's own stable parameter
   identifier, not by position in its list, so that automation survives plugin updates that add
   parameters.
2. Parameter changes made from LINA (automation lanes, the generic panel, mapped controls) are
   commands and are undoable.
3. Changes made inside a plugin's own editor are not individually undoable in the first release
   (decided 2026-11-13). They are captured as part of the plugin's state (below). This is the one
   exception to doc 12's "every change to the project is undoable" and is recorded there.

## Plugin state

1. The document stores, per device: the plugin's format, identifier, name, vendor and version,
   and its opaque state (docs 09 and 10; encoding in doc 16).
2. State is pulled from the running plugin and written into the document by a command when the
   plugin reports that its state changed, and before the device is duplicated or copied. Saving
   and recovery copies use what the document already holds.
3. Consequence: saving an unchanged project does not ask plugins for their state again, so the
   saved bytes do not change (doc 10). The fit check found that Tracktion's own saving re-fetches
   state on every save; LINA does not use that path.
4. A plugin that never reports state changes would be saved stale. To cover that, state is also
   pulled for every device when the user saves explicitly, and the document is updated only if
   the bytes differ.

## Missing plugins

1. A device whose plugin is not installed is shown as missing with the stored name and vendor,
   passes audio through unchanged, and keeps its stored state untouched (doc 09). The engine
   already behaves this way for the audio path.
2. Installing the plugin and rescanning restores the device without reopening the project.

## Delay compensation

1. Plugin-reported delay is compensated by the engine across tracks and through return tracks;
   the fit check confirmed this in source. LINA reads the total and shows it (doc 09).

## Open questions

1. LV2 in the first release: commit now (proposed) and investigate the assertions as the first
   task of the hosting work, or decide after investigating?
2. When does isolation of running plugins get scheduled: right after the first release, or only
   if crash reports show it is needed?
