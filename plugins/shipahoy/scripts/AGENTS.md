# Writing a Ship Ahoy script (guide for an AI assistant)

Ship Ahoy is a filter that rings like a resonator. It has no way to move on its own: a Lua script decides, step by
step, where the frequency goes. This file tells you how to work here. It holds no names, ranges or limits of the API:
those are in `API.md` and `constants.lua` next to it, generated from the plugin, and always right. Read them first.

## 1. What a script is, and what is in this folder

- A script is one Lua 5.4 text file. Its top-level code runs once when it is loaded. After that the plugin calls the
  hooks the script defines (global functions such as `OnStep`), and the script answers by calling commands (global
  functions such as `SetCutoff`). A hook that is not defined is not called.
- Commands do not return anything to hear; they tell the plugin what to do and when (the event's time plus an optional
  delay). The getters (`GetX`, `GetY`) read the two knobs of the plugin.
- Files here: `API.md` (every command, hook, constant, library and limit), `constants.lua` (the named constants, already
  global in every script), `examples/` (small scripts that run as they are), and the factory scripts (`hop.lua`,
  `sequence.lua`, ..., plus the library `notes.lua`). The factory scripts are read only.

## 2. Where a script goes

- On the Dwarf a script is a file `/data/jaypacks/scripts/shipahoy/user-1.lua` up to `user-8.lua`. Reach it with
  `ssh root@192.168.51.1` and `scp`; the data partition survives a plugin update.
- Select it with the plugin's Script setting (User 1 to User 8). The factory scripts cannot be edited: to change one, or
  an example, copy it to a user slot.
- A script may `import "notes"` (a factory library, see `API.md`); it cannot read other files, and `io`, `os`, `require`
  and `load` do not exist.

## 3. The loop to follow

1. Write the script to a user slot.
2. The plugin notices the saved file within a second and reloads it. Nothing is restarted.
3. Read `/data/jaypacks/scripts/shipahoy/user-1.log` (same name as the script, `.log`). It says whether the load worked,
   the message with the line of an error, the calls that were ignored, and every line the script wrote with `Log(...)`.
   The plugin's Script status output shows the same in a number (1 ok, 3 compile error, 4 runtime error, 5 over the
   budget, 6 out of memory).
4. Fix what the log says and save again.

A script that fails to load (syntax error, error at the top level or in `OnStart`) is rejected and the previous script
keeps sounding, so a typo never silences the plugin; but the new script is not running either. Always read the log after
a save. An error in a hook ends only that call; the next event runs again.

## 4. Rules in short

- Hooks are global functions with fixed names and argument lists (`API.md`, Hooks). Write `function OnStep(step)`, not
  a local.
- Commands take positional arguments and, last, an optional table of named fields:
  `SetCutoff(60, { glide = 0.5 })`. A field is written by name; there is no other order.
- Units: notes are MIDI notes (69 = 440 Hz, fractions are cents); the clock has 96 ticks per quarter note; time
  fields are in milliseconds (`ms`, `delayMs`) or ticks (`ticks`, `delayTicks`). `GetX()` and `GetY()` give 0 to 100.
- `step` and `tick` count from 0; a Lua list starts at 1: use `notes[step % #notes + 1]`.
- `import "name"` runs a library once, then its global table is there (`import "notes"` gives `Notes`).
- Randomness is `math.random`; it gives the same numbers on every run. Keep state in locals at the top of the script.
- Do work in the hook, not in loops that wait: a hook has a small budget of instructions and milliseconds.
- A number field outside its range is clamped. An integer argument (a timer id, a filter type) outside its range, or not
  whole, makes the call ignored and is written to the log. A field name the command does not know is ignored and
  logged. A wrong type or a missing argument is an error in the hook.

## 5. Mistakes seen most

Each line is a message the plugin writes to the log, and the fix.

| Log says | Cause and fix |
| --- | --- |
| `SetCutoff: unknown field 'glid' is ignored` | The field is misspelt; take the name from `API.md`. |
| `SetTimer: 'id' = 9 is outside 0..7, call ignored` | An integer argument out of range; the call did nothing. |
| `SetTimer: 'id' = 1.5 is not a whole number, call ignored` | An integer argument must be whole: use `math.floor`. |
| `SetCutoff expects 1 argument(s) and got 0` | A command called without its arguments. |
| `SetCutoff: the last argument must be a table of fields, not number` | Named fields go in braces: `{ glide = 0.5 }`. |
| `SetCutoff: 'note' must be a number, not nil` | A variable that is nil, often a list index one past the end (lists start at 1). |
| attempt to call a nil value (global 'SetCutof') | A misspelt command or hook name. Names are case sensitive. |
| the script sounds like the previous one | It was rejected at load; the log has the reason. |
| nothing moves | No hook that calls `SetCutoff` is defined, or the hook name is misspelt (`OnStep`, not `onStep`). |

Also: `glide` is a fraction of the step (0 to 1), while `GetX()` is a percent (0 to 100): divide by 100.

## 6. The examples

| File | Shows | Uses |
| --- | --- | --- |
| `examples/01-smallest.lua` | the smallest script: one note per step | OnStep, SetCutoff |
| `examples/02-note-list-glide.lua` | a list of notes in turn with a glide | OnStart, OnStep, SetCutoff, SetFilterType, SetResonance |
| `examples/03-random-hold.lua` | a random note of a scale on every step, X and Y as knobs | OnStart, OnControl, OnStep, GetX, SetCutoff, SetFilterType, SetResonance |
| `examples/04-speed-timer.lua` | speed changes on a repeating schedule | OnStart, OnTimer, OnStep, SetTimer, SpeedRatio, SetCutoff, SetResonance |
| `examples/05-follow-played.lua` | the filter follows the notes played into the plugin | OnNewPitch, OnStep, SetCutoff, import "notes" |
| `examples/06-transport-reset.lua` | restart the pattern when the host's transport starts | OnTransportStart, OnTransportStop, OnStep, SetCutoff, SetDrift, SpeedRatio |

Start from the example closest to the task and change it; each was run in the plugin.

## 7. A prompt to start an assistant

Paste this into an assistant started in this folder (or give it the files):

> Read `AGENTS.md`, `API.md`, `constants.lua` and the files in `examples/`. Write a Ship Ahoy script for: (describe the
> sound you want). Use only commands, hooks and constants that exist in `API.md`. Save it as `user-1.lua`, copy it to
> `/data/jaypacks/scripts/shipahoy/` on the Dwarf, wait two seconds, read `user-1.log` and fix every problem it names.
> Tell me what each knob does.
