# probe: control output readback and MIDI check

`plugins/probe/` (`jp::ProbePedal`), bundle `jaypacks-probe.lv2`, URI
`https://github.com/scjurgen/jaypacks-dwarf-dsp/plugins/probe`. Mono in, mono out, the audio
passed through unchanged, a MIDI input and a MIDI output. The smallest plugin with control outputs: it checks
that a host, mod-ui in particular, reports control output values to a client, that MIDI
messages (all 16 channels) reach a plugin and leave it again, and that the host's worker thread runs a job and
answers. Part of the test plugins (`docs/test-plugins.md`).

## Ports

| Port | Direction | Range | Meaning |
|---|---|---|---|
| `echoIn` | input | -1000 to 1000 | Any value |
| `echoOut` | output | -1000 to 1000 | Repeats `echoIn` after the next audio block |
| `seconds` | output | 0 to 16777216 s | Audio time since the plugin was activated |
| `midiEvents` | output | 0 to 16777216 | MIDI messages received since activation |
| `midiChannels` | output | 0 to 65535 | Bit n set when a message arrived on channel n + 1 (system messages belong to no channel) |
| `midiStatus`, `midiData1`, `midiData2` | output | 0 to 255 | The last message |
| `midiSent` | output | 0 to 16777216 | MIDI messages passed on to the MIDI output (every one, at the frame it came in) |
| `midiDropped` | output | 0 to 16777216 | Messages the host's output buffer could not take |
| `workerJobs` | output | 0 to 16777216 | Jobs worked on the host's worker thread (one per note on with a velocity above 0) |
| `workerAnswers` | output | 0 to 16777216 | Answers received on the audio thread, at the start of a later block |
| `workerNote` | output | 0 to 127 | The note of the last answer |
| `midiIn` | input (atom) | | MIDI input |
| `midiOut` | output (atom) | | MIDI output |

With these a script measures the readback path on the device: which message carries an
output value, how often values arrive, and how long a value set on an input takes to come
back (`tools/device_tests/probe_readback.py`; results in `docs/test-plugins.md`). The MIDI
counters do the same for MIDI: `tools/device_tests/midi_check.py` plays messages into the
device and reads what arrived.

## Techniques

| Technique | Where |
|---|---|
| Audio time as an integer sample count divided by the sample rate, so it does not drift (as a float it resolves about 60 us after 1000 s) | `plugins/probe/ProbePedal.h` |
| Control outputs listed in `modgui:monitoredOutputs`, so mod-ui asks mod-host for them | generated `modgui.ttl` (`generator/jpgen.py`) |

Common to every plugin (the LV2 adapter, `lv2/Lv2Adapter.h`): any host block size,
flush-to-zero for denormal floats while `run()` executes (`lv2/FlushDenormals.h`), NaN
control values ignored (bit-pattern test, safe under `-ffast-math`), no allocation, locks or
I/O in `run()`.

## Performance on the Dwarf

<!-- perf:begin -->
| Settings | CPU (points of mod-ui's DSP load) | Range over the cycles |
|---|---|---|
| idle (no note) | 8.7 | 8.7 to 8.9 |

Memory: the plugin allocates 0.2 kB when it is created (at 48 kHz, peak 0.2 kB, nothing while it runs; counted by its memory probe on 2026-10-17); the binary is 10 kB. Xruns while processing: 0.

Measured 2026-10-17 with tools/jp.py perf: plugin 0.1.0, commit 2552992+changes, MOD OS v1.13.5.3315, jackd 48000 Hz, 128 frames; input siggen white noise at -12 dBFS; median of 3 cycles of 6 s with and without the plugin.
<!-- perf:end -->

## Tests

`tests/host/Probe_test.cpp` (audio unchanged, echo, audio time, MIDI counts, channels and the
last message, the MIDI output at every sample for blocks of 1 to 2000, the worker round trip inline and on a
thread, a refused job), the generic host tests, and on the device `tools/device_tests/probe_readback.py`
and `tools/device_tests/midi_check.py`.
