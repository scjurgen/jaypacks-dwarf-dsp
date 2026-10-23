# Where lua-editor can get the scripting API from

Question: can the content of `API.md` and `constants.lua` be deduced from the DSP repo's source or from its example
scripts, for Morphexsynth and MIDI Script, without a build and without touching the repo?

Answer: yes from the C++ headers, completely for both plugins. No from the scripts alone, except as a cross-check.

## The three possible sources

| Source | Gives | Does not give |
|---|---|---|
| `API.md` and `constants.lua` from a build | everything below, already formatted | needs a build of the DSP repo (Mac or docker) |
| The binding headers (`MorphexScript.h`, `MidiScript.h`, `scripting/Timers.h`) | commands, positional values, table fields with kind, range, default, doc; hooks with argument names; enums with values | the runtime rules that live in code (see the end) |
| The example scripts | names that are used: commands, fields, enum members, hooks | ranges, defaults, docs, enum numbers, anything not used |

`API.md` and `constants.lua` are pure functions of the binding tables (`apiMarkdown()` and `constantsLua()` in
`scripting/ApiDescription.cpp`). Reading the tables gives the same information with no intermediate files.

## What I tried

A throwaway parser (not in the repo yet) read the tables of both plugins from the headers, then checked every example
script against the result. It checks that each capitalised call is a command, a hook or a global the script defines
itself; that each table key is a field of that command; the number of positional values; that every literal number is
inside its range; that every `Enum.Member` exists; that every defined hook exists.

| | Morphexsynth | MIDI Script |
|---|---|---|
| Commands | 14 | 10 |
| Fields of command tables | 45 | 19 |
| Hooks | 6 | 11 |
| Enums, members | 6, 44 | 1, 8 |
| Header counts equal the source by hand | yes | yes |
| Scripts checked | 10 | 9 |
| Problems found by the check | 0 | 0 |
| Commands, fields, members the scripts use | 12 of 14, 42 of 45, 17 of 44 | 5 of 10, 4 of 19, 1 of 8 |

The last row is why the scripts cannot be the source: they cover a part of the names and none of the ranges.

## Morphexsynth

- All tables are in `plugins/morphexsynth/MorphexScript.h`.
- The enum numbers come from other headers: `dsp/Synthesizer/MpeRouting.h` (`CtrlDimension`, `CtrlTarget`,
  `CtrlValueType`), `dsp/Synthesizer/MpeCurve.h` (`CtrlCurve`), `dsp/Generators/SynthLfo.h` (`LfoType`),
  `plugins/morphexsynth/MorphPatch.h` (`OscWaveform`). None has explicit values, so a value is the position in the
  declaration. The Lua names differ from the C++ types (`CtrlSource` is `CtrlDimension`, `LfoWaveform` is `LfoType`),
  so the names in the table are the ones to use.
- `SetFilter` takes `type` as text. The accepted names are 15 aliases in `MorphexScript.h` and the 35 mix names in
  `dsp/Filters/PoleMixingFilter.h`. They are only in the appendix of `API.md`, so completion of this value needs those
  two places read as well.
- Hooks count channels from 0 (only the hook documentation says so).
- Every script starts with `import "constants"`, which does nothing (the tables are global anyway).

## MIDI Script

- Tables are in `plugins/midiscript/MidiScript.h`. `SetTimer`, `StopTimer`, `OnTick`, `OnTransportStart`,
  `OnTransportStop` and `OnTimer` come from `scripting/Timers.h` by reference, so that file must be read too.
- The one enum, `Ticks`, has explicit values in the table. No other header is needed, and no constants file.
- Several commands share named value specs (`delayMs`, `delayTicks`, ...), so the parser must resolve names.
- Channels count from 1. `OnOther(status, data1, data2)` exists only here.
- `import "scales"`, `"chords"` and `"rhythm"` load Lua libraries that define the globals `Scales`, `Chords` and
  `Rhythm` (tables and functions). They are in no API description. lua-editor would have to learn them by scanning the
  library files for a global table and its `function Name.member(...)` definitions. Where `import` looks: the user
  folder first, then the factory folder of the bundle (`scripting/ScriptHost.h`), so a library in the factory folder is
  invisible to an editor that sees only the user folder.

## What no source gives

These are rules in code, not in tables. lua-editor has to carry them itself, and I have read them from the source:

- the sandbox (`scripting/LuaRuntime.cpp`): which libraries and functions exist, that `print` is `Log`
- limits (`scripting/LuaRuntime.h` and `ScriptHost.h`): script size 256 KiB, instruction and time limits, text values
  up to 31 characters, name lengths
- `Pass()` works only inside the hooks for incoming MIDI (a runtime error otherwise; only the doc says so)
- a wrong type is a script error; a value out of range drops the call with a log line

## Risks of reading the headers

- The parser depends on the layout of the tables: `std::array<T, N>{{...}}`, values in the order of `ValueSpec`,
  `.data()` references, the `JP_MORPHEX_ENTRY` macro, enums declared elsewhere. A new plugin written in a different
  style, or a change of `Binding.h`, can break it. It must fail loudly (counts and a self-check), never produce a
  partial API quietly.
- The headers are the truth of the source, a build is the truth of what runs on the device. They can differ if the
  device has an older bundle. The generated `api.json` should record the files it came from (path and hash) so lua-editor
  can say when it is out of date.

## Recommendation

1. Milestone 2 becomes `tools/api_from_headers.py`: DSP repo path in, one `api.json` per plugin out, in the schema of
   `docs/dsp-alignment.md`. `API.md` and `constants.lua` are not needed as input any more. The converter from `API.md`
   stays an optional fallback only if you prefer it.
2. The factory scripts of both plugins become a regression corpus in lua-editor's tests: with the API-aware checks of
   milestone 6 they must give zero warnings.
3. A separate small step scans Lua library files for the globals they define (MIDI Script needs it, Morphexsynth does
   not). Decide first where lua-editor may look for libraries: the scripts folder on the device only, or also a copy of the
   factory scripts.
4. In `docs/dsp-alignment.md` the request for `api.json` from ScriptDoc stays, now as a way to replace the parser, not
   as a requirement.
