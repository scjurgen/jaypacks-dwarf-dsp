# Adding scripting to a plugin

The checklist, as Morphexsynth did it (`plugins/morphexsynth/`); the core is described in `docs/scripting.md`. A plugin
writes its bindings and the glue; the sandbox, the queues, the files, the hot reload, the logs and the documents
come from `scripting/`.

## 1. What the script can say: the binding table

A header with constant tables (`MorphexScript.h` is the example):

- **Commands** (`script::CommandSpec`): a name, positional arguments and an optional table of named fields, each value
  with a kind (`Number` clamped to its range, `Integer` dropped when outside it, `Flag`, `Text`), a range, a default
  and a one-line meaning. At most 12 values per command. An enum field names its table of constants (`enumName`).
- **Hooks** (`script::HookSpec`): the functions a script may define, with the names of their numeric arguments.
- **Constants** (`script::EnumSpec`): tables of named whole numbers; build the entries from the C++ enums
  (`static_cast<int>(Type::Name)`) so a number cannot drift.
- An `appendix` function if a text value needs a list of names that is too long for a table cell.
- A `script::BindingSet` that points to all of these.

## 2. What the audio thread gets: events and commands

Two plain structs (trivially copyable, small): the **event** the pedal forwards (`MorphEvent`) and the **command**
the script's calls become (`MorphCommand`: the part and its new values). A `script::ScriptBinder<Event, Command>`:
`bindings()`, `makeCommand()` (a checked `CommandCall` into the command; return false and a reason to reject it),
`dispatch()` (call the hook for an event: `runtime.call("OnNoteOn", args, n)`), optionally `embeddedModule()`.

## 3. The pedal

- JSON: `"worker": true`; if a user can select a script, a control for the slot and a control output for the state
  (`ScriptState`, 0 to 6).
- Members: the binder and `script::ScriptHost<Event, Command>`; `setBundlePath()` (the adapter calls it before
  `setSampleRate()`) for the folder of the factory scripts, `setWorkScheduler()`, `work()` calling `ScriptHost::work()`,
  `workResponse()` empty. `HostConfig` (folders, sample rate) goes to `configure()`.
- At activation (`reset()`, not real-time): `select()` the script and call `work()` yourself, then drain the commands, so the
  first block already has the sound and the plugin is deterministic.
- Every `process()`: `advance(frames)` (the file check) and drain the commands. `readyGeneration()` changing means a
  new script has taken over and **all** the commands of its top level and start are in the queue: take them (`peekCommand()` while the
  generation equals the ready one, `discardCommand()`) into one new value of the plugin's state and apply it at once
  after releasing what the old script left; later commands are applied one by one.
- Every event that should reach a hook: push it with `pushEvent()` only if `wantsHook(index)` says the loaded
  script defines that hook.
- Never wait for the worker: nothing in the audio thread locks or allocates; a full queue drops and counts.

### Timed MIDI (optional)

A plugin whose scripts make timed output (a sequencer, an arpeggiator, a MIDI effect) adds `c_timerCommands` as the last
two entries of its command table and `c_coreHooks` to its hooks, sets `HostConfig::timerCommandBase`, gives its note
commands a delay field, and runs the pieces of `scripting/` per block as described in `docs/scripting.md` ("Timed
MIDI"): stamp the events, pop the commands with their time, `announce()`, `MidiScheduler::emit()`. A new generation or
a hook that failed (`hookFailures()` went up) is a `panic()` and the commands still queued are dropped; a file that does not load changes nothing. `tests/unit/Scripting/ClockedMidi_test.cpp` is a
complete plugin loop without a plugin; `plugins/midiscript/MidiScriptPedal.h` is the real one (MIDI in and out, no audio).

## 4. Factory scripts, documents, build

- The plugin's own scripts: `plugins/<name>/scripts/*.lua`; CMake copies them into the bundle's `scripts/`.
  Run them with a function like `evaluatePatchScript()` if they only set up a state, and keep the result as a value.
- `plugins/<name>/ScriptDoc.cpp`: `main` writing `apiMarkdown()` and `constantsLua()` into the folder it is given;
  CMake runs it into the bundle's `scripts/` (`API.md`, `constants.lua`).
- `plugins/<name>/scripts/examples/NN-slug.lua` and `plugins/<name>/scripts/AGENTS.md`: about seven examples, one idea
  each, written against the plugin's real API (the first comment line is the title, the next comment lines say what it
  shows and what to change; an `import` is of a factory library only), and the guide for an AI assistant with the
  outline of the existing ones (what a script is, where it goes, the loop, rules in short, mistakes seen most with the
  log messages the host really writes, the index of the examples, a prompt to paste) and no API facts of its own.
  Both ship in the bundle (CMake copies the folder; `scripts/*.md` is in the dependency list).
- `lua-editor`: the plugin in `PLUGINS` of `src/lua_editor/api/extract.py`, `api.json` regenerated
  (`lua-editor/tools/api_from_headers.py`), and the plugin in `PLUGINS` of `lua-editor/tests/test_examples.py`.
- `plugins/CMakeLists.txt`: the plugin's name in `JP_SCRIPTED_PLUGINS` (links `jp_scripting`, builds both).
- `tools/jp.py build-dwarf` builds the `-scripts` target on the Mac and assembles the device bundle from the Mac
  bundle, scripts included.

## 5. Tests that make it trustworthy

- Every example loaded into the plugin's real runtime with a few events, the script state ok and no late or dropped
  commands (`ShipAhoyPedalFactory.EveryExampleLoadsAndRunsWithoutErrorsOrLateCommands`,
  `MidiScript.EveryExampleLoadsRunsAndLeavesNoNoteHanging`,
  `MorphexsynthScript.EveryExampleLoadsSoundsAndSurvivesItsHooks`).

- The binding table against the original where there is one (ranges, defaults, constants with the numbers of the
  source); the hooks with their arguments; a command per kind into the state.
- The factory scripts against a known good value of the state (the oracle), with a mutation check that the
  comparison fails when a value changes.
- In the pedal: take-over as one state, release of what played, a broken file keeps the sound, hot reload, hooks
  only for hooks that exist, zone or channel gating, the selection control waiting, a reload under held notes.
- Through the binary with the host harness (`tests/host/Lv2Host.h` works the jobs inline or on a thread): the
  bundle's contents, a user script loaded, reloaded and broken.
- `tests/unit/Scripting/CoreIsPluginIndependent_test.cpp` fails if `scripting/` names a plugin.
- On the Dwarf: a device test that writes a script over ssh, checks the state output and the sound, edits the file,
  breaks it, removes it, restores what was there.
