# jaypacks-dwarf-dsp: rules for development sessions

LV2 audio plugins for the MOD Dwarf, developed and tested on a Mac. Borrows
algorithms from AbacDsp (https://github.com/scjurgen/AbacDsp).

## Read first

1. `docs/roadmap.md` - step list and status; what is done, what is next.
2. `docs/working-with-claude-code.md` - how sessions work here: the author's
   working rules, editing and verification routine, measuring and device
   techniques, known traps, open items.
3. `docs/references.md` - where things are in AbacDsp (pinned commit, where
   to look) and the provenance of every file ported from it.
4. `local-info/README.md`, if it exists - facts about the author's machine
   (paths, backups, private repositories). Gitignored.

Before analysing code in AbacDsp, look the topic up in `docs/references.md`
first and read only the named section or file.

## Keep these current

The docs describe the current state and the outlook, not the history; git
holds the history. With every change, in the same step: update
`docs/roadmap.md` (status, next steps), add a provenance row to
`docs/references.md` for every file copied or ported from AbacDsp, and
correct any doc that the change made wrong. Stale statements are fixed, not
left in place.

**Strong reminder: documentation is part of every change, never a follow-up.**
The plugin snapshots in `docs/plugins/images/` and the images in the README
count as documentation. Whenever a blueprint changes (a plugin JSON, the
generator, its templates, `generator/graphics.py`, the modgui files, the ports
or controls of a plugin), go through the consequences before reporting the work
as done: regenerate the snapshots (`tools/jp.py snapshot [name]`, and check
that `build-mac` warns about none), then search `README.md`, `docs/` and the
plugin pages for every statement, table or picture the change touches. The
author should never have to point out stale docs or pictures.

## References

Every algorithm, constant, table or measured target cites its source, in the
header brief and in a row of `docs/references.md` section 2 (author, title,
year, what was taken from it). A number nobody can trace is a bug.

- Prefer durable sources: standards, papers and books (DOI or publisher
  page), then Wikipedia and university or lab pages (J. O. Smith at CCRMA),
  then manufacturer pages, personal sites and blogs, forums last or never.
  A wiki or a paper beats a forum thread.
- Links must survive: for every source that is not a standard, a paper or
  Wikipedia, add an archive.org snapshot URL next to the live link. A datasheet
  or chart that a target was traced from is kept as the excerpt used in the
  documentation folder when the licence allows, otherwise the row says why not.
- Verify the link when it is added (HTTP 200, and that it shows what the row
  claims) and write the date in the row.
- Concise: one row per source, no prose; the reasoning lives in the header
  brief or in `documentation/<folder>/README.md`.

## Definition of done

A change is done, and may be reported as done, when every item that applies to
it holds. Say which items you ran and which do not apply; do not skip silently.
Details of the commands: `docs/working-with-claude-code.md` (verification
routine, plugin checklist), `docs/testing.md`.

Always:
- Code follows the code rules below; `clang-format` clean on changed files;
  `dev-scripts/dev-warnings.sh` shows 0 warnings in own code.
- Documentation is current (see above): roadmap, references, README, plugin
  pages, snapshots. Stale statements are fixed in the same step.
- References (see above): every source of a new algorithm, constant or
  target is cited in the header brief and in `docs/references.md`, with an
  archive link where the source is not durable, and the links were checked.
- ASCII check on changed files; no local machine facts in committed files.

When the change touches code:
- Unit tests for new or changed behaviour (`dev-scripts/dev-test.sh` all green;
  sanitizer build `mac-asan` green for DSP or real-time code). A bug fix gets
  a test that failed before.
- Plugin behaviour in `tests/host/`; generic host tests and memory probe pass
  for every plugin (no allocation in `run()`, nothing left after `cleanup()`).

When the change adds or changes an algorithm (a DSP block, a filter, a
generator, a reverb, a plugin's signal path):
- A folder in `documentation/` (as the existing ones: explore program, plots,
  checked-in PNGs, README explaining them) checks the code against theory and
  shows what it does: response, distribution, decay, behaviour at the extremes,
  whatever the algorithm promises. Numbers quoted in docs come from the
  explore program's summary. Row added to the folder table in
  `documentation/README.md` and to `DSP_MODULES_USED.md`; charts regenerated
  with `dev-scripts/dev-docs-plots.sh <folder>` when the algorithm changes.
  Prose about design and usage stays in `docs/`.

When the change ships to the device (any C++ in a plugin, the adapter, the
generator or its templates, modgui):
- `tools/jp.py build-dwarf <name>` passes (GCC 9.4, export and symbol checks,
  modgui check).
- Device integration test for the plugin passes (`tools/jp.py test-device`),
  and a change to what the web UI shows is looked at in the web UI.

When the change can affect CPU or memory (DSP, process loop, adapter, scripting):
- `tools/jp.py perf <name>` rerun, `plugins/<name>/performance.json` and the
  performance tables updated (`tools/performance.py update`); a regression
  against the previous numbers is explained or fixed.

When the change touches a blueprint, the generator, templates or graphics:
- Generator tests and graphics tests green (`.venv`), snapshots regenerated,
  `build-mac` warns about none.

## Licence

Code is MIT (`LICENSE`). The pedal artwork is not: the files listed in
`ARTWORK-LICENSE.txt` (generator graphics and style, modgui icon templates, generator
PNGs, `documentation/Graphics/`, `docs/plugins/images/`) are reserved, and the brand
names are in `BRANDING.md`. A new artwork file is added to the list in
`ARTWORK-LICENSE.txt` in the same step; do not copy artwork into other projects.

## No local machine information in the repository

Committed files (code, comments, docs, commit messages) never contain facts
that are true only on the author's machine: home paths (`~/projects/...`),
backup locations, ssh key files, local docker images, private repositories.
Such facts go to `local-info/README.md` (gitignored; create it if missing).
Generic facts stay: the Dwarf's default address `192.168.51.1`, the standard
macOS LV2 folder `~/Library/Audio/Plug-Ins/LV2`. `.claude/` is gitignored as
a whole: whatever a session needs from it belongs in this file or a skill.

## Code rules

- C++17 only (device toolchain GCC 9.4). Ports from AbacDsp (C++20) replace
  `std::numbers`, `std::span`, ranges and concepts; port the unit tests with
  the algorithm.
- Formatting: `.clang-format` (Allman, 4 spaces, 120 columns). Naming: `m_`
  members, `c_` constants, camelCase setters, `PascalCase` classes, namespace
  `jp` for `dsp/`.
- Header briefs follow AbacDsp `docs/DOCSTYLE.md` (contract and reasoning,
  not callers).
- Real-time code (`process`, `run`): no allocation, no locks, no file or
  console I/O, no exceptions. Smoothing per sample inside the pedal.
- Randomness only through `dsp/Numbers/Random.h` (seeded, identical on Mac and Dwarf);
  never `std::random_device` or the `std::` distributions in DSP code.
- ASCII only in code, comments, config, JSON, docs and commit messages. No
  em or en dashes. Unicode math symbols only inside formulas. One more
  exception: Latin letters with diacritics (ü, é, ç, ñ) in proper names
  (author Jürgen, authors in citations), titles and quoted foreign-language
  text, in docs, comments, docstrings and commit messages. Never in
  identifiers, program output strings, config, JSON, CLI commands or logs.
  The ASCII check in the verification routine flags these too; look at each
  hit and accept names and quotes.
- Generated files (ports headers, TTL, modgui, images) go to the build
  directory and are never committed. One exception: the plugin snapshots in
  `docs/plugins/images/`, refreshed by `tools/jp.py snapshot` (`build-mac`
  warns when one is out of date).

## Commands

```
dev-scripts/dev-test.sh [regex]     build and run the tests (build-tests/)
dev-scripts/dev-warnings.sh         clean rebuild, list warnings in own code
dev-scripts/dev-explore.sh [file]   throwaway C++17 program in explore/
dev-scripts/dev-docs-plots.sh [dir] rebuild and render the charts in documentation/
dev-scripts/dev-venv.sh             create/update .venv (charts, modgui images)
dev-scripts/dev-lua-editor.sh [--local]  lua-editor: repo/ scripts + device/ over ssh (--local: repo only)
tools/jp.py build-mac [name] [--install]  release LV2 bundles, optionally into ~/Library/Audio/Plug-Ins/LV2
tools/jp.py build-dwarf [name]      cross-build in docker, checks, device bundles in build-dwarf/bundles/
tools/jp.py deploy [name] [--unload]  install on the Dwarf
tools/jp.py test-device <name> [--build] [--no-deploy] [--restore]  device integration test
tools/jp.py perf [name] [--cycles N] [--restore]  CPU/memory on the Dwarf -> plugins/<name>/performance.json, tables
tools/jp.py snapshot [name]         modgui screenshot -> docs/plugins/images/<name>.png
tools/performance.py update         rewrite the performance tables from the JSON files
tools/modui.py status               read-only device status
tools/device_tests/*.py [--restore] checks on the Dwarf (docs/test-plugins.md)
cmake --preset mac-asan             sanitizer build (build-mac-asan/)
```

Scratch output goes to `build-tests/scratch/` or `scratch/` (gitignored),
never to `/tmp`. Shell commands use absolute paths instead of `cd`.

## Device

MOD Dwarf at `DWARF_HOST` (default 192.168.51.1), ssh as root with key. The
author lets sessions use the device freely (2026-10-15): deploy jaypacks
plugins and run device tests without asking, and leave the test graph loaded
so the author can watch it in the web UI (device scripts keep it; `--restore`
reloads the previous board). Deleting other bundles or pedalboards and system
changes still need the author's go-ahead. Details in `docs/references.md`
section 1.3.

## Commits

Commit only when the author says so. The author has delegated the commit
message (2026-10-15): choose it yourself, no approval round. No
`Co-Authored-By` trailer. Push only when asked.
