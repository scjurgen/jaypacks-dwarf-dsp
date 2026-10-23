# Alignment with the DSP repo (jaypacks-dwarf-dsp)

lua-editor never changes that repo. Only the generator `tools/api_from_headers.py` reads its sources, at
development time; the editor itself needs the generated files and, for the `repo/` mount, the scripts in
`plugins/<plugin>/scripts` (it writes there when the author saves). This page lists what lua-editor depends on and what would help, so the two can be
aligned when convenient. Nothing here is urgent; lua-editor works without it.

## What lua-editor relies on today

| Fact | Source in the DSP repo |
|---|---|
| Scripts are in `/data/jaypacks/scripts/<plugin>/<name>.lua` | `scripting/ScriptDirectory.h`, `docs/device.md` |
| Script name: letters, digits, `-`, `_`, 1 to 47 characters (`detail::validName`); `import` names allow 40 | `scripting/ScriptHost.h`, `scripting/LuaRuntime.cpp` |
| A script file is at most 256 KiB (`c_maxScriptBytes`); lua-editor does not check this yet | `scripting/ScriptHost.h` |
| A command needs at least its positional values and takes at most one table more; a wrong Lua type is a script error; a value out of range drops (integers) or limits (numbers) the call; an unknown field is only logged (`runCommand`) | `scripting/LuaRuntime.cpp` |
| An unknown filter type rejects the call | `plugins/morphexsynth/MorphexScript.h` |
| sshd on the Dwarf has no SFTP; files move by `scp -O` or `ssh ... 'cat > file'` | `docs/device.md` |
| A changed file is reloaded within about a second (mtime polled twice a second) | `docs/device.md` |
| `<name>.log` is written next to the script after each load and failing hook | `scripting/ScriptHost.h` |
| Sandbox: libraries, removed functions, globals | `scripting/LuaRuntime.cpp`, `scripting/ApiDescription.cpp` |
| The command, hook and constant tables | `plugins/<name>/*Script.h`, generated into `API.md` and `constants.lua` |

If one of these changes, lua-editor needs to know.

## Requests, in order of value

1. `api.json` from ScriptDoc. lua-editor reads the binding headers itself (`tools/api_from_headers.py`, reasons in
   `docs/api-source-analysis.md`). The reader relies on the layout of the `Binding.h` tables (`std::array<T, N>{{...}}`,
   `.data()` references, the `JP_MORPHEX_ENTRY` macro, enum values by declaration order), on `LuaRuntime.cpp` setting up
   the sandbox as it does now, and on the Lua libraries being column-0 global tables; it stops with an error when a
   count or a layout does not match. If `ScriptDoc.cpp` wrote the same JSON from the `BindingSet`, that reader would
   not be needed. The file lua-editor writes (`apis/<plugin>/api.json`, schema 1) has these parts: `plugin`, `title`, `lua`
   (5.4.8), `limits` (text length, script size, instruction, time and memory limits), `sandbox` (libraries, removed
   globals and fields, the globals the runtime adds), `noopImports`, `commands` (positional `args` and table `fields`,
   each with kind, min, max, default, doc, enum, and for `SetFilter.type` the accepted `values`), `hooks`, `enums`,
   `libraries` and `sources` (every file read, with a hash).
2. The limits and the sandbox as data, in the same JSON, so that they need not be read out of `LuaRuntime.h`,
   `ScriptHost.h` and `LuaRuntime.cpp`.
3. The accepted names of a text value (the filter types of `SetFilter`) as data on the value, instead of the appendix
   markdown and two headers.
4. The Lua libraries of a plugin (`scales.lua`, `chords.lua`, `rhythm.lua`) are found by their file name and column-0
   layout. A header comment of the form `--   Name.member(args)    what it does` gives their docs; keeping that form in
   new libraries keeps completion good.

## Changes in lua-editor that may need the DSP repo

- None planned. When the DSP sources change, run `python tools/api_from_headers.py --check` in lua-editor; it says which
  `api.json` is out of date, and without `--check` it rewrites them.
