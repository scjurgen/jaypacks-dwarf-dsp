# Documentation: charts and analysis

Charts that check the DSP code against theory and show what it does, one folder per
algorithm or plugin. Same layout as AbacDsp `documentation/`: a small C++17
explore program writes plain text series, `Plot/PyConPlot.py` renders them, the PNGs are
checked in next to a README that explains them. Prose about design and usage lives in
`docs/`, not here.

Kinds (as in AbacDsp):

- **documentation**: verification charts against theory (response, distribution, decay).
- **exploration**: an offline harness for tuning or understanding behaviour.
- **generation**: a tool that produces source code from a design process
  (`JaysTwin/fit_cabinet.py` prints the cabinet table of `dsp/Filters/TwinCabinet.h`).

## Folders

| Folder | Kind | Covers |
|---|---|---|
| `OrnsteinUhlenbeck/` | documentation | `dsp/Generators/OrnsteinUhlenbeck.h`: timelines, autocorrelation and stationary distribution at four sigma speeds; exact transition against Euler-Maruyama at slow step rates; the walk of shipahoy's `slow-drift.lua` (brown) |
| `Filters/PoleMixing/` | documentation, exploration | `dsp/Filters/PoleMixingFilter.h`, `dsp/Filters/OnePoleLowpass.h`: the 15 shipahoy filter types (theory and measured), phaser phase, the menu mapping fix, the stage across sample rates, oscillation threshold and peak tuning (with AbacDsp's measured correction), resonance up to self-oscillation, jumps and glides, the stage clamp |
| `Analysis/` | documentation | `dsp/Analysis/`: zero-crossing frequency error, Hann Goertzel band around a tone, ToneFit against one Goertzel block, TransferBand block length and coherence (the meter's 16384-sample bands) |
| `ShipAhoy/` | documentation | `plugins/shipahoy/ShipAhoyEngine.h` and the reference hopper `tests/unit/ShipAhoyClockedEngine.h`: the classic sound at work, the hop algorithms of the factory scripts, glide, mix law, the additive drift; background on the name |
| `YinPitch/` | documentation | `dsp/Analysis/YinPitchDetector.h` and `PitchNoteTracker.h`: the CMNDF of three tones, error over the range for the decimation options, a step of pitch, vibrato, broadband noise, the tracker's notes over a phrase; the cost on the Dwarf |
| `Eigenghost/` | documentation | `plugins/eigenghost/EigenghostPedal.h`, `dsp/Reverbs/FdnTank.h`: impulse response and echo density per diffusion, energy decay per decay setting, decay time per octave band per damping setting, shimmer partials, size change by fade against a hard change, line lengths per bulge, the modulation (pitch deviation, late tail per depth) |
| `JaysTwin/` | documentation, generation | `dsp/Filters/FenderToneStack.h`, `VolumeBright.h`, `TwinCabinet.h`, `dsp/NonLinear/AtanDrive.h`: tone stack per knob, bright lift per volume, cabinet against the Jensen C12K datasheet curve, drive curves, the linear chain; `trace_chart.py` (datasheet chart to CSV) and `fit_cabinet.py` (the cabinet's biquad table) |
| `Wavetables/` | documentation | `dsp/Wavetables/`: the shapes, harmonics per mipmap level, non-harmonic power with the chosen level against one table, pitch-bend headroom of a level, pulse-width modulation spectra |
| `Graphics/` | exploration | the modgui look: knob filmstrip, footswitch and pedal face rendered with the generator's code plus design variants, contrast measurement, interactive pages on mod-ui's wood background (`GraphicsDesign.py`, output in `generated/`, not part of `dev-docs-plots.sh`) |
| `Plot/` | shared tooling | `PyConPlot.py`, the matplotlib wrapper, copied unchanged from AbacDsp |

`DSP_MODULES_USED.md` lists which header each explore program uses.

For algorithms that are not ported here, AbacDsp's own `documentation/` tree is the
reference (https://github.com/scjurgen/AbacDsp, see `docs/references.md` section 1).

## Regenerating

```
dev-scripts/dev-docs-plots.sh                       # every folder
dev-scripts/dev-docs-plots.sh Filters/PoleMixing    # one folder
```

The script configures `build-docs/` (Release, `JP_BUILD_DOCS=ON`, no tests), builds the
`docs_explore` target, creates or updates the repository venv `.venv` from the root
`requirements.txt` (`dev-scripts/dev-venv.sh`, needs network once), then runs each folder's
`plots.sh`.
The explore program writes its text data and a `summary.txt` with the key numbers into the
folder's `generated/` (gitignored); `plots.sh` renders the PNGs and prints the summary.
Numbers quoted in the READMEs come from these summaries.

Conventions that differ from AbacDsp:

- One shared venv and one `requirements.txt` for all folders, shared with the modgui
  images (AbacDsp has one per folder); versions pinned as in AbacDsp (numpy 2.5.2,
  matplotlib 3.10.8).
- One script, `dev-scripts/dev-docs-plots.sh`, builds and renders; each folder has only a
  `plots.sh` with its render commands (AbacDsp has a self-contained `generate.sh` per
  folder).
- Smaller figures (about 900 pixels per column, 150 dpi as fixed in `PyConPlot.py`), so
  the PNGs stay between about 50 and 450 KB.

## Adding a folder

1. Write `<Folder>/<Name>Explore.cpp` (C++17, includes from the repo root such as
   `dsp/...` and `plugins/...`; `tests/unit/TestHelpers.h` has a sine probe). Take the
   output directory as the first argument and write a `summary.txt` with the numbers the
   README quotes.
2. Add it to `documentation/CMakeLists.txt` with `jp_docs_explore(<Name>Explore ...)`.
3. Write `<Folder>/plots.sh` (copy one, it receives the build dir and the venv python).
4. Write `<Folder>/README.md`: what is shown, what to read from each chart, the numbers.
5. Add rows here and in `DSP_MODULES_USED.md`.
