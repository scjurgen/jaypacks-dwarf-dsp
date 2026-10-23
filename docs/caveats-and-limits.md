# Caveats and limits

Things that bit, or would bite, when developing for the Dwarf with this repository. Each item
names where it is decided or measured.

## Compiler and language

- **C++17 only.** The device toolchain is GCC 9.4; C++20 features from AbacDsp
  (`std::span`, `std::numbers`, ranges, concepts) are replaced when porting.
- **Apple clang accepts code GCC 9 rejects.** Example: `Foo() noexcept = default;` is
  deleted by GCC 9 when a member's constructor is not `noexcept` (libstdc++ `std::complex`),
  and the error only appears in the Dwarf build. Cross-build new code early
  (`tools/jp.py build-dwarf <name>`).
- **Floating-point flags differ.** The device build uses `-ffast-math -fno-finite-math-only`
  and `-O3` (from the toolchain's `local.env`), the Mac build neither. Results can differ in
  the last bits; compare Mac and device with tolerances, not bit equality, except where
  the output must be a copy (mix -100, bypass). Check NaN/Inf with a bit test, which holds
  under any flags (`SignalStats::isNonFinite`, the adapter's NaN test).
- **Denormals.** `run()` sets flush-to-zero itself (`lv2/FlushDenormals.h`); a host does not
  do it for a shared object. Smoothers snap to their target so their state never decays into
  denormals (`dsp/Parameters/ParameterSmoothing.h`).
- **Random numbers.** Only `dsp/Numbers/Random.h`: the `std::` distributions give different sequences
  with libc++ and libstdc++. Every instance uses the same seed (the Lua `math.random` of a script too), so two shipahoy
  instances with the same script and settings hop identically.

## Real time

- One `run()` at 48 kHz and 128 frames is 2.67 ms for the whole graph, on a Cortex-A35.
  shipahoy costs 0.1 % of real time on the Mac and 2 to 3 points of mod-ui's CPU figure on
  the device; the figure itself varies by several points between runs, so compare within one
  run.
- The Mac does not predict the Dwarf's cost: eigenghost at 7.8 us per 128 frames on the Mac
  first cost 8.8 points on the device, shipahoy at 2.2 us costs 0.1. Measured with a
  cross-built benchmark on the device (step 16), the causes were (1) memory: delay lines
  larger than the A35's 512 kB L2 cache wait for memory on every block and line unless the
  next block is prefetched (the delay alone went from 56 to 27 us per 128 frames);
  (2) aliasing: a loop that reads a `const float*` parameter and writes members of the same
  object is not vectorised or kept in registers, because the pointer could point into the
  object; a local copy of the input block fixed it (the tank went from 102 to 71 us);
  (3) GCC's cost model on the A35 often judges NEON not worth it (the A35 executes 128-bit
  NEON operations in two halves), so arithmetic stays close to scalar speed. Time the parts
  on the device before optimising.
- No allocation, locks, file or console I/O, or exceptions in `process()`/`run()`
  (CLAUDE.md); the adapter allocates only in `instantiate()`.
- Parameters must be smoothed per sample inside the pedal: a hardware knob has 201 steps,
  and the adapter passes values once per block (2.67 ms), which would be audible as zipper
  noise without smoothing .
- A 20 ms one-pole smoother reaches its target exactly only after about 17 time constants
  (0.34 s), when it snaps (`OnePoleSmoother::c_snapDistance`); tests that expect identical
  output wait longer than that.

## mod-ui and the device

- mod-ui moves a pedal on the board through its drag handle, a layer over the whole pedal
  at z-index 20 (`pedals.css`, cursor `move`). A modgui element above it that covers the face
  takes the mouse instead, and a drag there pans the board (closed-hand cursor): the step 13
  knob group did that until it got `pointer-events:none` (the knobs `auto`); checked by
  `generator/test_jpgen.py`.

- **Reinstall while in use.** `/sdk/install` refuses to replace a plugin used by the loaded
  graph: `jp.py deploy --unload` (unloads a saved pedalboard only; an unsaved graph stays
  and the install still fails, 2026-10-16), or empty the graph first (`test-device` does).
- **Control outputs** reach websocket clients only when listed in `modgui:monitoredOutputs`
  (the generator does it), only while the client echoes `data_ready`, as text with six
  decimals (report levels in dB), about every 12.5 ms and only on change.
- **Version display.** mod-ui shows `minor.micro` (0.1.0 appears as "1.0") and calls it
  "testing"; which versions it calls stable is still to be looked up before a release
  (roadmap).
- **Port properties.** mod-ui matches `logarithmic` by name, so the old generator's wrong
  `lv2core#logarithmic` still worked there; other hosts may ignore it. We write
  `pprops:logarithmic`.
- **modgui** is shown only by mod-ui (device web UI), not by desktop hosts. The stylesheet
  places knobs and footswitch at absolute positions; mod-ui's own rules for these elements
  set only `z-index`. At mod-ui's 100 % zoom the pedals are drawn at half size, so a 60 px
  knob is 30 screen pixels: hand and value arc still reach their contrast targets there, the
  rim edge is thinner than a pixel (`documentation/Graphics/`).
- **Graph changes.** One xrun was seen once while a plugin was added to a running graph
  (step 10); device tests judge xruns only while the graph processes.
- **Device clock** is not synced: read the journal after a cursor, not after a time.
- **No SFTP:** `scp -O`, or `ssh ... 'cat file' > file`.
- **Hardware actuators** can be assigned and checked (`hw_map`) by script, but not turned or
  pressed; that stays a manual check.

## Mac side

- **modgui images need numpy and Pillow** from the venv, so they are made on the Mac
  (`JP_GRAPHICS`); the Dwarf build takes the generated files from the Mac build. A knob
  filmstrip takes about 3 s per plugin.
- **Python tooling** uses only the standard library, except the venv `.venv` for charts and
  images.
- **docker image** `dwarf-new` is about 19 GB and takes about 15 minutes to build.

## DSP findings that shape tests

- Hann blocks of 4096 samples are too short for transfer measurements at 63 and 125 Hz on
  steep slopes (coherence 0.979); the meter and the tests use 16384
  (`documentation/Analysis/`).
- shipahoy's filter stages clamp their state at +-1. A 1 kHz sine of 0.25 at 85 % resonance
  and a 1 kHz frequency already reaches the clamp, because the resonance peak lies 5 % below
  the knob; level tests near the peak need small signals (step 8).
