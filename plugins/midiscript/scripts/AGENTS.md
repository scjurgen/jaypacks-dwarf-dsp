# Writing a MIDI Script script (guide for an AI assistant)

MIDI Script is a MIDI in, MIDI out plugin. Every incoming message is handed to a Lua script, and what the script sends
is what comes out: it can pass, change, add or drop messages and play things on the clock. This file tells you how to
work here. It holds no names, ranges or limits of the API: those are in `API.md` and `constants.lua` next to it,
generated from the plugin, and always right. Read them first.

## 1. What a script is, and what is in this folder

- A script is one Lua 5.4 text file. Its top-level code runs once when it is loaded. After that the plugin calls the
  hooks the script defines (global functions such as `OnNoteOn`), and the script answers by calling commands (global
  functions such as `NoteOn`, `CC`, `Pass`). A hook that is not defined is not called.
- **A message only goes out when the script sends it.** A message whose hook is missing, or whose hook sends nothing, is
  dropped. `Pass()` sends the message of the hook that is running, unchanged. The real-time messages (MIDI clock) have
  no hook and are not passed on.
- Files here: `API.md` (every command, hook, constant, library and limit), `constants.lua` (the named constants, already
  global in every script), `examples/` (small scripts that run as they are), the factory scripts (`passthrough.lua`,
  `arpeggiator.lua`, ...) and the libraries `chords.lua`, `rhythm.lua` and `scales.lua`. The factory scripts are read only.

## 2. Where a script goes

- On the Dwarf a script is a file `/data/jaypacks/scripts/midiscript/user-1.lua` up to `user-8.lua`. Reach it with
  `ssh root@192.168.51.1` and `scp -O`; the data partition survives a plugin update.
- Select it with the plugin's Script setting (User 1 to User 8). The factory scripts cannot be edited: to change one, or
  an example, copy it to a user slot.
- A script may `import "chords"`, `import "rhythm"` or `import "scales"` (factory libraries, see `API.md`); it cannot read
  other files, and `io`, `os`, `require` and `load` do not exist.

## 3. The loop to follow

1. Write the script to a user slot.
2. The plugin checks the file twice a second and reloads it after a save. Nothing is restarted.
3. Read `/data/jaypacks/scripts/midiscript/user-1.log` (same name as the script, `.log`). It says whether the load
   worked, the message with the line of an error, the calls that were ignored, and every line the script wrote with
   `Log(...)`. The plugin's Script status output shows the same in a number (1 ok, 3 compile error, 4 runtime error,
   5 over the budget, 6 out of memory). Late and Dropped count messages that missed their time or were lost.
4. Fix what the log says and save again.

A script that fails to load (syntax error, error at the top level or in `OnStart`) is rejected and the previous script
keeps playing, so a typo never silences the plugin; but the new script is not running either. Always read the log after
a save. An error in a hook ends the notes that call started, drops what it had queued, and the next event runs again.

## 4. Rules in short

- Hooks are global functions with fixed names and argument lists (`API.md`, Hooks). Write `function OnNoteOn(channel,
  note, velocity)`, not a local.
- Channels count from 1 in the hooks and in the commands. Notes and velocities are 0 to 127; a note on with velocity 0
  reaches `OnNoteOff`. Pitch bend is -8192 to 8191.
- Commands take positional arguments and, last, an optional table of named fields: `Note(1, 60, 100, { lengthTicks =
  Ticks.Sixteenth })`. Time is in milliseconds (`delayMs`, `lengthMs`, `ms`) or ticks (`delayTicks`, `lengthTicks`,
  `ticks`); the clock has 24 ticks per quarter note and `Ticks.Quarter` and the other lengths are in `constants.lua`.
- `Note` sends a note on and, one length later, its note off, which is certain even if the script is replaced. With
  `NoteOn` the script must send the `NoteOff` itself: remember what a key started, and send the note off to the same
  note (a transposing script that changed its setting in between would otherwise leave a note hanging). `Panic()` ends
  everything.
- `Pass()` works only inside the hooks for incoming MIDI (`OnNoteOn`, `OnNoteOff`, `OnCC`, `OnPitchBend`,
  `OnProgramChange`, `OnOther`), not in a timer or a tick.
- `OnTick` and the ticks of a timer need the clock: the host's transport (or the tempo the plugin reads); `OnTimer` is
  set with `SetTimer(id, { ms = ..., ticks = ..., repeating = true })`.
- `import "name"` runs a library once, then its global table is there (`import "scales"` gives `Scales`).
- Randomness is `math.random`; it gives the same numbers on every run. Keep state in locals at the top of the script.
- A hook has a small budget of instructions and milliseconds: no long loops.
- A number field outside its range is clamped. An integer argument (a channel, a note, a velocity, a timer id) outside its
  range, or not whole, makes the call ignored and is written to the log. A field name the command does not know is
  ignored and logged. A wrong type or a missing argument is an error in the hook.

## 5. Mistakes seen most

Each line is a message the plugin writes to the log, and the fix.

| Log says | Cause and fix |
| --- | --- |
| `NoteOn: 'channel' = 0 is outside 1..16, call ignored` | Channels count from 1 here. |
| `NoteOn: 'note' = 128 is outside 0..127, call ignored` | A transposed note must be clamped: `clamp(note + shift, 0, 127)`. |
| `NoteOn: 'velocity' = 0 is outside 1..127, call ignored` | A note on needs a velocity of at least 1. |
| `Note: 'velocity' = 63.5 is not a whole number, call ignored` | Round it: `math.floor(x + 0.5)`. |
| `Note: unknown field 'length' is ignored` | The fields are `lengthMs` and `lengthTicks`. |
| `Pass: Pass() works only inside a hook for incoming MIDI, call ignored` | Pass was called from a timer or a tick. |
| `CC expects 3 argument(s) and got 2` | A command called without all its arguments. |
| `CC: the last argument must be a table of fields, not number` | A value too many: named fields go in braces. |
| attempt to call a nil value (global 'noteOn') | A misspelt command or hook name. Names are case sensitive. |
| the script sounds like the previous one | It was rejected at load; the log has the reason. |
| notes hang | A `NoteOn` without its `NoteOff` (or the off went to another note). Use `Note`, or remember the note sent. |
| nothing comes out | The hook is missing or does not send: add `Pass()` or a command. |

## 6. The examples

| File | Shows | Uses |
| --- | --- | --- |
| `examples/01-pass-through.lua` | everything through; a message without Pass is dropped | OnNoteOn, OnNoteOff, OnCC, OnPitchBend, OnProgramChange, OnOther, Pass |
| `examples/02-transpose.lua` | a shift in semitones, remembering what each key sent | OnNoteOn, OnNoteOff, OnCC, OnPitchBend, NoteOn, NoteOff, Pass |
| `examples/03-chord-from-one-note.lua` | a strummed chord on every key | OnNoteOn, OnNoteOff, NoteOn, NoteOff, import "chords" |
| `examples/04-remap-a-controller.lua` | one controller turned into another | OnCC, CC, Pass |
| `examples/05-quantise-to-a-scale.lua` | every note moved into a scale | OnNoteOn, OnNoteOff, NoteOn, NoteOff, import "scales" |
| `examples/06-timed-arpeggio.lua` | held keys played on a repeating timer in a rhythm | OnNoteOn, OnNoteOff, OnTimer, SetTimer, StopTimer, Note, import "rhythm" |
| `examples/07-panic-on-stop.lua` | all notes off when the transport stops | OnNoteOn, OnNoteOff, OnTransportStop, Pass, Panic |

Start from the example closest to the task and change it; each was run in the plugin.

## 7. A prompt to start an assistant

Paste this into an assistant started in this folder (or give it the files):

> Read `AGENTS.md`, `API.md`, `constants.lua` and the files in `examples/`. Write a MIDI Script script for: (describe
> what should happen to the notes). Use only commands, hooks, constants and libraries that exist in `API.md`. Save it as
> `user-1.lua`, copy it to `/data/jaypacks/scripts/midiscript/` on the Dwarf, wait two seconds, read `user-1.log` and fix
> every problem it names. Tell me what to play to hear each part.
