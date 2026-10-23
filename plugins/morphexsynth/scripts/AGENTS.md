# Writing a Morphexsynth script (guide for an AI assistant)

Morphexsynth is a polyphonic synthesizer with three oscillators, a filter, envelopes, an LFO, a routing matrix and an
effects bus. Its sound is a Lua script: a list of commands that set the patch, and hooks that react to MIDI. This file
tells you how to work here. It holds no names, ranges or limits of the API: those are in `API.md` and `constants.lua`
next to it, generated from the plugin, and always right. Read them first.

## 1. What a script is, and what is in this folder

- A script is one Lua 5.4 text file. Its top-level code and `OnStart` run once when it is loaded, and the commands they
  call (`SetOscillator`, `SetFilter`, `SetCtrlSlot`, ...) make **the patch**. The patch starts from a blank one
  (everything off, no oscillator sounding): a sound needs at least an oscillator with a level, and an amplitude
  envelope and a filter you can hear. What the script leaves out stays at the blank value.
- After that the plugin calls the hooks the script defines (`OnNoteOn`, `OnNoteOff`, `OnCC`, `OnProgramChange`,
  `OnPitchBend`). A hook that is not defined is not called. Commands called from a hook change the sound later, one at
  a time.
- Files here: `API.md` (every command, hook, constant and limit), `constants.lua` (the named constants such as
  `OscWaveform.Saw`, already global in every script), `examples/` (small patches that run as they are) and the ten
  factory patches (`warm-bass.lua`, `airy-pad.lua`, ...). The factory patches are read only: copy one to start from a
  complete sound.

## 2. Where a script goes

- On the Dwarf a script is a file `/data/jaypacks/scripts/morphexsynth/user-1.lua` up to `user-8.lua`. Reach it with
  `ssh root@192.168.51.1` and `scp -O`; the data partition survives a plugin update.
- Select it with the plugin's Script setting (User 1 to User 8). While a script is selected the Patch setting is ignored.
  To change a factory patch or an example, copy it to a user slot.
- A script may `import "name"` for another script file of the same folder; `io`, `os`, `require` and `load` do not exist.

## 3. The loop to follow

1. Write the script to a user slot.
2. The plugin checks the file twice a second and reloads it after a save. Nothing is restarted. The notes that sound are
   released and the new patch is loaded as a whole.
3. Read `/data/jaypacks/scripts/morphexsynth/user-1.log` (same name as the script, `.log`). It says whether the load
   worked, the message with the line of an error, the calls that were ignored, and every line the script wrote with
   `Log(...)`. The plugin's Script status output shows the same in a number (1 ok, 3 compile error, 4 runtime error,
   5 over the budget, 6 out of memory).
4. Play a note and listen. Fix what the log says, or what you hear, and save again.

A script that fails to load (syntax error, error at the top level or in `OnStart`) is rejected and the previous sound
goes on, so a typo never silences the synth; but the new script is not running either. Always read the log after a save.
An error in a hook ends only that call; the next event runs again.

## 4. Rules in short

- Hooks are global functions with fixed names and argument lists (`API.md`, Hooks). Write `function OnStart()`, not a local.
- Channels count from 0 in the hooks of this plugin (MIDI channel 1 is 0). Notes are 0 to 127, velocity 1 to 127, pitch
  bend -8192 to 8191, controller values 0 to 127.
- Commands take positional arguments (an index, a preset) and, last, an optional table of named fields:
  `SetOscillator(0, { waveform = OscWaveform.Saw, level = 0.6 })`. The three oscillators are 0 to 2, the routing slots
  0 to 9.
- Units: times are in milliseconds (`attackMs`), levels are 0 to 1 unless the API says otherwise, the volume is in dB,
  the filter cutoff is a note number (60 = middle C, up to 135), pitch depths are in semitones.
- A filter type is a text (`type = "LP4"`), not a constant; the names are listed in `API.md`.
- The routing matrix is how the player's hands reach the sound: a slot reads a source (X pitch bend, Y mod wheel, Z
  pressure, Velocity, Note, the envelopes), shapes it with a curve, scales it with the depth and adds it to a target
  (`CtrlSource`, `CtrlCurve`, `CtrlTarget`, `CtrlValueType` in `constants.lua`). A slot with the target `None` is off.
- Randomness is `math.random`; it gives the same numbers on every run. Keep state in locals at the top of the script.
- A hook has a small budget of instructions and milliseconds: no long loops.
- A number field outside its range is clamped. An integer argument (an oscillator index, a slot, a preset) outside its
  range, or not whole, makes the call ignored and is written to the log. A field name the command does not know is
  ignored and logged. A wrong type or a missing argument is an error.

## 5. Mistakes seen most

Each line is a message the plugin writes to the log, and the fix.

| Log says | Cause and fix |
| --- | --- |
| `SetOscillator: 'index' = 3 is outside 0..2, call ignored` | There are three oscillators, 0 to 2. |
| `SetCtrlSlot: 'slot' = 10 is outside 0..9, call ignored` | There are ten routing slots, 0 to 9. |
| `SetOscillator: unknown field 'wave' is ignored` | The field is `waveform`; take the names from `API.md`. |
| `SetFilter: unknown filter type 'LP5', call ignored` | A filter type is one of the texts listed in `API.md`. |
| `SetFilter: 'type' must be a string, not number` | Write the type as text in quotes: `type = "LP4"`. |
| `SetDistortion expects 1 argument(s) and got 0` | A command called without its arguments. |
| `SetFilter: the last argument must be a table of fields, not number` | Named fields go in braces: `SetFilter({ cutoff = 60 })`. |
| attempt to index a nil value (global 'OscWaveform') | A misspelt constant. Names are case sensitive. |
| the script sounds like the previous one | It was rejected at load; the log has the reason. |
| silence | No oscillator has a level, or the amplitude envelope sustains at 0, or the cutoff is too low. |
| a hook does nothing | The hook name is misspelt (`OnNoteOn`, not `onNoteOn`), or its channel test counts from 1. |

## 6. The examples

| File | Shows | Uses |
| --- | --- | --- |
| `examples/01-a-patch-in-onstart.lua` | the smallest useful patch | OnStart, SetVolume, SetOscillator, SetAmpEnvelope, SetFilter |
| `examples/02-mod-wheel-to-the-filter.lua` | the mod wheel opens the filter | SetCtrlSlot, CtrlSource, CtrlTarget |
| `examples/03-pressure-to-resonance.lua` | pressure and velocity shape the sound | SetCtrlSlot, CtrlSource, CtrlValueType |
| `examples/04-an-effects-chain.lua` | distortion, chorus, phaser and reverb | SetDistortion, SetChorus, SetPhaser, SetReverb |
| `examples/05-vibrato-with-the-lfo.lua` | a slow LFO on the pitch | SetLfo, LfoWaveform |
| `examples/06-patches-by-program-change.lua` | program change switches between patches | OnProgramChange, SetOscillator, SetAmpEnvelope, SetFilter |
| `examples/07-wave-morph-on-a-controller.lua` | a controller morphs the waveform while notes sound | OnCC, SetWaveMorph |

Start from the example closest to the task and change it; each was run in the plugin.

## 7. A prompt to start an assistant

Paste this into an assistant started in this folder (or give it the files):

> Read `AGENTS.md`, `API.md`, `constants.lua` and the files in `examples/`. Write a Morphexsynth script for: (describe
> the sound and how the hands should shape it). Use only commands, hooks and constants that exist in `API.md`. Save it
> as `user-1.lua`, copy it to `/data/jaypacks/scripts/morphexsynth/` on the Dwarf, wait two seconds, read `user-1.log`
> and fix every problem it names. Tell me which controller does what.
