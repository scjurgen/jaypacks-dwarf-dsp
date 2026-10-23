# Working on this repository with Claude Code

A guide for an LLM session (Claude Code) that continues the work: how the project was built
step by step, which techniques proved reliable, and the traps met on the way. `CLAUDE.md`
holds the binding rules; this page explains how to work within them. The overview for people
is `docs/genai-development.md`.

## Why the project works this way

The README's goals set the direction: organic, playable plugins for creative use (not
replicas of gear), developed with heavy LLM assistance, and trusted because they are
measured. A session serves these by keeping the plugins musical (smooth, expressive,
surprising) and the evidence solid (tests, measured numbers, docs that state the facts).

## Start of a session

1. Read `CLAUDE.md`, then `docs/roadmap.md` (status, open findings), `local-info/README.md`
   if it exists (the author's machine), and `docs/references.md` only for the section a
   question needs.
2. Look at `git log --oneline -10` and `git status`: one roadmap step is one commit, and
   uncommitted work may be waiting for the author's "commit".
3. For device work: `tools/modui.py status` (read-only) shows whether the Dwarf is reachable,
   what is loaded and which jaypacks bundles are installed.

## How the author works with sessions

- **Instructions mean exactly what they say.** "commit" means commit and stop; "commit, then
  start step N" means exactly those two things; a question is answered, not acted on.
- **Commits:** only when the author says so. The message is chosen by the session (delegated,
  no approval round), no `Co-Authored-By`, no push unless asked.
- **Plans:** a step that is not fully specified in the roadmap (step 9 was such a step) gets
  a mini-plan first (goal, files, risks, verification), written to `.claude/<slug>.md`
  (gitignored), presented, and only implemented after approval; when done the file gets the prefix `done-`. Changes beyond an approved plan are
  named and agreed first.
- **Step numbers:** every new plan numbers its steps from 1 (steps 1 to 16 of the first
  roadmap end with Eigenghost and are not continued). In `docs/roadmap.md` the plan gets its
  own section with its own status table; commit messages and docs name the plan with the
  step ("Jays Twin step 1").
- **Questions to the author** only for real decisions (a choice of behaviour, a protocol, what
  to leave out). Offer a recommendation as the first option.
- **The device** may be used freely: deploy, run tests, leave the test board loaded.
  Deleting other bundles or pedalboards and system changes still need the author.
- **Reports** at the end of a step: what was built, the measured numbers, deviations from the
  plan with reasons, findings for later steps, what is not committed. Short, no filler.
- **Pronouns:** the author is referred to as "the author" or with they/them.

## Text and file rules (enforced)

- ASCII only in code, comments, JSON, docs and commit messages; no em or en dashes, no
  box-drawing characters. Diagrams in Markdown are Mermaid (a `mermaid` code block, mostly
  `flowchart LR`), not drawn with `-->`, `|` and `+--` (those break under search and
  replace); `#lt;` and `#gt;` stand for angle brackets in labels. Unicode only for math symbols
  inside formulas, and letters with diacritics (ü, é, ç, ñ) in proper names, titles and quoted
  foreign-language text in docs, comments and commit messages (never in identifiers, program
  output, config, JSON or logs). Check with `LC_ALL=C grep -n '[^ -~<TAB>]' <files>`; it flags
  these too, so look at each hit.
- No `/tmp` on the Mac: scratch files go to `build-tests/scratch/` or `build-dwarf/scratch/`
  (gitignored) or the session scratchpad.
- No `cd`: a hook blocks every shell command whose text contains `cd`, even inside a
  heredoc. Use absolute paths, `git -C <repo>`, `cmake -S/-B`, `ctest --test-dir`. A script
  that needs `cd` (e.g. a `plots.sh`) is written with the Write tool.
- C++: `.clang-format` (Allman, 4 spaces, 120 columns), `m_` members, `c_` constants,
  namespace `jp`, C++17, header briefs per `docs/DOCSTYLE.md`. A post-write hook runs the
  formatter on C++ files, so re-read a file before an exact-match edit of a region the
  formatter may have touched.

## Editing technique

- **Small exact edits:** the Edit tool (read the file first in the session).
- **Many edits across files** (docs after a step): a short Python script with a
  `sub(path, old, new)` helper that asserts `old` occurs exactly once, run with
  `python3 - <repo> <<'EOF' ... EOF`. A failed assertion names the edit, nothing is half
  applied per file, and the edits are reviewable in one place.
- **Local facts** (paths on the author's Mac, backups, private repositories) go to
  `local-info/README.md`, never into a committed file.
- **Generated content** (JSON with many ports, e.g. `meter.json`) is produced by a script and
  then reformatted to the house layout (one port per line).
- **The shell is zsh:** an unquoted `$VAR` holding a space-separated list is passed as ONE
  argument (no word splitting as in bash). Pass long argument lists from Python
  (`subprocess.run([...])`) or use arrays; a helper that silently gets one argument instead
  of 480 returns plausible-looking nonsense.
- **IDE diagnostics** for new files ("file not found", unknown `jp` types) come from the
  editor's index, not from the build; the build and the tests are the truth.

## Verification routine (every step)

```
dev-scripts/dev-test.sh                                   all tests (191 at step 10)
cmake --build build-mac-asan && ctest --test-dir build-mac-asan   ASan/UBSan
dev-scripts/dev-warnings.sh                               0 warnings in own code
clang-format --dry-run --Werror <changed .h/.cpp>
LC_ALL=C grep -n '[^ -~<TAB>]' <changed files>            ASCII
python3 -m py_compile tools/*.py tools/device_tests/*.py
tools/jp.py build-dwarf <name>                            GCC 9 and binary checks
```

The list of changed files comes from `git -C <repo> ls-files --others --modified
--exclude-standard`. Run the Dwarf build for any new C++ that ships: GCC 9 rejects code
Apple clang accepts.

## Measuring instead of asserting

The tests judge what comes out of the plugin, with the analysis blocks of `dsp/Analysis/`,
and against theory computed by the same C++ code:

- **Transfer functions** from noise with `TransferBand` (H1 estimate, Hann blocks of 16384
  samples) against `poleMixingResponse()`; never band energies alone (they scatter by dB).
- **Tone level and residual** with `ToneFit` (exact for any window length): the residual is the
  zipper and distortion metric.
- **Frequency** from zero crossings; **steps** from the largest sample-to-sample difference.
- **Clicks** as energy above a cutoff (`highBandEnergyDb()`, e.g. above 5 kHz on a 237 Hz
  sine), with a deliberate hard change as the control. In a ringing system (a reverb tank)
  the largest sample step is no click measure: after a change the output swings to a new
  steady state and may get louder on the way.
- **Zipper of a crossfade between two different signals** (dry and a reverb's wet): the bound
  is the sum of the two signals' largest steps, not the larger one; two sines of the same
  frequency and different phase can add up louder than either during the crossfade.
- **Pitch of a granular shifter** as the spectral centroid around the expected frequency
  (`spectralCentroid()`): the grains spread the carrier over the grain rate with random
  fine structure, so the highest bin scatters by tens of cents and zero crossings miss
  periods where two grains cancel.
- **Hidden state from the audio:** shipahoy's hop positions are read from the amplitude of a
  probe tone through the filter (a copy of a factory script with the lp 6 dB type and no resonance), inverted in
  closed form, and compared with the statistics the script's algorithm promises (uniform, an Ornstein-Uhlenbeck walk).
- **Exact where it must be exact** (mix -100, bypass: difference at the -240 dB floor),
  tolerances where noise is measured; every tolerance is backed by a measured value.
- **Tests print their numbers** (`[ tag      ] ...` lines), and the numbers go into the roadmap
  and the docs.
- **A failing test is first suspected to be a wrong expectation**, then checked by reasoning
  about the exact behaviour (examples: a sampled sine's largest step is A sin(w) when a sample
  falls on the zero crossing; a meter setting applies only to the window after the current
  one). Fix the side that is wrong and say which it was.
- **Charts** for analysis claims (`documentation/<Folder>/`); README numbers come from the
  program's `summary.txt`. Look at the rendered PNG before describing it.

## Device technique

- Everything goes through mod-ui (HTTP and websocket, `tools/modui.py`), never mod-host
  directly while mod-ui runs.
- Test graphs: `/reset`, `/effect/add`, `/effect/connect`; siggen as source, the plugin under
  test, the meter with the source also on its reference input; nothing to the audio outputs.
- Readback: control outputs listed in `modgui:monitoredOutputs`, echo every `data_ready`,
  `OutputReader` keeps the latest `output_set` values.
- Measure protocol: set ports, settle, send a new `measure` value, wait for `resultId`,
  poll 0.3 s more, read. Settle 1 s before exact checks (smoothers snap after about 0.34 s).
- New device tests: `tools/device_tests/test_<name>.py` on top of `devicetest.py` (`Check`,
  `Graph`, stats, journal); expected values from C++ helpers built with the tests (e.g.
  `shipahoy_expected`), not re-implemented in Python.
- MIDI to the device: a plugin with a MIDI input only receives it when the graph connects
  `midi_merger_out` to it, and a test sends through the ALSA sequencer
  (`modui.midi_send()`, `docs/device.md`, "MIDI"); an external JACK client loses about half of
  its events. The device's Python is 3.4 (no f-strings in code that runs there).
- Lua scripts: edit them in `plugins/<plugin>/scripts` (git is the undo), try a draft with the editor's Push (Ctrl+U) or
  `tools/jp.py push-script <plugin> user-N <file>`: the plugins load only `user-1` to `user-8` from the user folder, and
  the plugin hot-reloads the selected one within a second. The author listens on the Dwarf; accepting a script is a commit
  plus removing the draft slot (`lua-editor/README.md`, "Workflow").
- CPU and xruns from the `stats` messages: compare within one run; judge xruns while the graph
  processes, report those during graph changes. The journal is read after a cursor
  (the device clock is wrong).
- Verify the verifier: a check that finds nothing (0 journal lines, 0 xruns) is confirmed
  once to be able to find something (e.g. the cursor query on older entries).
- Performance: `tools/jp.py perf <name>` after any DSP change of a plugin; it rewrites
  `plugins/<name>/performance.json` and the tables (`tools/test_performance.py` fails when a
  table and its JSON disagree).
- **Timing DSP on the device:** a throwaway benchmark in `build-dwarf/scratch/` (each part
  of a plugin timed in us per 128 frames), cross-built in the container with the toolchain's
  `$CXX $CXXFLAGS -O3`, copied with `scp -O` to `/tmp` on the Dwarf, run over ssh and
  removed. It separates memory from arithmetic and showed what to optimise for eigenghost.
  Keep an optimisation bit-identical where possible and prove it by comparing a recorded
  output (`cmp` of two raw dumps) before and after.
- Unexpected device behaviour is isolated with a small experiment script in
  `build-dwarf/scratch/` (e.g. 40 add/remove cycles to see whether xruns come with plugin
  adds) before the test or the code is changed.

## Facts and sources

- Facts about the device and mod-ui come from `docs/device.md`; a fact that was not
  verified is marked "to verify" or "assumed", and verified later with a small probe (the
  `probe` plugin answered the readback question before the real plugins were built).
- Historical or external claims in docs need a cited source; claims that cannot be confirmed
  are left out or labelled (see the background of `docs/plugins/shipahoy.md`).
- When a statement in a doc turns out wrong, it is corrected in place; the docs state the
  current facts, git keeps the history.

## Adding the next plugin (checklist)

1. JSON in `plugins/<name>/<name>.json` (`docs/plugin-json.md`), pedal class
   `plugins/<name>/<Name>Pedal.h`, DSP blocks in `dsp/` with unit tests; a port from AbacDsp
   gets a provenance row in `docs/references.md`.
2. Add the name to `JP_PLUGINS` in `plugins/CMakeLists.txt`; `dev-scripts/dev-test.sh` runs
   the generic host tests on it automatically (every control must change the audio or an
   output; output independent of block size) and its memory probe (`host.memory.<name>`:
   no allocation in `run()`, nothing left after `cleanup()`).
3. Plugin behaviour through the binary in `tests/host/<Name>_test.cpp`; charts in
   `documentation/<Name>/` where the algorithm needs explaining.
4. `tools/jp.py build-mac <name> --install` for listening in Reaper (author),
   `tools/jp.py build-dwarf <name>`, a device test `tools/device_tests/test_<name>.py`, run by
   `tools/jp.py test-device <name>`.
5. Docs: roadmap, `docs/plugins/<name>.md`, README.

## Open items

Listed in `docs/roadmap.md` ("Next"). Still to do by hand on the device: turn knob 1 and press
footswitch B (assigned by the device test).
