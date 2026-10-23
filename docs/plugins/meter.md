# meter: in-place measurements

`plugins/meter/` (`jp::MeterPedal`), bundle `jaypacks-meter.lv2`, URI
`https://github.com/scjurgen/jaypacks-dwarf-dsp/plugins/meter`. Two audio inputs (signal,
reference), one output (the signal, passed through unchanged). Part of the test plugins
(`docs/test-plugins.md`); the measurements are the blocks of `dsp/Analysis/`.

## Inputs

| Port | Range | Default | Meaning |
|---|---|---|---|
| `measure` | 0 to 16777215 | 0 | A new value starts a new window at the next block |
| `window` | 0.1 to 60 s | 0.5 | Window length |
| `toneHz` | 10 to 20000 Hz | 1000 | Frequency of the tone fit |
| `band1Hz` .. `band8Hz` | 10 to 20000 Hz | 63, 125, 250, 500, 1000, 2000, 4000, 8000 | Band frequencies (measured at most at 0.49 fs) |

Window length, tone and band frequencies set without a new `measure` value apply from the next
window on.

## Outputs

All results are replaced together when a window ends and held until the next one ends.
Levels are dBFS with the floor -240 (also shown before the first result and for silence).

| Port | Meaning | Block |
|---|---|---|
| `resultId` | `measure` value the window started with | |
| `peak`, `rms`, `crest` (dB), `dc` (linear) | Of the signal | `SignalStats` |
| `nonFinite`, `denormals` | Sample counts of the signal (bit pattern, also under `-ffast-math`) | `SignalStats` |
| `maxStep` | Largest step between neighbouring samples: the zipper metric | `SignalStats` |
| `frequency` | From upward zero crossings (hysteresis 0.001), Hz; 0 with fewer than two | `ZeroCrossingFrequency` |
| `toneLevel`, `toneResidual` | Amplitude of the best-fitting sine at `toneHz`, and the RMS of what is left (noise, distortion, steps, modulation) | `ToneFit` |
| `refRms` | RMS of the reference | `SignalStats` |
| `diffPeak` | Peak of signal minus reference: -240 when they are equal | `SignalStats` |
| `band<k>Level` | Signal level at the band frequency (a sine on a bin centre reads its amplitude) | `TransferBand` |
| `band<k>Gain` | Gain from reference to signal at the band frequency, H1 estimate (-240 to +240 dB) | `TransferBand` |
| `band<k>Coherence` | 0 to 1: 1 for a linear, time-invariant path without added noise | `TransferBand` |

Bands use Hann blocks of 16384 samples (2.9 Hz bins at 48 kHz): with 4096 samples the
coherence on a steep slope drops to 0.98 at low frequencies (step 8 finding;
`documentation/Analysis/an_transfer_blocks.png`); with 16384 samples it still drops to 0.88
to 0.99 at 63 Hz on the low cut of the Jays Twin cabinet (about 1 dB change across the bin),
where the gain scatters by +-0.4 dB between windows (Jays Twin step 3). A block cut by the end of the window is
dropped, so a window shorter than 16384 samples has no band result (level and gain at the
floor, coherence 0); a few blocks per window (window 2 s and up) average the noise.
Non-finite samples are counted and enter the other measurements as 0.

## Techniques

| Technique | Where | Charts |
|---|---|---|
| Measurement windows started by a counter (`measure` / `resultId`): results held for a whole window, independent of the host's update rate | `plugins/meter/MeterPedal.h` | |
| Peak, RMS, crest, DC, largest sample step (zipper metric), non-finite and denormal counts from the bit pattern | `dsp/Analysis/SignalStats.h` | |
| Frequency from upward zero crossings with hysteresis and linear interpolation of the crossing time | `dsp/Analysis/ZeroCrossingFrequency.h` | `documentation/Analysis/` |
| Least-squares fit of DC plus one sine (3x3 normal equations, exact for any window), residual as the distortion and zipper metric | `dsp/Analysis/ToneFit.h` | `documentation/Analysis/` |
| Hann-windowed Goertzel per band, power averaged over blocks (Welch for one frequency) | `dsp/Analysis/GoertzelBand.h` (`GoertzelBand`) | `documentation/Analysis/` |
| Transfer function per band as the H1 estimate Sxy / Sxx with coherence, blocks of 16384 samples | `dsp/Analysis/GoertzelBand.h` (`TransferBand`) | `documentation/Analysis/` (block length) |
| Results in dB with a -240 floor, counters below 2^24, outputs clamped to their port ranges (mod-ui reports six decimals) | `plugins/meter/MeterPedal.h` | |

Common to every plugin (the LV2 adapter, `lv2/Lv2Adapter.h`): any host block size,
flush-to-zero for denormal floats while `run()` executes (`lv2/FlushDenormals.h`), NaN
control values ignored (bit-pattern test, safe under `-ffast-math`), no allocation, locks or
I/O in `run()`.

## Accuracy

From `documentation/Analysis/` (clean float signals, 48 kHz): frequency within 2.2 ppm over
1 s (29 ppm over 0.1 s), 14 ppm over 1 s with noise 60 dB below the tone; tone level exact to float
precision for any window length; band reading 1.42 dB low for a tone half a bin from the
band frequency, side lobes of the window from -31.5 dB.

## Performance on the Dwarf

<!-- perf:begin -->
| Settings | CPU (points of mod-ui's DSP load) | Range over the cycles |
|---|---|---|
| defaults | 1.2 | 1.0 to 1.9 |

Memory: the plugin allocates 3.5 kB when it is created (at 48 kHz, peak 3.5 kB, nothing while it runs; counted by its memory probe on 2026-10-16); the binary is 22 kB. Xruns while processing: 0.

Measured 2026-10-15 with tools/jp.py perf: plugin 0.1.0, commit e816f12+changes, MOD OS v1.13.5.3315, jackd 48000 Hz, 128 frames; input siggen white noise at -12 dBFS; median of 3 cycles of 6 s with and without the plugin.
<!-- perf:end -->

## Tests

`tests/unit/Plugins/MeterPedal_test.cpp` (protocol and timing, sine results, difference, bands against
a one-pole lowpass, band level on a bin centre, short windows, counts, block size
independence), the generic host tests, `tests/host/TestPlugins_test.cpp` (shipahoy's filter
shapes measured through the meter within 0.07 dB) and on the device
`tools/device_tests/test_plugins_check.py`.
