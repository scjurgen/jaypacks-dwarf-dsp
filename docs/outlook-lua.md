# Outlook: Lua scripting on the MOD Dwarf

Research note (2026-10-15), measured on the device on 2026-10-17 (`docs/device.md`, "Worker, script directory, MIDI
out and tempo"); the plan is the roadmap section "Lua scripting". The question: can special algorithms be written as
Lua scripts, generated and refined with an LLM, and run inside a jaypacks plugin on the Dwarf, and how does a
script get onto the device and get edited there, by a user who has only the installed plugin, a USB cable and ssh?
Facts marked "checked" were looked at on the device or in the source; "to verify" marks what is assumed.

## What AbacDsp already does

AbacDsp has a shared Lua engine for its JUCE examples (`LUA.md`, `LUA-MANUAL.md` in AbacDsp;
six examples use it, e.g. `dronesequencer`, `spectraltap`, `tapelooper`). Checked in the
source:

- **Lua 5.4 with sol2** (both MIT), compiled into the plugin.
- **Sandbox:** only `base`, `math`, `table` and `string`; no `io`, `os`, `require`.
- **Memory:** the Lua state allocates from a fixed arena (`LuaScriptMemoryPool`, passed to
  `lua_newstate`), so calls on the audio thread do not touch the process heap.
- **Two threads:** loading a script (compile, run the top-level code, bind the handlers) is
  message-thread work; a script that fails to compile is rejected and the previous one keeps
  running. On the audio thread the engine calls the script's handlers once per block (MIDI,
  timers, transport, UI parameters), each call under an instruction-count watchdog so a
  runaway script cannot stall the audio.
- **Control, not samples:** scripts steer the C++ DSP through bound setters (`SetFilter`,
  `NextNotes`, ...); the per-sample work stays in C++.
- **LLM authoring:** a localhost HTTP API (`AuthoringHttpServer`, AbacDsp `authoring/`) lets
  a Claude Code session find a running plugin instance, push a script, and read diagnostics
  back. It replaced an older folder-watching workflow.

This is the model to follow: Lua for decisions at block or event rate, C++ for the samples.

## What exists for LV2 and on the Dwarf

| Option | What it is | Status |
|---|---|---|
| Moony (Open Music Kontrollers) | LV2 plugins that run realtime Lua 5.4 scripts as glue between plugins, with a realtime allocator (TLSF) | Control and atom (event) ports only, no audio ports [1]. Not installed on this Dwarf (checked); whether the MOD plugin store offers it is to verify |
| Notes (Open Music Kontrollers) | "Sticky note" plugin: stores a text in the pedalboard | Installed on this Dwarf (`/root/.lv2/notes.lv2`, checked) |
| Ardour Lua DSP | Lua scripts as Ardour plugins | Ardour only, not an LV2 plugin; not available in mod-ui |

How Notes edits text in the web UI (checked in its bundle): the text is an LV2 parameter
(`lv2:Parameter` with range `atom:String`, listed in `patch:writable`), carried by
`patch:Set` messages on an atom control port and kept in the plugin's state, so it is saved
with the pedalboard. The modgui shows it with mod-ui's string widget, a `<textarea>` with
`mod-role="input-parameter"` and `mod-widget="string"`. The plugin's description warns of a
limit of about 450 characters (whether that is mod-ui's or the plugin's limit is to verify).

## Ways to get a script into a plugin on the Dwarf

| Route | How | Verdict |
|---|---|---|
| String parameter, as Notes does | `script` parameter (atom:String, `patch:writable`), edited in a textarea of the modgui, saved with the pedalboard | Too small: Notes states about 450 characters, a patch script has 1.0 to 2.3 kB; not tested which layer sets the limit |
| String parameter from a script | `tools/modui.py` sends the same `patch:Set` over the mod-ui websocket | Same limit |
| File parameter | `atom:Path` parameter with a file from the device's user files, as the IR loaders do | mod-ui offers user files only for a fixed list of types (checked in its code); there is no script type and no upload route |
| **Files in a directory the plugin reads (chosen)** | Scripts as `.lua` files below `/data` (the plugin process is root and can read and write there, checked), copied with `scp -O` or edited over ssh; the plugin polls the modification time and reloads | Works without the web UI; needs ssh over USB, which every Dwarf user has; no size limit |
| Built-in scripts | Scripts in the plugin bundle, chosen by a menu | The factory patches; read-only |

## Constraints on the Dwarf (checked 2026-10-17 unless marked)

- **CPU:** 4 x Cortex-A35 at 48 kHz and 128 frames: one `run()` has 2.67 ms. Lua 5.4 runs 31 million VM
  instructions per second (15 million with a count hook for a watchdog), a call of a small note handler takes 1.5 to
  1.8 us, compiling a 1.25 kB script 0.6 to 0.9 ms. Scripts belong at event or block rate, as in AbacDsp, and
  they run in the LV2 worker thread, not in `run()`.
- **Worker:** mod-host provides `work:schedule` and `work:interface`. The worker wakes within 0.06 ms (0.15 ms on
  average, 1.3 ms at most under 12 notes of Morphexsynth), and a response reaches the plugin at the start of the
  next `run()`, so a script's answer is one period (2.7 ms) late. The queues of the worker are small (jobs of 8 kB
  are refused, about 80 responses per period fit), so bulk data uses the plugin's own lock-free queues.
- **MIDI and tempo:** a plugin can send MIDI to another plugin and to the Dwarf's MIDI output with the frame offsets
  it writes, and gets the board tempo from a designated control port and `time:Position` atoms.
- **Toolchain:** Lua 5.4 is plain ISO C and builds with GCC 9.4 (307 kB shared object with the standard libraries).
  AbacDsp's engine uses sol2 and C++20 (`std::span`) and JUCE threading, so the core is a port with the plain C API,
  like every AbacDsp port.
- **State:** a script that is a file needs no plugin state; the selected slot is a control port, which the host
  saves with the pedalboard. mod-host supports the state interface (strings found in its binary); the adapter does
  not use it.
- **Persistence (to verify):** `/data` is a separate partition; that it survives a MOD OS update was not tested.

## Reaper

REAPER has its own scripting (ReaScript in Lua or Python) and JSFX for audio effects. Neither
runs inside an LV2 plugin, so they do not help on the Dwarf. Useful are: ReaScript to automate
renders of test projects on the Mac (`docs/reaper.md`), and the fact that a jaypacks plugin
with a string parameter would be editable in REAPER's generic UI only if REAPER shows LV2
string parameters (to verify).

## Recommendation

Decided with the author on 2026-10-17 (roadmap section "Lua scripting"):

1. Lua at event and block rate, C++ for the samples, as in AbacDsp.
2. A plugin-independent core (`scripting/`) that every scripted plugin uses; a plugin supplies only its bindings.
3. Scripts as files, run in the worker thread, hot-reloaded by modification time, errors in a log file beside the
   script and in a status port; the API described by a file generated from the plugin's binding table, so an LLM on
   the user's machine can write scripts against what the installed plugin really offers.
4. The plain Lua C API instead of sol2.
5. Morphexsynth is the first user, a MIDI effect written in Lua (MIDI in, MIDI out, arpeggiators and the like) the second.

## Sources

1. Moony.lv2, Open Music Kontrollers, https://www.ventosus.ch/moony.lv2/ (read 2026-10-15).
2. MOD forum, "Saving plugin internal state" (LV2 parameters of type string in mod-ui),
   https://forum.mod.audio/t/saving-plugin-internal-state/9009 (read 2026-10-15).
3. LV2 patch and parameters extensions, https://lv2plug.in/ns/ext/patch and
   https://lv2plug.in/ns/ext/parameters.
