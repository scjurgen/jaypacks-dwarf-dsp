# Testing

Test layers, as far as they exist here:

| Layer | Where | What | Run |
|---|---|---|---|
| L0 | `tests/unit/*_test.cpp` | DSP blocks, analysis blocks, pedals, the LV2 adapter's services, the scripting core (GoogleTest, one `unit_tests` executable) | `dev-scripts/dev-test.sh` |
| L1 | `tests/host/*_test.cpp` | The built `.so` through a minimal LV2 host (`Lv2Host.h`): generic tests for every bundle, integration tests per plugin, the test plugins wired together | `dev-scripts/dev-test.sh` |
| Python | `generator/test_jpgen.py`, `generator/test_graphics.py`, `tools/test_*.py`, `tests/host/modgui_check.py` | Generator and face layout; images (sizes, no clipping, fixed lighting, contrast of hand, value arc, rim edge, footswitch symbols and text for every `COLORS` entry); device client parsing; modgui of every bundle | `dev-scripts/dev-test.sh` (ctest `generator.*`, `tools.unittest`, `host.modgui`) |
| L2 | (not done) | The L1 harness inside the arm64 container against the Dwarf binary | roadmap, optional |
| L3 | `tools/device_tests/` | On the Dwarf: test plugins measure in the graph, read over the mod-ui websocket | `tools/jp.py test-device <name>` |

The whole Mac suite: `dev-scripts/dev-test.sh` (191 tests at step 10), the same under
ASan/UBSan with the `mac-asan` preset, and `dev-scripts/dev-warnings.sh` for 0 warnings. A
filter: `dev-scripts/dev-test.sh "MeterPedal|host\\.Probe"`.

## L0: unit tests

- Every `tests/unit/*_test.cpp` is picked up by a glob; test names are `<Suite>.<Test>`.
- `tests/unit/TestHelpers.h` has `c_pi`, `toDb()`, `toneAmplitude()` (Hann single-bin DFT),
  a steady-state sine gain measurement, and on a radix-2 FFT: `highBandEnergyDb()` (clicks:
  energy above a cutoff) and `spectralCentroid()` (the pitch of a granular shifter, whose
  carrier is spread over the grain rate); theory functions live with the blocks (e.g.
  `onePoleLowpassResponse()`, `poleMixingResponse()`).
- Test against theory or exact values, with tolerances that say what they allow; prefer the
  analysis blocks of `dsp/Analysis/` for measurements (`ToneFit`, `TransferBand`,
  `ZeroCrossingFrequency`, `SignalStats`).
- Every ported algorithm brings its unit tests (CLAUDE.md).

## L1: host tests

`Lv2Host.h` reads the bundle's TTL like a host (the Turtle subset the generator writes),
loads the binary with `dlopen`, connects every port and runs `process()` /
`processChannels()` in host blocks; `output(symbol)` reads control outputs. It provides `urid:map` and the
worker like mod-host (a job is worked after the `run()` that scheduled it, its answer reaches `work_response()` at
the start of the next one; `startWorkerThread()` works the jobs on a thread of their own, `refuseJobs()` makes the
host refuse them), and collects the events of a MIDI output (`takeMidiOut()`), which it checks in every block:
a sequence of the right type, events of 1 to 3 bytes, frames in order and inside the block. It finds every
`jaypacks-*.lv2` in `<build>/lv2/`. ctest lists the host tests when it runs
(`DISCOVERY_MODE PRE_TEST`), so a newly added bundle gets its generic tests without a change
to `host_tests` itself.

**Generic tests** (`GenericPlugin_test.cpp`) run for every bundle without plugin code:

| Test | Requirement |
|---|---|
| `ManifestTtlAndBinaryAgree` | URI, binary name, port indices 0..n-1, defaults in range, one plugin per binary |
| `ControlOutputsStayInTheirRange` | Every control output finite and inside its port range, before and during processing |
| `SilenceAndNoiseGiveFiniteOutputAtEverySampleRate` | 44.1, 48, 96 kHz: finite output, peak below 100 |
| `EveryControlAtItsMinimumAndMaximumStaysFinite` | Each control alone and all together at the extremes |
| `EveryControlReachesThePlugin` | The extreme farther from the default changes the audio or a control output (a plugin with a MIDI input gets a note on channel 1, a pitch bend and a note on channel 3 first) |
| `OutOfRangeAndNanControlsAreHarmless` | -1e9, 1e9, NaN on every control |
| `OutputDoesNotDependOnTheBlockSize` | Blocks of 1, 7, 16, 128, 4096 give identical audio and control outputs |
| `MidiEventsActAtTheirSampleWhateverTheBlockSize` | Plugins with a MIDI input: channel voice messages of every kind on all channels at random sample positions give identical audio and control outputs for blocks of 1, 7, 16, 128, 4096 (skipped without MIDI input) |
| `InPlaceProcessingEqualsSeparateBuffers` | Shared input/output buffer (skipped without audio input) |
| `ActivateClearsTheState` | Silence in, silence out after `activate()` (skipped without audio input) |
| `CpuTimePerBlockIsSmall` | Below 10 % of the real-time budget on the Mac (prints the figure; skipped in sanitizer builds, which run many times slower) |

So a new control must change something audible or reportable, and a new plugin must be
deterministic for a fixed seed. A plugin with a MIDI input is driven in the host by
`Instance::sendMidi(sample, status, data1, data2, size)` (queued for the next `process()`,
delivered in the block that contains the sample, at its frame; the host provides `urid:map`).
The adapter's handling of events (cuts at the frames, events outside the block, short and
long messages, a missing `urid:map`) is tested without a binary in
`tests/unit/Lv2/Lv2AdapterMidi_test.cpp` with a recording pedal, its MIDI output (frames and order, offsets from
the start of a call, raising and clamping, a full buffer, bad sizes, no connected port, a plugin without audio ports)
and its worker (the required feature, the extension data, scheduling and refusal, `work()` and the answer) in
`tests/unit/Lv2/Lv2AdapterServices_test.cpp`; the probe plugin shows both through a binary
(`tests/host/Probe_test.cpp`, also on a worker thread; run under ThreadSanitizer once). The generic tests do not
play notes yet: an instrument's "every control reaches the plugin" check needs a held note,
which the first MIDI instrument adds.

**Per-plugin tests** go into `tests/host/<Name>_test.cpp`. They judge only what comes out
of the binary (audio and control outputs) and use the same analysis blocks as the meter.
Patterns used in `ShipAhoy_test.cpp`:

- set ports, then `reactivate()`, so smoothers start at the values;
- measure a filter as a transfer function from noise (`TransferBand`, 16384-sample blocks)
  against the theory;
- read hidden state from the audio (hop positions from the amplitude of a probe tone) and
  compare with a plain pedal object running alongside with the same seed;
- print the measured numbers (`std::cout << "[ tag      ] ..."`), so a run documents itself.

Patterns added in `Eigenghost_test.cpp` (a reverb, stereo out; the harness returns the
first output):

- the TTL defaults against the pedal's `c_default*` constants;
- decay time from the energy decay curve of an impulse response (Schroeder backward
  integration, least squares between -5 and -35 dB);
- clicks as energy above 5 kHz on a 237 Hz sine (`highBandEnergyDb()`), with a deliberate
  step as the control that shows the check can fail; for a ringing tank the largest sample
  step is no measure, but for dry/wet gain jumps it is (as for shipahoy's mix);
- intervals in the tail as tone levels (`toneAmplitude()`) with and without pitch shifting.

`TestPlugins_test.cpp` wires siggen, shipahoy and meter like the device board and uses the
meter's measure protocol; a device test can be developed there first.

## L3: device tests

See `docs/test-plugins.md` for the plugins, the readback path and the protocol, and
`docs/device.md` for access. `tools/jp.py test-device shipahoy` deploys siggen, meter and
shipahoy and runs `tools/device_tests/test_shipahoy.py`: 284 checks at step 10. The
expected filter responses come from the C++ program `shipahoy_expected` of the test build,
so the theory exists once. `tools/jp.py test-device eigenghost` does the same for the
reverb with two meters (left against the input, right against left); its expected wet
levels come from `eigenghost_expected`, which runs the pedal itself.

`tools/device_tests/midi_check.py` checks the MIDI path with the probe plugin (below,
`docs/test-plugins.md`: MIDI on the device).

`tools/jp.py perf` measures the CPU of every plugin on the device (`docs/device.md`) and takes
the memory from the memory probes (below); `tools/performance.py memory` refreshes only the
memory; `tools/test_performance.py` checks that the tables in the docs match the JSON files.

## Memory

Every plugin has a memory probe, `build-tests/memprobe/jaypacks-<name>-memprobe`: its own
`lv2/PluginEntry.cpp` and generated `ports.h` (the code of the `.so`) linked with
`tests/host/MemoryProbe.cpp`, which replaces the global `operator new` and `operator delete`
in that executable and counts what passes through them. It instantiates the plugin at
48 kHz, activates it, runs 2 s of noise in blocks of 128 (defaults, every control at its
minimum, at its maximum, defaults), cleans up, and prints the bytes live after
`instantiate()` and `activate()` (the plugin's memory; a plugin with a MIDI input gets `urid:map`
and a note on and a note off in every block), the peak, the allocations during
`run()` and what `cleanup()` left. A plugin with a MIDI output gets a buffer for it, a plugin with a worker
the host's worker (jobs in fixed storage, worked after the `run()` that scheduled them, their allocations counted
apart as `workerAllocations`, since allocating there is allowed). ctest runs every probe as `host.memory.<name>` with
`--check`, which fails on any allocation in `run()` or anything left after `cleanup()` (a
scratch plugin that allocates in `process()` made it fail: 748 allocations, one per block).
The figure is the requested bytes (the allocator adds its own overhead); `malloc` called
directly is not counted (the plugins do not call it). The device runs the same code at the
same rate, so the figure holds for the Dwarf, where it cannot be measured
(`docs/device.md`).

## Thresholds in use

| Check | Threshold | Why |
|---|---|---|
| Filter shape, Mac (step 8) | 0.2 dB, coherence > 0.98, where theory > -40 dB | measured 0.077 dB |
| Filter shape, device | 0.3 dB, coherence > 0.98 | measured 0.10 to 0.15 dB with 8 s windows |
| Zipper (mix jumps) | largest sample step within 1.1 times max(dry, wet) | measured equal to dry |
| Exact equality (mix -100, bypass) | difference at the -240 dB floor, after 1 s settle | the 20 ms smoothers snap after about 0.34 s |
| CPU on the device | at most 5 points of mod-ui's CPU figure added | measured 2 to 3 points |
| xruns on the device | unchanged while the graph processes | graph changes reported only |
| CPU on the Mac | below 10 % of real time per plugin | shipahoy 0.1 %, meter 0.17 % |

## Adding tests

- A DSP block: `tests/unit/<Category>/<Block>_test.cpp`, the category as in `dsp/`.
- A plugin: nothing to register for the generic tests; plugin behaviour in
  `tests/host/<Name>_test.cpp`; a device test as `tools/device_tests/test_<name>.py` built on
  `devicetest.py`, run with `tools/jp.py test-device <name>`.
- Python (standard library `unittest`): `tools/test_*.py` are picked up by ctest
  `tools.unittest`; the generator tests are registered per file in `plugins/CMakeLists.txt`
  (`generator.unittest` runs `test_jpgen.py`, `generator.graphics` runs `test_graphics.py`),
  so a new generator test file needs an `add_test` there.
