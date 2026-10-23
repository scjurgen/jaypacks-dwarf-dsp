# lua-editor

Editor for the Lua scripts of the plugins. Plan: `PLAN.md`. What lua-editor needs from the DSP repo:
`docs/dsp-alignment.md`.

## Start

`dev-scripts/dev-lua-editor.sh` does the setup below when needed and starts the editor with two mounts in the tree:
`repo/` is `plugins/<plugin>/scripts` of this repository (the working copy, git is the undo), `device/` is the
Dwarf's `/data/jaypacks/scripts` (192.168.51.1, over ssh, connected the first time it is used, so the editor also
works without the device). `dev-scripts/dev-lua-editor.sh --local` never connects. Further options are passed on.

## Workflow

1. Edit a script in `repo/<plugin>/` (in the editor, or let an LLM edit the file; Ctrl+R reloads a tab after that).
2. Push it to the device: Ctrl+U, the Device menu or the tree's context menu. A script goes to a user slot: the
   plugins only load `user-1.lua` to `user-8.lua` from the user folder, so the first push asks for the slot (free ones
   first, those in use marked) and remembers it for the session. A library (a script that defines a global table, like
   `notes.lua`) is pushed under its own name without asking, as `import` finds it there. "Push on save" in the Device
   menu pushes after every save.
3. Select that user script in the plugin's Script setting. The plugin hot-reloads a changed file within about a second
   (it watches only the selected script, so a changed library is noticed when the script that imports it reloads);
   the Log panel shows the slot's `user-N.log` for a pushed repo script.
4. Repeat until it sounds right. To accept: commit the repo file (it ships as a factory script with the next deploy; a
   new script also needs an entry in the plugin's script table), then Device > Remove from device, so that the slot
   no longer holds the draft.

Pull (Ctrl+Shift+U) brings a slot back into its repo script (it asks for a name when the slot was not pushed from
the repo in this session); either side of a pair can be selected. Both ask before replacing a file that this
editor did not write. A folder: push and pull handle its libraries and the scripts pushed before; Remove from device
removes all the scripts of a device folder after listing them.

What happened is told twice: the status line shows the latest message, the **Activity** tab (next to Problems and Log)
keeps every message of the session with its time and the full paths, for example `Pull to the repo: 1 written
(user-3), 3 not on the device - never pushed or removed (chords, rhythm, scales)`. It comes to the front when
something was skipped or failed. A file that is missing at the source is skipped and reported, never a failure of
the whole push or pull. A real error opens an alert with a sentence on what went wrong, a hint on what to try and
"Show Details" (operation, exception, traceback). Questions that replace or delete name their action on the button
(Overwrite, Remove, Delete) and list the files, with sizes, behind "Show Details".

F1 (Help > API reference) writes the API of the plugin of the current tab as a self-contained HTML page into the user's
cache folder and opens it in the browser, at the command, hook, constant or library under the cursor. Without a tab it
asks for the plugin. `python -m lua_editor.api.html <plugin> --apis lua-editor/apis --out api.html` makes the same page
without the editor.

The page also has the plugin's examples (`plugins/<plugin>/scripts/examples/`, each with a Copy button and the editor's
token colours) and its complete factory scripts (collapsed). Every command, hook, constant and library has a "Used in"
line that links to the examples that use it. Help > "Copy API brief for an LLM" puts one Markdown text on the clipboard:
the plugin's guide for an AI assistant (`AGENTS.md`), the commands, hooks, constants, libraries, limits and sandbox, and
every example; `python -m lua_editor.api.markdown <plugin> --apis lua-editor/apis` writes the same text to a file or to
standard output. Paste it into an assistant, together with a task, to have a script written (the prompt to use is at the
end of the guide).

An example is a small script that runs as it is: open it from `repo/<plugin>/examples/`, push it to a user slot like any
script, change it. The tree shows the `examples/` folder, but pushing a plugin's folder sends only the libraries and
the scripts pushed before, never the examples. `examples/` and `AGENTS.md` are sources of `api.json` (see below), so after
a change of either run `python lua-editor/tools/api_from_headers.py`; `--check` fails when `api.json` is out of date.

## Examples and the guide in `api.json`

`tools/api_from_headers.py` also reads, per plugin, `scripts/examples/*.lua` (key `examples`), the factory scripts
(`scripts`, not the libraries and not `user-*.lua`, which are drafts and never a source) and `scripts/AGENTS.md`
(`guide`). Per example: id, file, title (the first comment line), summary (the rest of the leading comment block), code
and `uses`, the commands, hooks, constants and libraries the code names, found with the tokenizer (comments, strings and
fields after a dot do not count). The files and their hashes are listed in `sources`, so the freshness test covers them.
`tests/test_examples.py` checks every example against the real API (syntax, `check_api`, imports of factory libraries
only, a title and a summary), the index of `AGENTS.md` against the folder, every name `AGENTS.md` uses against
`api.json`, and that the page and the brief carry every example verbatim. The C++ tests of each plugin load every
example into the real runtime.

## Setup

Uses the repository venv `.venv` (`dev-scripts/dev-venv.sh` creates it); run from the repository root:

```
.venv/bin/pip install -r lua-editor/requirements.txt
.venv/bin/pip install --no-deps -e lua-editor
QT_QPA_PLATFORM=offscreen .venv/bin/python -m pytest lua-editor/tests
```

Regenerate the API files after the DSP sources change (the DSP repo is only read; `--check` writes nothing):

```
.venv/bin/python lua-editor/tools/api_from_headers.py [--dsp PATH] [--check]
```

Options of `python -m lua_editor` (see `--help`): `--repo` (default: the repository this package is in), `--device
[HOST]`, `--no-device`, `--device-root` (default `/data/jaypacks/scripts`), `--user`, `--key`, `--accept-new-host`,
`--apis` (folder with the `<plugin>/api.json` files, default `apis`) and `--root FOLDER`, which edits one folder laid
out like the device's scripts folder instead (no push and pull then). The ssh login is key based as for
`ssh root@192.168.51.1`, the host in known_hosts.
The Log panel shows the plugin's `<name>.log` of the script in the current tab (after a save, with Ctrl+L).
The ssh tests (`tests/test_shell_fs.py`) run against the Dwarf and skip themselves when it is not reachable
(`LEDIT_DWARF_HOST` picks another address).

Indentation: Enter keeps the indentation of the line and goes one level (4 spaces) deeper after `function ...`, `if`,
`for ... do`, `while ... do`, `repeat`, `else`, `elseif` and an unclosed `(` or `{`; between `{` and `}` the closer moves
to its own line. Typing `end`, `else`, `until`, `}` or `)` alone on a line steps it back one level. The last line of a
call split over lines hands the indentation back to the line that opened it. Tab inserts spaces up to the next
multiple of 4 (a selection: indents its lines), Shift+Tab takes one level off. Strings and comments do not count, a
long string or comment is left as typed, and one Enter with its indentation is one undo step. Continuation lines that
are aligned under an opening bracket (the style of some factory scripts) are not reproduced; the next line is one
level deep instead. Enter on a finished word (`end`) is a new line even while the completion list is open.

In the editor: Ctrl+Space opens completion by hand; it also opens by itself after two letters, after a dot, inside a
command's `{ ... }` and in the quotes of `import "..."` and of text values. `--apis` picks the folder with the
`<plugin>/api.json` files (default `apis`). A file gets the API of the folder it is in, named like the plugin.

