# siggen: test signal source

`plugins/siggen/` (`jp::SigGenPedal`), bundle `jaypacks-siggen.lv2`, URI
`https://github.com/scjurgen/jaypacks-dwarf-dsp/plugins/siggen`. No audio input, one audio
output. Part of the test plugins (`docs/test-plugins.md`).

## Inputs

| Port | Range | Default | Meaning |
|---|---|---|---|
| `waveform` | menu 0..7 | 6 (log sweep) | silence, sine, square, saw, white noise, impulses, log sweep, step |
| `frequency` | 1 to 20000 Hz | 1000 | Tone, square, saw and impulse frequency; start of the sweep; the step comes after one period of it |
| `level` | -120 to 0 dBFS | -20 | Peak level of every waveform |
| `sweepEnd` | 1 to 20000 Hz | 20000 | End of the sweep (below `frequency` sweeps down) |
| `sweepTime` | 0.1 to 60 s | 5 | Duration of one sweep; it then starts again |
| `on` | toggle | 1 | Off writes silence; the generator keeps running |
| `restart` | 0 to 16777215 | 0 | Any new value restarts the waveform |

The default waveform is the sweep because it is the one waveform every frequency and sweep
port acts on (the generic host test requires each control to change the output); scripts
always set the waveform they need.

## Outputs

| Port | Meaning |
|---|---|
| `restartId` | The last `restart` value applied |
| `rms` | Output RMS of the last complete second in dBFS (-240 until the first second ends and for silence) |

## Waveforms

Every waveform is computed per sample from a phase accumulator in double, a sample count and a
seeded `jp::Random`, so the output does not depend on the host's block size, and a restart
repeats it exactly: phase 0, the sweep at its start, the noise from the same seed.

| Waveform | Sample at phase p |
|---|---|
| sine | `level sin(2 pi p)`, starts at 0 and rises |
| square | `+level` for p < 0.5, `-level` after (not band limited) |
| saw | `2 level p - level` (not band limited) |
| white noise | `level (2 u - 1)`, u uniform, 2^24 steps; RMS `level / sqrt(3)` |
| impulses | `level` on the first sample of each period, else 0 |
| log sweep | sine with frequency `f0 (f1 / f0)^(k / N)` at sample k of N = `sweepTime fs` |
| step | 0 for one period of `frequency`, then `level` |

A frequency change keeps the phase. Level and on/off act at the next sample without
smoothing: a test source must not add ramps of its own.

## Techniques

| Technique | Where |
|---|---|
| Phase accumulator in double, one sample count, waveforms computed per sample (no wavetables, not band limited) | `plugins/siggen/SigGenPedal.h` |
| Exponential (log) sweep: frequency `f0 (f1 / f0)^(k / N)` from the sample count k (one `exp` per sample while sweeping), phase accumulated per sample | `plugins/siggen/SigGenPedal.h` |
| Uniform white noise from a seeded xoshiro256** (24-bit floats), repeatable after a restart | `dsp/Numbers/Random.h` |
| Restart by counter: any new value resets phase, sweep and seed, so a measurement can be repeated exactly | `plugins/siggen/SigGenPedal.h` |
| Output RMS over whole seconds (sum of squares in double), reported in dBFS with a -240 floor | `plugins/siggen/SigGenPedal.h` |

Common to every plugin (the LV2 adapter, `lv2/Lv2Adapter.h`): any host block size,
flush-to-zero for denormal floats while `run()` executes (`lv2/FlushDenormals.h`), NaN
control values ignored (bit-pattern test, safe under `-ffast-math`), no allocation, locks or
I/O in `run()`.

## Performance on the Dwarf

<!-- perf:begin -->
| Settings | CPU (points of mod-ui's DSP load) | Range over the cycles |
|---|---|---|
| defaults | 0.5 | 0.3 to 0.8 |

Memory: the plugin allocates 0.3 kB when it is created (at 48 kHz, peak 0.3 kB, nothing while it runs; counted by its memory probe on 2026-10-16); the binary is 10 kB. Xruns while processing: 0.

Measured 2026-10-15 with tools/jp.py perf: plugin 0.1.0, commit e816f12+changes, MOD OS v1.13.5.3315, jackd 48000 Hz, 128 frames; input none (no audio input); median of 3 cycles of 6 s with and without the plugin.
<!-- perf:end -->

## Tests

`tests/unit/Plugins/SigGenPedal_test.cpp` (level and frequency of the sine, square, saw and impulse
timing, step, noise statistics and repetition, restart, sweep frequency over time and
repetition, on/off, RMS, block size independence), the generic host tests, and
`tests/host/TestPlugins_test.cpp`.
