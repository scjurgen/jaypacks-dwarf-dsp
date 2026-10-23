# Documentation style for dsp/ and plugin headers

How the comments in headers are written, so new ones stay consistent.
Adapted from AbacDsp `docs/DOCSTYLE.md` (see `docs/references.md`); the rules
are the same, the Doxygen-specific parts are left out because this project
has no API doc build.

## The one rule that matters

A class comment documents **the class's own contract and the reasoning behind
it**. Not who calls it, not what it is used for, not where its data comes
from. Callers change; the class does not.

Use cases belong in separate documents (`docs/plugins/`, `documentation/`),
with example code where that helps. They are deliberately absent from the
headers.

## What earns a place in a brief

Ask whether a competent reader would already know it from the declaration. If
yes, leave it out.

Worth writing:

- **What it actually is, named precisely.** "One-pole (first-order IIR)
  lowpass", not "a filter".
- **The numeric contract.** Order, slope, state size, latency, allocation
  behaviour, thread and real-time constraints, valid ranges, measured
  limits (e.g. where a cutoff mapping drifts, where resonance self-oscillates).
- **Why a specific choice was made,** where the alternative is not obviously
  worse: why the exact Ornstein-Uhlenbeck transition instead of an Euler
  step, why our own Gaussian instead of `std::normal_distribution`.
- **Invariants and traps.** An ordering dependency between two methods, a
  float dead band, a state where a method stops meaning what its name says.
- **A theory reference,** where a canonical one exists.

Numbers stated in a brief are measured or derived, not guessed; the unit tests
or the charts in `documentation/` back them.

Not worth writing:

- Restating the class name as a sentence.
- Describing what the code does step by step.
- Naming a caller, a plugin, or a feature the class happens to serve.
- Project history: what this replaced, what it was ported from, what is
  planned. Provenance belongs in `docs/references.md`, plans in
  `docs/roadmap.md`, history in git.

## Shape

```cpp
/**
 * @brief One line: what this is, named precisely.
 *
 * Why it exists, what it trades off, what is non-obvious about it.
 * Numeric contract and invariants.
 * @see https://ccrma.stanford.edu/~jos/filters/One_Pole.html
 */
```

Short is good but is not the goal; completeness is. A thin wrapper gets one
line and points to the theory of what it wraps.

Enumerators take `///<` trailing briefs where the distinction between values
is the useful information. Free functions and methods take `///` briefs,
generally two or three lines.

## References

Check that a URL resolves before citing it. Prefer sources that outlive
redesigns: Julius Smith's CCRMA books, the DAFx paper archive, Wikipedia for
standard algorithms, the original author's page for an algorithm (e.g. the
xoshiro page). Add each new reference to `docs/references.md`, section 3.

## Mechanics

- ASCII for formulas: `pi`, `sqrt(2)`, `+/-`, `-3 dB`, `e^(-x)`. No em or en dashes. Proper names
  and quoted foreign-language text keep their diacritics (Jürgen, Schwietering, "réverbération").
- Comments describe the code as it is; when the code changes, the comment
  changes in the same edit.
