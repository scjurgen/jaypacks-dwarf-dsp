# lua-editor plan

A desktop editor for the Lua scripts of the jaypacks Dwarf plugins (Morphexsynth, MIDI Script, Ship Ahoy). It edits files on the
device over ssh, highlights Lua 5.4 plus the plugin's own commands, and checks syntax and API use while typing.

lua-editor does not depend on the DSP repo's code. At run time it reads only the scripts in `plugins/<plugin>/scripts` (the
`repo/` mount); everything else it needs comes from the generated `apis/`. What it needs from that repo is written down in
`docs/dsp-alignment.md`.

## Decisions

- Python 3, PySide6 (LGPL). Own `QSyntaxHighlighter`, line number area and error markers.
- Lua checks with `lupa` (Lua 5.4, same as the device's 5.4.8), compile only, never run a script.
- Device access by ssh only: the Dwarf's sshd has no SFTP. Shell backend with `paramiko` `exec_command`.
- The scripting API comes from lua-editor's own `api.json`, one per plugin, read from the DSP repo's binding headers
  (analysis and risks: `docs/api-source-analysis.md`). `API.md` and `constants.lua` are not needed.
- Platforms: Windows, Linux, macOS (macOS tested later).

## Facts about the target (from the DSP repo docs)

- Scripts: `/data/jaypacks/scripts/<plugin>/<name>.lua`. Name: letters, digits, `-`, `_`.
- The plugin hot-reloads a changed file within about a second (mtime polled twice a second), so saves must be atomic.
- The plugin writes `<name>.log` next to the script: status, error line, the script's own `Log` lines.
- Sandbox: base (no dofile, loadfile, load, collectgarbage, warn), math, table, string (no dump). No os, io, package,
  debug, coroutine, utf8. Globals: `Log`, `print` (same as Log), `clamp`, `lerp`, `import "name"`, plus the commands,
  hooks and constants of the plugin.
- Text values: at most 31 characters. Integer values outside their range drop the call; a wrong type is an error.

## Layout

```
lua-editor/
  pyproject.toml
  PLAN.md
  docs/dsp-alignment.md
  apis/<plugin>/api.json                             generated, with the source files it came from
  tools/api_from_headers.py                          DSP binding headers -> api.json
  src/lua_editor/
    remote/  base.py (RemoteFS), local.py, shell.py, repo.py (plugins/<plugin>/scripts as <plugin>/), mount.py (MountFS),
             transfer.py (plan and run a copy between mounts)
    editor/  widget.py, highlighter.py, palette.py (token colours, also used by the page), completer.py
    check/   syntax.py (lupa), api_check.py
    ui/      mainwindow.py, filetree.py, problems.py, logpanel.py
    api/     cpp_tables.py (binding tables), lua_modules.py (library scanner), extract.py, model.py (loader),
             html.py (the API as a page for the browser), markdown.py (the brief for an LLM)
  tests/
```

## RemoteFS

Paths are POSIX, relative to the root of the scripts tree. Operations: list, stat, read, write (atomic: temp file, then
rename), mkdir, rename, delete. `LocalFS` works on a folder, `RepoFS` shows the repo's scripts folders like the device's, `MountFS` puts several of them
under one root (`repo/`, `device/`); `ShellFS` runs `ls`, `cat`, `cat > tmp && mv` over ssh
and must work with busybox tools. One contract test suite runs against every backend. Conflict check on save: size and
mtime (or hash) compared with what was loaded.

## Milestones

1. Project skeleton, `RemoteFS`, `LocalFS`, contract tests.
2. `tools/api_from_headers.py` and `api.json` for morphexsynth and midiscript, with the source files and hashes recorded.
3. Main window: lazy tree, tabs, open, save, new, rename, delete, dirty marker, name validation. I/O off the UI thread.
4. Editor: Lua highlighting, bound commands, hooks and constants in their own colors, line numbers, autocomplete for
   commands, table fields and `Enum.` members, doc and range in a status line.
5. Syntax check with lupa (debounced), markers and a problems panel.
6. API-aware checks: removed libraries and functions (os, io, load, ...), unknown command or field, constant not in
   its enum, text over 31 characters, `import` of a missing file.
7. Device backend `ShellFS` (host key check, ssh config), log panel that fetches `<name>.log` after each save.
8. Packaging (PyInstaller) and macOS testing.

## Status

Milestones 1 to 7 are done, plus the repo and device workflow, the Activity list, auto indent and the examples and the AI guide below (439 tests).

Milestone 7: `remote/shell.py`. `ShellFS` runs busybox `stat -L -c`, `find -exec stat`, `cat`, `mv`, `mkdir`, `rm` over
paramiko's `exec_command` (injected `Runner`, so the logic is apart from the connection); a write is `cat > .name.lua-editor-tmp
&& mv -f` in the same folder. `SshRunner` checks the host key against known_hosts (`--accept-new-host` to trust a new
one) and takes user and key from `~/.ssh/config`. The contract tests run against the Dwarf (`tests/test_shell_fs.py`, in
`/data/jaypacks/lua-editor-test/`, removed afterwards; skipped when the device is unreachable). The Log panel shows
`<name>.log` of the script in the current tab: on opening a tab, 1.5 s after each save (the plugin polls twice a second),
and with Ctrl+L. The tree lists only folders and `.lua` files, so a log cannot be opened as a script. mtime has whole seconds, so a change within the same second and of the same size is not noticed as a conflict.

Milestone 6, `check/api_check.py`: if the script compiles, its use of the plugin's API is checked (a syntax error hides
these, so there is one thing to fix at a time). Errors are what stops the script or leaves a name undefined: a call of
an unknown function (with "did you mean"), the removed libraries and functions (`os`, `io`, `load`, `string.dump`,
...), an unknown `Enum.Member` or library member, a library used without its `import`, an `import` of a script that is
not in the folder, a wrong number of arguments, a wrong type for a literal value, a last argument that is not a table,
a script over 256 KiB. Warnings are what the plugin ignores or adjusts and logs: an unknown field, a nameless entry in
a field table, an integer that is out of range or not whole (the call is dropped), a number out of range (limited), a
text that is too long or not a known name, a constant of the wrong enum, a hook the plugin does not have (`OnTick` in
Morphexsynth), a hook declared `local`. Warnings get an amber mark, errors a red one; the tab shows `!` or `?`.
The checks are quiet when unsure: a name the script declares or assigns anywhere is never reported, whatever its scope.
Not checked: `Pass()` outside the MIDI hooks (needs to know the enclosing function), values that are not literals,
and unknown names that are not calls. The 22 factory scripts give no problems, which the tests keep true.

Milestone 5: 400 ms after the last change the script is compiled with lupa's
Lua 5.4 (`load` in text mode, never run); Lua stops at the first syntax error, so there is at most one. It gets a wavy
underline, a red dot in the margin, a tooltip, an entry in the Problems panel (double click jumps to it), a `!` on the
tab and a count in the status bar. An error at the end of the text is shown on the last line that has text. The bundled
Lua is some 5.4.x, the device has 5.4.8; the syntax is the same throughout 5.4. A script with an error can be saved.

Milestone 4: the editor highlights Lua 5.4 and the plugin's own names (commands, hooks,
constants, runtime globals, library members, fieldgo ahead names), shows line numbers and completes commands, table fields,
`Enum.` members, filter type names, `import "..."` names, hook definitions and library members (after the `import`).
A line under the editor says what is under the cursor: signature, kind, range, default, doc.
Libraries come from two places: the factory ones in `api.json`, and the other scripts of the script's own folder,
scanned when it opens and again when a script of that folder is saved.

Workflow (after milestone 7; usage in `README.md`): the tree has two mounts, `repo/` (editable working copy,
`plugins/<plugin>/scripts`) and `device/` (ssh, connected on first use). Push and pull copy between them; the
plugins load only `user-1` to `user-8` from the user folder, so a script is pushed to a slot (asked once, kept in memory
for the session), a library (a script with a global table) under its own name. Before a copy the files are compared:
equal ones are skipped, a target that differs and was not written by this editor needs a yes. The device watches only
the selected script, so a changed library takes effect when its importer reloads. F1 renders `api.json` as an HTML
page (`api/html.py`) in the user's cache folder and opens it in the browser.
Messages: every message goes to the status line and to the Activity list (`ui/activity.py`); warnings and errors bring
the tab to the front. Errors are explained by `ui/explain.py` (summary, hint, details) and shown by `Dialogs.error`;
a missing source in a copy is `State.MISSING`, skipped and reported. `NotFound.path` and `MountFS` keep the mount in
the path. Indentation: `editor/indent.py` holds the rules as functions (tested without Qt), `CodeEdit` applies them to
Enter, Tab, Shift+Tab and to a closing word typed alone on a line.
Examples and guide (2026-10-23): each scripted plugin ships `scripts/examples/NN-slug.lua` (7 each) and `scripts/AGENTS.md`
in its bundle. `api.json` gets the optional keys `examples`, `scripts` (the complete factory scripts) and `guide`;
`api/html.py` shows them (Examples, Complete scripts, "Used in" under every name, Copy buttons, colours from
`editor/palette.py`), `api/markdown.py` builds the brief (guide, API, examples; Help > Copy API brief for an LLM). The
generator ignores `user-*.lua` drafts. The corpus tests read `plugins/<plugin>/scripts`, not a sandbox folder.
The slot table is not saved: after a restart the first push asks again, and a pull of a slot asks for the repo name.

## Open items

- Completion and the hint call `mask()` on the whole text on each key press; fine for scripts of a few KB, to be
  measured on a script near the 256 KiB limit.
- Script size limit (`c_maxScriptBytes`) is not in the generated docs; see `docs/dsp-alignment.md`.
