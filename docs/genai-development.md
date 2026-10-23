# GenAI-assisted development

How this project is developed with an LLM coding agent (Claude Code), written for people:
contributors who want to work the same way, and anyone curious how far the approach carries
for real-time audio code. The operational guide that a session itself follows is
`docs/working-with-claude-code.md`; the binding rules are in `CLAUDE.md`.

## The idea

The author decides what a plugin should do and how it should sound; the agent does most of
the typing: porting DSP from AbacDsp, writing unit and host tests, the generator, the build
and deploy tools, device tests, charts and documentation. The approach only works if the
agent's output can be trusted without reading every line, so the project is built around
evidence: every claim about the sound or the behaviour is measured, on the Mac and on the
MOD Dwarf, and the measurements are part of the repository.

## What the repository gives the agent

An agent starts every session without memory of the previous one. The repository is the
memory, in layers that are cheap to read:

| Layer | Content | Read |
|---|---|---|
| `CLAUDE.md` | Binding rules: language standard, style, real-time rules, ASCII, commits, device use, no local information | Always, loaded automatically |
| `docs/roadmap.md` | Status, what is next, open findings | At the start |
| `docs/working-with-claude-code.md` | How to work here: editing technique, verification routine, measuring and device techniques, known traps | At the start |
| `docs/*.md`, `docs/plugins/*.md` | The current facts: architecture, building, testing, device, caveats, one page per plugin | When the task touches them |
| `docs/references.md` | Where to look in AbacDsp, provenance of ported files | Only the section a question needs |
| `local-info/README.md` (gitignored) | Facts true only on the author's machine | If present |

Three rules keep these layers useful:

- **Docs state the current facts.** A change updates every doc it makes wrong, in the same
  step. There is no decision log and no changelog in the docs; git holds the history. A
  stale statement misleads the next session more than a missing one.
- **Numbers come from measurements.** A number in a doc comes from a test's printed output, a
  chart program's `summary.txt` or a device run, never from the agent's estimate.
- **No local information in committed files.** Paths on the author's Mac, backups, ssh key
  files and private repositories go to the gitignored `local-info/`. What is committed must
  be useful to anyone who clones the repository.

## The working loop

```
author: request (a step of the roadmap, a bug, an idea)
  -> agent: plan for anything non-trivial (goal, files, risks, verification), then stop
  -> author: approve, change or answer open questions
  -> agent: implement, run the verification gates, update the docs
  -> agent: report (what was built, measured numbers, deviations, what is not committed)
  -> author: listen, review, "commit"
```

- **Plans** are written to `.claude/<slug>.md` (gitignored) and presented before any file is
  touched. A change that turns out necessary beyond the plan is named and agreed first.
- **Questions** go to the author only for real choices (behaviour, protocol, what to leave
  out), with a recommendation; everything else follows from the rules and the code.
- **Instructions mean what they say.** "commit" means commit and stop; a question is
  answered, not acted on.

## Verification gates

Trust comes from gates the agent must pass and report, from fast to slow:

| Gate | Command | Catches |
|---|---|---|
| Unit tests | `dev-scripts/dev-test.sh` | DSP against theory, pedal behaviour |
| Host tests | same, through the built `.so` | the LV2 adapter, every control changes the output, block-size independence, no allocation in `run()` |
| Sanitizers | `cmake --preset mac-asan` | memory and undefined behaviour |
| Warnings | `dev-scripts/dev-warnings.sh` | target 0 in own code |
| Dwarf build | `tools/jp.py build-dwarf` | GCC 9.4 rejects code Apple clang accepts; symbol and GLIBC versions |
| Device test | `tools/jp.py test-device <name>` | the plugin measured in the device's graph: transfer functions, zipper, CPU, xruns |
| Listening | Reaper on the Mac (`docs/reaper.md`), the Dwarf | what no test captures |

The tests measure the audio that comes out (transfer functions, tone fits, steps between
samples) and compare it with theory computed by the same C++ code, so a plausible-looking but
wrong implementation fails. See `docs/testing.md`.

## Autonomy and safety

- The dev-scripts (`dev-scripts/README.md`) are short fixed commands, so the agent can be
  allowed to run them without a permission prompt for every `cmake` chain.
- The agent may deploy jaypacks plugins to the Dwarf and run device tests on its own, and
  leaves the test graph loaded so the author can watch it in the web UI. Deleting other
  bundles or pedalboards and system changes need the author's go-ahead.
- Commits only on the author's word; pushes only when asked.

## What to watch for

Traps met while building this project, and the rule that answers each:

- **Confident but wrong expectations.** A failing test is first suspected to be a wrong
  expectation and checked by reasoning about the exact behaviour; the report says which side
  was wrong.
- **Facts that were never checked.** Unverified facts are marked "to verify" and settled with a
  small probe (the `probe` plugin answered the device readback question before the real
  plugins were built). Historical claims need a cited source or are left out.
- **Platform differences.** Code that compiles on the Mac can fail with the device compiler;
  new code that ships is cross-built early.
- **Stale docs and scope creep** in long sessions. Rules are re-read before a plan is
  executed, and the docs are updated in the same step as the code.
- **Leaking private context.** Local paths and private repositories slip easily into docs and
  comments; they belong in `local-info/`.

## Starting a session

Useful openings, each answered from the repository alone:

- "Continue with the next roadmap step; plan first."
- "Port `<algorithm>` from AbacDsp with its tests; plan first."
- "Measure the performance of all plugins on the Dwarf and update the tables."
- "Review the docs against the code and fix what is stale."
