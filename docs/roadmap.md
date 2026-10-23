# Roadmap

The step list this project was created with (2026-10-14), with the status of
each step, and what comes next. Sources are in `docs/references.md`. One step
is one reviewable commit; update the status table and the step text when a
step is done or the plan changes. Later plans number their steps from 1 and have their
own section below (Jays Twin); "Jays Twin step 1" names a step of such a plan.

## Status

| Step | Content | Status |
|---|---|---|
| 1 | Skeleton, licence, CMake, dev-scripts, references, roadmap | done |
| 2 | DSP for shipahoy plus unit tests | done |
| 3 | The shipahoy pedal | done |
| 4 | Algorithm documentation (charts and analysis) | done |
| 5 | LV2 adapter, generator, Mac build, host tests | done |
| 6 | Graphics and modgui | done |
| 7 | Docker, Dwarf build, deploy | done |
| 8 | Mac integration test for shipahoy (knobs and switches) | done |
| 9 | Test plugins (signal generator, meter; own mini-plan first) | done |
| 10 | Device integration test for shipahoy | done |
| 11 | Documentation completed | done |
| 12 | Refinement: docs state current facts (decision log removed, private and local references moved to the gitignored `local-info/`), `dsp/` and `tests/unit/` in AbacDsp categories, performance on the Dwarf per plugin (`tools/jp.py perf`), techniques per plugin, snapshot of shipahoy, REAPER, GenAI and Lua outlook docs, `dev-scripts/README.md` | done |
| 13 | modgui redesign: milky casing, generated knob and footswitch, sans-serif text (Inter), knob layouts for 1 to 6 controls, casing width by control count, brand "Krachpedal", colours at saturation 1 and lightness 0.4, contrast checks; design tool `documentation/Graphics/` | done |
| 14 | Eigenghost DSP: FDN tank (order 16, 0.5 s lines, crossfaded size changes), 6-stage Schroeder diffuser, block pitch shifter, one-pole highpass, Walsh-Hadamard, primes and bulge, with unit tests | done |
| 15 | Eigenghost pedal (mono in, stereo out), JSON, host tests, charts in `documentation/Eigenghost/` | done |
| 16 | Eigenghost: Dwarf build, tank optimisation, performance, device test, plugin page, snapshot (listening in Reaper by the author open) | done |

## Jays Twin

A simple Fender Twin Reverb (AB763) from biquads, for guitar (plan of 2026-10-16): the real
tone stack (Yeh closed form, AB763 values), the bright cap across the volume pot (follows the
gain knob, a setting without face control), an atan drive, a 2x12 cabinet fitted to the
Jensen C12K datasheet curve with an assumed open back, level. Face: bass, mid, treble over
gain, level. Charts and method in `documentation/JaysTwin/README.md`.

| Step | Content | Status |
|---|---|---|
| 1 | DSP blocks with unit tests: `Biquad.h` (port), `BilinearTransform.h`, `FenderToneStack.h`, `VolumeBright.h`, `BiquadCascade.h`, `TwinCabinet.h` (C12K fit), `NonLinear/AtanDrive.h`; traced C12K curve, cabinet fit, charts | done |
| 2 | The pedal `plugins/jaystwin/` (JSON, `JaysTwinPedal.h`, smoothing, tone stack makeup gain, gain mapping, output trim), `RampedBiquad.h` (coefficient ramps between designs), pedal unit tests, host tests (sweeps without clicks, level against gain and presets, aliasing at high gain, no subnormals), `docs/plugins/jaystwin.md`, README; host tests discovered when ctest runs | done |
| 3 | Dwarf build, `perf`, device test (`test_jaystwin.py`, `jaystwin_expected`), snapshot, gain up to eleven; the author's listening test in Reaper (2026-10-16: sounds ok for now) | done |

Measured in step 1: closed form against a nodal analysis of the circuit 1.6e-12 dB; digital
against analog at the warped frequency 6e-4 dB (48 kHz), the warp up to 5 kHz 0.28 dB;
cabinet fit 0.68 dB rms over 30 Hz to 8 kHz (at most 2.4 dB up to 6 kHz); all headers compile
with the device GCC 9.4 (`-Wall -Wextra -Wpedantic -Wshadow -Wconversion`, no warnings).

Measured in step 2: small signals follow the designed chain within 0.02 dB; the defaults are
as loud as the input (a chord peaking at -12 dBFS, 0.2 dB apart in RMS), gain 0 to 10 adds
21 dB, the presets lie within 0.3 dB of the defaults; knob sweeps and jumps stay within 1.17
times the largest steady step (treble; designs applied at once every 16 samples made 3.1
times, hence the coefficient ramps, which equal a redesign per sample within 0.03 %); aliases
at gain 10 -98 dB (1.3 kHz) and -42 dB (3.1 kHz) re fundamental, at gain 7 -84 dB or lower;
1.9 us per 128 frames on the Mac.

Measured in step 3: 0.5 to 0.7 points of mod-ui's DSP load (defaults and presets), binary
22 kB, no xruns; the device matches the Mac within 0.054 dB in the band gains from 250 Hz up
and within 0.001 dB in the drive levels (47 checks). The author asked for the gain to go up
to eleven (Spinal Tap): 0 to 11, 3.6 dB of drive per step, the bright cap ends at 10.

Later, if needed: oversampling of the drive (in the numbers only high notes at high gain
alias strongly, at 11 a 3.1 kHz tone as strongly as its harmonics; the author's listening
test found the plugin ok without it); the open-back highpass (80 Hz, an assumption) and the
cabinet target can be replaced by a measurement of a Twin or a modeller (the author's setup).

## Eigenghost refinements

The Eigenghost items of the author's `TODO.md` (plan of 2026-10-16): line bulge default 0.6
(early reflections first), a pre-delay (0 to 200 ms, default 20 ms), the diffuser in metres
(spread 3 and bulge 0.4 fixed) and a 0.5 Hz modulation on lines 4 and 5 with a depth in cents
(0 to 20, default 4). Pre-delay and depth are settings; the face stays as it is.

| Step | Content | Status |
|---|---|---|
| 1 | `dsp/BlockProcessors/BlockProcVibrato.h` (port, sine LFO, allpass interpolation), callback latency in `FdnTank`, unit tests | done |
| 2 | The pedal and JSON (bulge 0.6, pre-delay, diffuser in metres, modulation on lines 4 and 5, version 0.1.1), pedal and host tests, charts (new: modulation), docs | done |
| 3 | Dwarf build (version script for the exports), `perf`, device test (modulation check), presets Plate and Small room, fractional pitch; the author's listening test (also of the presets with the new defaults) | done, listening open |

Measured in step 1: the vibrato's pitch deviation equals the set cents within 1 % (4, 10, 20
cents at 0.5 Hz); a depth-0 vibrato with its latency registered leaves the loop length exact
(T60 2.027 s against 2.028 s without); with two modulated lines a lossless tank loses 0.18 dB
(4 cents) and 0.41 dB (20 cents) over 10 s. Hermite interpolation (the plan) filtered the
lines on every pass (a 10 s decay fell to 8.2 s in the highs); the allpass, agreed with the
author, keeps them at 9.85 to 9.92 s.

Measured in step 2: the pre-delay moves the wet onset by exactly its samples, a change every
133 ms stays at -110 dB above 5 kHz; the depth smooths over 500 ms; with the new defaults the
first echo comes at 97.5 ms at diffusion 0 (pre-delay, diffuser 42 ms at 1.3 m, shortest line),
the echo density reaches 0.9 after 140 ms (308 ms without diffusion), the decay fits 0.491,
0.969, 2.79, 8.90 s for 0.5, 1, 3, 10 s (shorter mean lines: 17 instead of 12 passes per
second through the damped lines), wet level +1.2, +5.5, +8.3 dB, left/right correlation 0.09.
The modulation spreads a 1 kHz tail (outside +-3 Hz: 36 % at 0, 47 % at 4, 98 % at 20 cents)
but does not lower the strongest late-tail mode against the mean (18.3 dB at 0, 18.1 dB at 4
cents); the listening test decides.

Measured in step 3: 6.0 points of mod-ui's DSP load at the defaults (4.7 before the
refinements; 7.2 with `std::sin` per sample in the vibrato, which a cross-built benchmark
showed as 40 of its 49 us per 128 frames; now one sine per block and a rotation, 17 us); wet
levels on the device within 0.02 dB of the Mac; the modulation raises the residual beside a
held 1 kHz tone from -108.5 dB (depth 0) to +4.4 dB (20 cents). Presets "Plate" (dense after
30 ms, 2.56 s) and "Small room" (dense after 40 ms, 0.66 s) added at the author's request;
Pitch 1 and 2 take fractional semitones. The Dwarf build exports only `lv2_descriptor`
through `lv2/exports.map` (a `std::vector` member had become visible).

## Morphexsynth

An MPE subtractive synth after AbacDsp's `examples/morphexsynth` (plan of 2026-10-16,
`.claude/morphexsynth-port.md`): three wavetable oscillators through the existing pole-mixing
filter, three envelopes, an LFO, distortion, a 10-slot MPE routing matrix and a phaser, chorus and
reverb bus (the reverb on `jp::FdnTank`), 12 voices, no audio input. Until Lua exists the sounds
are the ten factory patches compiled in as C++ settings (a Patch menu); the Lua part is last, as
its own mini-plan, and replaces the compiled-in patches by scripts that produce the same
settings.

| Step | Content | Status |
|---|---|---|
| 1 | MIDI input in the LV2 chain: `midi-in` in the JSON, atom port and `urid:map` in the generator and adapter (blocks cut at the event frames), `Lv2Host.h` with MIDI, probe counters, vendored atom and midi headers; the Dwarf's MIDI path measured, `midi_check.py` | done |
| 2 | MPE and voice management blocks (`dsp/Synthesizer/`, `dsp/SynthHandling/`, `dsp/Parameters/FixedSmoothing.h`): curves, routing slots, zone, voice allocator, sustain pedal, modulation targets, with unit tests | done |
| 3 | Wavetable oscillator (`dsp/Wavetables/`), tables built outside `run()` and shared by the voices; charts in `documentation/Wavetables/` | done |
| 4 | LFO and brown noise, envelopes, distortion tables and interpolation kernels | done |
| 5 | The voice (`dsp/Synthesizer/MorphexsynthVoice.h`), and a cross-built benchmark on the Dwarf that fixes the voice count; the filter is the band-pass feedback variant of AbacDsp (below) | done |
| 6 | Effects bus (`dsp/Synthesizer/MorphexsynthEffects.h`): phaser (`dsp/Filters/Phaser24.h`), chorus (`dsp/Delays/ModulationDelay.h`), reverb on `jp::FdnTank` | done |
| 7 | The plugin (`plugins/morphexsynth/`): pedal, the ten factory patches as C++ tables, JSON, unit and host tests with MIDI (the author listens on the Dwarf, not in Reaper: 2026-10-17) | done |
| 8 | Dwarf build, `perf`, device test (`test_morphexsynth.py`, `morphexsynth_expected`), snapshot, plugin page `docs/plugins/morphexsynth.md`; the author's listening test on the Dwarf open | done |
| 9 to 12 | The Lua part became its own plan, section "Lua scripting" below | see there |

Measured in step 1 (Dwarf, 2026-10-16, probe plugin): mod-ui lists the plugin's MIDI input and
mod-host makes the JACK port `effect_<N>:midiIn`; with the board's MIDI input connected to it,
messages played through the ALSA sequencer arrive complete: 16 note ons on 16 channels (16 of 16,
mask 0xFFFF), an MPE-style burst of note on, pitch bend, channel pressure and CC74 on channels 2
to 16 (60 of 60, mask 0xFFFE, last message intact), 500 messages 1 ms apart (500 of 500), program
change and timing clock (both). A separate JACK client lost about half of its events on the way
through the merger, and events written straight into the plugin's port never arrived without the
board connection; details in `docs/device.md`, "MIDI". On the Mac the adapter's event handling
has 7 unit tests with a recording pedal, the probe 5 host tests, and every MIDI plugin gets a
generic test that random channel messages at random samples give the same audio for blocks of 1
to 4096 (passes for the probe; ASan and UBSan clean).

Measured in step 2: 58 new unit tests (484 in all), clean under ASan and UBSan, headers and tests compile with
the device GCC 9.4 (`-Wall -Wextra -Wpedantic -Wshadow -Wconversion`, no warnings). The curve tables of the
source (5 x 16384 floats, 320 kB, shared by the voices) are gone: curves are computed per controller
message. Two things differ from AbacDsp on purpose (provenance table): `ValueConnector` ignores an enumerator
that is not listed (the source wrote it into its first slot when it lay below the largest listed one; a mutation
of the table default makes the new test fail), and a 14-bit bipolar value reaches exactly -1 and +1.
For step 7: 7-bit controllers (CC74, mod wheel, channel pressure) go through `mpeValueFrom7Bit()`, not a
shift by 7; the engine's 16 sustain slots and 12 voices are `SustainPedalHandler<16>` and `VoiceAllocator<12>`.

Measured in step 3: 26 tests of the bank and the oscillator (510 in all), clean under ASan and UBSan. The
bank builds all eleven waveforms in 2.6 ms on the Mac (the Dwarf figure comes with the step 5 benchmark) and
takes 131200 bytes per waveform (1.44 MB for all; the five waveforms of the synth: 656 kB, against about 14 MB
that AbacDsp's per-oscillator copies would be for 36 oscillators). The oscillator's pitch is exact to 1e-4 cent
from 20 Hz to 12 kHz; the non-harmonic power of a saw, square and shark fin from 55 Hz to 11 kHz is at most -67 dB
re total (-60.9 dB for a saw from 40 Hz to 12 kHz), against -6 dB at 8 kHz with one table for all pitches; a
level kept while the pitch rises stays clean up to the designed factor 1.67 (-60 dB crossed at 1.69). A mutation
(always the first level) fails the aliasing test. Fixed against AbacDsp (provenance table): a pulse-width ramp that
lasted 480 blocks, a block fast path without a wrap check (replaced by a per-sample wrap), and per-oscillator table copies.
The tables are 128 kB per waveform that every voice's three oscillators read at different pitches: whether
that costs cache misses on the A35 is measured in step 5.

Measured in step 4: 52 new unit tests (562 in all), clean under ASan and UBSan, no warnings with the device GCC 9.4.
The LFO runs at the rate it is stepped at (the voice steps it, and the envelopes, at a 16th of the sample
rate); its sine is exact to the tick (zero crossings within 1e-4 relative from 0.05 to 40 Hz at a 3 kHz tick).
AbacDsp's README counts 30 distortion presets, the table has 31. All ten factory patches use only the filter
type "LP4", which is the "4 pole lp" entry of shipahoy's table ("lp 24 dB"): decision 2 holds without
a new filter family; the script's other type names (LP2, BP4, HP2, HP4, Notch ...) are mapped to the same
table in the Lua part. No charts in this step.

Measured in step 5 (the A/B runs compiled AbacDsp's voice alongside as one-off programs, not kept):
- Dwarf, 128 frames, voices all sounding, a patch with three oscillators, pulse width, resonance, filter
  envelope and LFO, notes 36 to 91: 1 voice 61 us (2.3 % of the 2667 us period), 4 voices 259 us, 8 voices 614 us,
  12 voices 926 us (34.7 %, 99th percentile 1272 us, worst 1473 us = 55 %; 1031 us with the first filter); a patch
  without moving filter and LFO costs 807 us for 12. Parts: a saw oscillator 4 us per 128 frames (9 us with pulse
  width), the filter 12 to 14 us, the LFO 1 us for 8 ticks. Set-up: the five waveforms of the bank 62 ms (the mac builds all eleven in
  2.6 ms), the shaper tables 2.4 ms, 12 voices 2.9 ms; one voice is 2808 bytes, the bank 656000. With the effects
  bus (Eigenghost's tank costs 151 to 159 us) 12 voices fit; the figure that counts is mod-ui's, measured in
  step 8. Zero-level oscillators are still computed (3 per voice): skipping them is the first saving to take if
  needed.
- Against AbacDsp's voice at resonance 0 the level of a saw through the filter is within 5 % (0.1753, 0.1600,
  0.1113, 0.0490 against 0.1663, 0.1526, 0.1071, 0.0473 for cutoffs 127, 84, 60 and 46); pitch is exact (largest
  error 5e-5 cent from note 24 to 96).
- **Decision on the filter (author, 2026-10-17): the variant with band-pass feedback.** The first voice used
  shipahoy's `PoleMixingFilter` (loop fed back from stage 4), whose resonance differs from AbacDsp's: the pass band
  fell by 5 to 8 dB (AbacDsp 0.5 to 3), a saw at 98 Hz with the filter open and dial 0.35 was 0.079 RMS against
  AbacDsp's 0.166, and a bass patch lost its bottom. `PoleMixingBandpassFilter` (`dsp/Filters/`, with
  `PoleMixingCorrections.h`) is the filter of AbacDsp's voice (`Filter1Pole4StageSmooth`: band-pass tap, saturated
  loop, fitted cutoff warp and critical resonance) on shipahoy's preset table: sample-exact against it (20
  combinations of preset and resonance, a moving cutoff, largest difference 0). In the voice, a saw at 98 Hz,
  level 0.5, gives the RMS of AbacDsp's to three digits at all cutoffs tried (0.166, 0.153, 0.107, 0.047 at 0
  resonance; 0.166, 0.150, 0.104, 0.076 at dial 0.35), and "Warm Bass" 0.1555 against 0.1544 at the start. The
  threshold table and the cutoff warp of the first version are gone. Peak heights on white noise (A/B program)
  still differ by a few dB at low resonance since the noise realisations differ, the filters do not. The loop input
  is saturated even at resonance 0 (a triangle of peak 0.87 leaves at 0.66), as in AbacDsp.
- Quirks of AbacDsp found by the A/B and kept or fixed (provenance table): the first note of a voice started
  soft (fixed), the velocity factor starts at 0.1 and not at the 0.47 of its range 0.15 (kept), the shaper gain
  is 2 until a Distortion slot moves it (kept), the envelope dimensions use half of the 14-bit range (kept).

Measured in step 6: the phaser and the modulated delay are sample-exact against AbacDsp (one-off comparison with the
source compiled alongside: phaser at four sample rates and three ramp lengths with a moving cutoff, the delay at three
speeds and four depths with a depth change; largest difference 0). 30 new unit tests (640 in all), clean under
ASan and UBSan, no warnings with GCC 9.4. On the Dwarf the bus (block 16, order-32 tank, everything on; the order was set to 16 on 2026-10-17, see step 8) costs 267 to
290 us per 128 frames (10 %: phaser 4 stages 34 us, two chorus lines 43 us, the rest the tank), so 12 voices and the
bus come to about 1.2 ms (46 % of 2667 us); the bus is not cheaper with everything off, since it still runs.
Findings: the chorus delay moves in steps once per 16 samples (the sweep phase is held between ticks), up to 0.31
samples at 1 Hz and a depth of 300 samples; a per-sample phase would make it smooth and is the first change to
try if the chorus is heard to zipper. The first chorus depth set (and a depth of 0) only arrives after one full sweep
period (1.67 s at the default rate), a quirk of AbacDsp kept for the sake of the comparison. The phaser saturates
its input with an atan and its feedback is negative, so resonance takes away the low end (-5 dB at 20 Hz for 0.2).
The tank replaces AbacDsp's glide of the read heads with the crossfade of `FdnTank`; wet and dry gains are smoothed by a
one-pole of 3 ms instead of a linear ramp of 10 ms.

Measured in step 7: 38 new tests (678 in all; 26 specific to the plugin and 12 generic ones for the new bundle): 19 on the pedal (notes, the zone, bend per channel by a spectral peak
within 8 cent, the value before the note on, sustain, 12 voices and the steal, blocks cut at 1 to 24000 samples give
the same output bit for bit, the ports), 7 on the bundle (MIDI through the `.so`, every patch different and bounded,
the bend of one channel, 12 voices for 10 s of audio in 0.2 s on the Mac, the steal without a click: the largest step
around it is 0.045 against 0.076 before it, the zone ports), and the generic tests now play notes for a plugin with a
MIDI input (a synth is silent without them). The memory probe: 3.95 MB allocated in `setSampleRate()`, nothing in
`run()`, nothing leaked (the preset table of the distortion is no longer a function-local static, which the probe
counted as a leak). Clean under ASan and UBSan, no warnings, GCC 9.4 syntax check clean.
Decisions and deviations of the plugin: the face has six knobs (Vol, Cutoff, Resonance, Reverb, Chorus, Phaser); Cutoff
and Resonance are offsets of the patch's values (the patches carry their own cutoff, so a dial value would override
them), Reverb is an offset in dB of the patch's mix (the lowest value switches it off), Chorus and Phaser scale the
patch's mixes (percent); Bend range and Glide (0: as the patch), the patch and the MPE zone are in the settings
(`hasUI` 0). The sound is made in blocks of 16 samples, so a MIDI event acts up to 15 samples (0.3 ms) late. A bend, CC74 or
pressure value that arrives before the note on of its channel is kept and applied to the next note (AbacDsp lets the
first note ignore it). The default patch is "Retro Poly". Changing the patch releases all notes and applies the patch
completely (all ten routing slots are written).

Measured in step 8 (the author set the tank to 16 lines on 2026-10-17; the numbers of step 6 are for 32): on the Dwarf an idle
plugin costs 7.0 points of mod-ui's DSP load (10.8 with 32 lines), one note 10.9, four 18.3, twelve notes 39 to 43 depending
on the patch (Cutting Lead 39.3, Retro Poly 40.6, Airy Pad 40.0, Warm Bass 41.6, Section Brass 42.7; with 32 lines 43 to 46); the
plugin allocates 2345 kB (3854 kB with 32 lines), nothing while it runs; no xruns while processing. The device test
(`tools/jp.py test-device morphexsynth`, 122 checks, no xrun in the whole run) compares the pedal on the device with the
same pedal on the Mac through the meter: the level of both channels within 0.06 dB for six patches, a single note, the
bend of a member channel (+2 semitones and, with Bend range 12, an octave) and two notes a tritone apart, the
zero-crossing frequency of single notes within 0.1 %; the zone ports, the sustain pedal, every control at its extremes
and all ten patches in turn with notes held give no non-finite and no denormal sample; 12 notes for 10 s stay alive.
Findings: (1) the pedal face had no jack for the MIDI input (the modgui template drew audio jacks only), so mod-ui
offered nothing to connect a controller to; the template, the screenshot and `modgui_check.py` now have it, and the same for the MIDI output jack (added later: the port was in the TTL but mod-ui drew no socket to cable from). (2) a USB
controller (the LinnStrument) shifts the JACK port numbers and made the test sender believe it was connected to
the merger (`docs/device.md`, MIDI). (3) Anything played on a connected controller reaches the plugin during a test.
(4) The levels of a chord of notes with a simple ratio (a fifth) depend on their relative phase over a 1 s window, the
zero crossings of detuned oscillators or noise are not a pitch: the test compares those by level or not at all.

The patches are documented in `docs/plugins/morphexsynth-patches.md` (settings, routing, measured effect of Y, Z and velocity;
the author will research how Y and Z should share the parameters, as only Cutting Lead sends both to one; velocity changes the level
by 2 dB only).

Changes after the author's first listening test on the Dwarf (2026-10-17): the default bend range is 12 semitones (was
the patch's own), Vol defaults to -6 dB and Reverb to -10 dB (the patches were too loud: 12 notes peaked at +2.6 dBFS),
the Chorus and Phaser knobs scale mixes that now follow by a one-pole of 10 ms (a step in the mix was audible as zipper
noise; a test checks it, and fails without the smoothing), poly pressure (0xA0) is Z as well as channel pressure, a new
Pressure setting (default 200 %) scales the slots that read Z (full channel pressure changed the level of Cutting
Lead by 0.5 dB and of Section Brass by 2.5 dB: the patches use Z for small steps of the cutoff), and the cutoff and
resonance of a voice are smoothed over 16 control ticks (5.3 ms) instead of AbacDsp's 128 (43 ms): the cutoff's target is
set at every tick, so the ramp of 128 steps restarted each time was a one-pole approach with a time constant of 43 ms
(90 % after 100 ms), which delayed the filter envelope and every slide of Y and Z (a jump of Y reached 90 % of its
effect after 51 ms, now within 30 ms, tested). The filter movement of the patches is therefore faster than in AbacDsp, and
the A/B against its voice (step 5) no longer holds for moving cutoffs, only for static ones. The author then heard
the pressure of a LinnStrument on Cutting Lead (Y and Z both on the cutoff) as a nervous jumping whose level fell with
harder pressure: `applyCtrl()` set a target to the slot of the dimension that arrived, so Y and Z overwrote each other
at every message of either (the source's documentation says the slots add); the voice now sums the slots of a target
from the last value of each source (reset at a note start), which a test checks and which fails with the old logic
(the level of both together was 0.089 against 0.094 for one of them alone). The steady level of Cutting Lead does not fall
with pressure (-20.6 dB rms at 0 to -20.3 dB at full pressure, 200 %). The Resonance range is -50 to +100 % (the patches
have 0.05 to 0.72, so -100 % reached below zero for all of them).

Deviations and findings for later steps:

- The MIDI check extends the `probe` plugin (counters and a MIDI input) instead of adding a new
  test plugin. `tests/host/MemoryProbe.cpp` now gives a MIDI plugin `urid:map` and two events per
  block.
- The generic tests do not play notes: an instrument's "every control reaches the plugin" and
  finite-output tests need a held note, which step 7 adds with the first instrument.
- mod-host sends all-notes-off and all-sound-off on all channels when a plugin is bypassed (read
  in its source): the instrument must handle CC 123 and 120.
- The names `CtrlCurve`, `CtrlDimension`, `CtrlTarget`, `CtrlValueType` and `MpeCtrl` are AbacDsp's, because the
  scripts' constants use the same numbers; `ctrlType` became `type`.
- Device tests inject MIDI with about 1 ms timing (ALSA sequencer, no sample accuracy): fine for
  pitch, level and routing, not for sample-exact checks (those are done on the Mac).

## Lua scripting

A scripting core shared by plugins, Lua 5.4 scripts as files on the device that users and LLM sessions write and
hot-reload without recompiling, Morphexsynth as the first user and a MIDI effect written in Lua as the second (plan of
2026-10-17, `.claude/done-morphexsynth-lua.md`; facts in `docs/outlook-lua.md` and `docs/device.md`). Decisions: scripts are
files (ssh, `scp -O`), the plain Lua C API (no sol2), handlers run in the worker thread, the factory patches become
scripts (the compiled tables stay as test oracle), only Morphexsynth specifics stay in the plugin, the core is in
`scripting/`, the wavetable morph comes last.

| Step | Content | Status |
|---|---|---|
| 1 | Probe on the Dwarf (throwaway plugin): worker, script directory, queue limits, Lua cost, MIDI out, tempo | done |
| 2 | Worker (`work:interface`) and MIDI output in the adapter and generator (`lv2/PedalServices.h`), host harness, probe plugin extended, device check | done |
| 3 | The scripting core (`scripting/`, `third_party/lua/`): runtime with memory limit, sandbox and watchdog, binding table, queues, script host with hot reload, log and status, generated API description (`docs/scripting.md`) | done |
| 4 | Morphexsynth bindings (`plugins/morphexsynth/MorphexScript.h`): the `Set*` commands, constants, hooks, the filter type names | done |
| 5 | The pedal runs scripts; the ten factory patches as scripts, golden test against the compiled tables | done |
| 6 | Dwarf: perf, device test, the user's loop over ssh, documentation | done |
| 7 | Core extensions for timed MIDI: sample time, timed events, clock, timers, stuck-note guard | done |
| 8 | MIDI Script plugin (MIDI in, MIDI out, logic in Lua): bindings, pedal, six scripts and three libraries, unit and host tests, plugin page | done |
| 9 | MIDI Script on the Dwarf: device test (`test_midiscript.py`, 72 checks), perf, latency measured, the user's loop over ssh, documentation | done |
| 10 | Three-slot wavetable morph: `SetWaveMorph`, oscillator and voice, tests, device check and CPU | done |

Measured in step 1 (Dwarf, 2026-10-17, details in `docs/device.md`): mod-host provides the LV2 worker; it wakes within
58 us (idle) and a response reaches the plugin at the next `run()`, one period (2.69 ms mean, 2.97 ms at most under 12
notes of Morphexsynth); the worker is `SCHED_OTHER` at nice -19 and the audio thread `SCHED_FIFO` 75. The worker's rings
are small: jobs of 8 kB are refused and only about 79 responses per period arrive, so bulk data uses the plugin's own
queues. The plugin process can read and write below `/data`, stat takes 30 to 57 us, reading 1.25 kB 77 to 160 us;
mod-ui has no route to upload scripts (fixed user-file types), they come by ssh. Lua 5.4: a new state 0.3 to 0.5 ms,
compiling a 1.25 kB patch script 0.6 to 0.9 ms, a note handler call 1.5 to 1.8 us, 31 million VM instructions per second
(15 million with the watchdog's count hook, so AbacDsp's 2,000,000 instructions are 87 ms), a string-growth bomb stops
at a 1 MB limit after 4 ms. MIDI out reaches another plugin (67 of 67 messages, all 16 channels) and the Dwarf's MIDI
output with the written frame offsets; the board tempo arrives in a control port with the designation
`time:beatsPerMinute` and as `time:Position` atoms on every block while the transport rolls. Not tested: saving state
through the pedalboard, a string parameter's limit, a file picker for a custom type, persistence of `/data` over an OS
update. Consequences for the plan: own queues instead of worker payloads, a watchdog budget of about 100,000
instructions per handler instead of 2,000,000, a command reaches the audio thread one period after the event.

Measured in step 2: the adapter takes the JSON keys `midi-out` and `worker` (the generator writes the output atom port,
`work:schedule` as required feature and `work:interface` as extension data; a plugin with `midi-out` may have no audio
ports and then has `process(n)`). A pedal gets a `MidiOutput&` (messages of 1 to 3 bytes at a frame counted from the
start of its `process()` or `midi()` call; the sequence is emptied at every `run()`, frames are raised to the previous
one and lowered to the block, what does not fit is dropped and counted) and a `WorkScheduler&`, and implements `work()`
(host worker thread) and `workResponse()` (audio thread, a later `run()`). 16 new unit tests of the adapter's services
with recording pedals, 10 generator tests, 6 host tests on the probe plugin (MIDI out at every sample for blocks of 1
to 2000, the worker round trip inline and on a thread, a refused job); the host harness works the jobs like mod-host
and checks every MIDI output sequence it reads; the memory probe gives a worker plugin the host's worker and counts
its allocations apart (`workerAllocations`, 0 for the probe, and nothing in `run()`). 706 tests pass, also under ASan
and UBSan; the worker-thread tests pass under ThreadSanitizer (a one-off build, not a preset); no warnings with Apple
clang and none with the device GCC 9.4 (`-Wall -Wextra -Wpedantic -Wshadow -Wconversion`, apart from the vendored atom
macros). On the Dwarf `midi_check.py` passes with two probes in a chain: 16 note ons on 16 channels, 500 messages and
200 note ons one millisecond apart arrive in the second probe, and the first probe's worker works and answers every
note on (16 of 16, 200 of 200); the Morphexsynth device test passes unchanged (no xrun while processing). The probe
costs 8.7 points of mod-ui's DSP load (8.4 before; the figure is mostly mod-ui's readback of its outputs) and 10 kB.
One expectation of mine was wrong: the outputs are written at the end of `run()` and the worker works after it, so
they show the jobs of a block one block later. `worker.h` is vendored from the lv2-1.18.2 archive, the other vendored
headers match it byte for byte.

Measured in step 3: Lua 5.4.8 as released (C, a fixed string-hash seed through a force-included header), `scripting/`
with 59 unit tests (runtime 26, commands and documents 10, queue 3, host 19, independence 1), all passing, also under ASan and UBSan; the queue and host tests under ThreadSanitizer, which flags the queue
when its release store is weakened to relaxed (checked and restored). The device GCC 9.4 compiles the core without
warnings (`-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wdouble-promotion`). On the Dwarf (a throwaway cross-built
benchmark, not kept): creating a state with the bindings 0.36 ms, loading a 1 kB patch with `OnStart` 0.61 ms, a hook
that calls a command with a table 9.8 us; an endless loop is stopped after 88 ms at the load budget of 2,000,000
instructions (also inside a pcall loop), a tail-call loop after 241 ms for the same count, a hook after 4.6 ms at
100,000 instructions (about 15 million instructions per second with the hook), a 8 MB `string.rep` after 0.12 ms and a
table-growth loop after 6.2 ms at the memory limit. Deviations from the plan: the memory is limited by an allocator
that refuses past the limit, not a fixed arena (the scripts run in the worker thread, so no allocation rule applies);
`BindingTable.h` is `Binding.h`; the host depends on `lv2/PedalServices.h` for the wake-up job. Found by the tests
and fixed: an endless loop in a `__gc` finalizer cannot be stopped because Lua switches hooks off while it runs, so
`setmetatable` refuses such a metatable; the pattern matcher is C code that no instruction count interrupts, so
subject and pattern lengths are bounded; deep recursion ends at the memory limit before Lua's stack limit. Not
done: the tests of "no allocation outside the arena" (not applicable), the cross-build of `jp_scripting` through
CMake (step 4 links it into a plugin; the strict compile of every file was done directly).

Measured in step 4: `plugins/morphexsynth/MorphexScript.h` has the 12 commands of AbacDsp's morphexsynth scripts with
their ranges and defaults (read from `MorphexsynthScriptEngine.h`) plus `SetVolume`, the six tables of constants with
AbacDsp's numbers (a test lists all 43 from its `constants.lua`), the hooks `OnStart`, `OnNoteOn`, `OnNoteOff`, `OnCC`,
`OnProgramChange`, `OnPitchBend` (channels from 0, the bend centred on 0, as there), `MorphCommand` (a plain 230-byte value:
the part and its new settings), `MorphEvent`, `applyCommand()` (a command into a `MorphPatch`) and `sameSettings()`
(patch equality without the name, which a test shows sees a change in every field). 13 unit tests, also under ASan and
UBSan, no warnings with the device GCC 9.4. The script of "Cutting Lead" as AbacDsp has it (with `SetVolume(-4)` for
the level that was a field of its file) ends in exactly the compiled `c_factoryPatches[3]`; the other nine follow in
step 5. Deviations: `SetVolume` is new (the patch level lives in the script, not in a file field); the filter type
is looked up in our table of mixes: AbacDsp's short names LP1 to LP4, HP1 to HP4, BP2, BP4, AP1 to AP4 and Notch are
aliases of rows of it, every row can also be named exactly, any other name (e.g. AbacDsp's "HP2 + Notch A") is rejected
with a line in the log and the API description lists the names; `SetCtrlSlot` depth is limited to -100 to 100 (AbacDsp
has no limit); hooks for aftertouch and poly pressure are not offered (decision 3).

Measured in step 5 (Mac; the device is step 6): `plugins/morphexsynth/` runs scripts. The ten factory patches are the files
`scripts/<name>.lua` (extracted from AbacDsp's JSON, `SetVolume` added), run once in `setSampleRate()` and kept as `MorphPatch` values: the
Patch port copies one at once in the audio thread. A user script (Script port 1 to 8, `user-<n>.lua` in
`$JP_SCRIPT_DIR/morphexsynth`, `~/Library/Application Support/jaypacks/scripts/morphexsynth` or
`/data/jaypacks/scripts/morphexsynth`) lives in a `ScriptHost`: loaded in the worker (or at once in `reset()`, which is
not real-time), reloaded when the file changes (checked twice a second), its hooks fed with the MIDI the zone lets through and only for the
hooks it defines. What the top level and `OnStart` call is taken over as one patch from a blank one (the host's
`readyGeneration()` says it is all queued), so a script's sound does not depend on what played before; later commands are
applied one by one. **Golden test:** all ten scripts give exactly the compiled patches (a mutation of one value fails
it); the compiled table moved to `tests/unit/Plugins/MorphexsynthFactoryPatches.h` as the oracle and left the plugin.
A user script that is a copy of a factory script sounds bit for bit like the factory patch (24000 samples, two
notes). 22 new pedal and host tests (take-over, release of the notes, a broken script keeps the sound, hot reload by
the next check, hooks acting after the worker ran and waking it only when defined, zone gating, `SetMpeZone` from a
script, the Patch port waiting, twelve held notes under a reload, the bundle's contents, the worker thread); the host
got `deselect()`, `wantsHook()`, `readyGeneration()`, `peekCommand()`, serialised `work()` and folder creation for the
log; 802 tests pass, also under ASan and UBSan, the script, queue and worker-thread tests under ThreadSanitizer.
Adapter: `setBundlePath(const char*)` (optional on a pedal) gives the bundle folder before `setSampleRate()`. The plugin
costs 508 kB of binary (Lua inside). Changes to the plugin: version 0.1.1, JSON `worker`, port `Script` and output
`Script status`; the bundle has `scripts/` with the ten factory scripts, `API.md` and `constants.lua` (written by
`plugins/morphexsynth/ScriptDoc.cpp` from the binding table, built with the Mac bundle; `jp.py build-dwarf` builds that
target too and assembles the device bundle from it). Deviations: factory patches are values, not live scripts (instant
and independent of the worker thread; a hook in a factory script would not run: copy it to a user slot); the
first note after a reload can sound with the previous hook settings for 3 to 5 ms (the hook runs in the worker).

Measured in step 6 (Dwarf, 2026-10-17): the Morphexsynth device test passes all 143 checks (the 122 of before and the
script checks): level and frequency against the Mac within the old tolerances (Retro Poly A3 -26.024 dB on the device
against -26.023 dB on the Mac), the bundle on the device has the ten factory scripts, `API.md` and `constants.lua`, the
status output is 1 with the Patch menu and with a user script, a user script written over ssh to `user-8.lua`
sounds as it says (-6 dB script 4.7 dB under the factory Retro Poly, and +12.000 dB after the file is edited to +6 dB and
reloaded within 1.2 s), a file with a syntax error gives status 3 and the log `state: compile error ... user-8:2:`
while the sound stays, a fixed file loads again, a hook (`OnNoteOn` turning the patch down by 34 dB) acts (-58.7 dB), a
removed file gives status 2, Script = 0 brings the Patch menu back at the same level (within 0.3 dB). The test
saves and restores a `user-8.lua` that was there. No xruns while processing; the first run had one, at the moment a USB
MIDI device was removed from the Dwarf (`remove_hw_port` in the same second). `perf`: idle 7.1 points of mod-ui's DSP load
(7.0 before), one note 10.9, four 17.9, twelve 38.5 to 42.0 depending on the patch (39.6 for Retro Poly, 40.6 before); the plugin
allocates 2467 kB (2345 kB before: the script host, ten patch values, the Lua state of the factory run is freed) and the binary is
496 kB with Lua. The expected-values program of the device test had been silent without the factory script folder (the
device test found it: -234 dB expected against the sound it heard); it now sets the folder. `tools/jp.py push-script
<plugin> <name> <file>` copies a script to `/data/jaypacks/scripts/<plugin>/<name>.lua` (ASCII only, names checked) and
`script-log` prints the log the plugin wrote next to it. The user's loop is described in `docs/scripting.md` and
tried by the test with ssh only. Not tested: `/data` over a MOD OS update or a reboot (the data partition is separate).

Measured in step 7 (Mac, units only; no plugin uses the pieces yet): 45 new unit tests (847 in all), clean under ASan and
UBSan and, for the host and the chain test, under ThreadSanitizer; the new headers compile with the device GCC 9.4
(`-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror`). The chain test (a simulated plugin with the clock, the host
with its worker and the scheduler) shows an arpeggio from `OnTick` on exactly the samples start + 6000 i (note on) and
+3000 (note off) for blocks of 16, 64, 128, 513, 1024 and 4096 with a lookahead of two blocks plus 256 samples, no late
message; a repeating timer set in `OnTransportStart` fires on start + 480 k for blocks of 32 to 4096. Checked by
mutation: commands scheduled at "now" instead of the event's time fail the exact-sample tests, no panic on a new
generation fails the reload test, and not dropping the commands of a script that failed leaves two notes hanging (the
chain test found that one; the rule is in `docs/scripting.md`). Found on the way: `repeat` is a Lua keyword, so a field
of that name is a syntax error in a script; `checkBindings()` now rejects such names in every plugin's tables. Not
measured: the real round trip on the Dwarf (step 9 with the MIDI Script plugin; the probe gave one block plus up to 1.3
ms jitter); the constant latency of the plugin is chosen there.

Measured in step 8 (Mac only): `plugins/midiscript/` is a MIDI processor (MIDI in and out, no audio ports, worker, 6 control
inputs, 4 control outputs), 889 tests in all (42 new in the unit tests and the host tests), clean under ASan, UBSan and (the
worker thread test) TSan, and the device GCC 9.4 builds the bundle (`jp.py build-dwarf midiscript`) without a warning.
The pedal drives the core as `docs/scripting.md` describes it; every shipped script is checked against an exact list of
messages with their samples (the arpeggio lands on 2 L + 6000 i for blocks of 16 to 1024 frames, the passthrough returns
every message of a 16-channel stream byte for byte one latency later, the quantiser moves all 128 notes, the note offs
follow), and mutations (no latency, commands at "now", no panic on a new script, a failed script still applied) fail the
tests. For this plugin the generator got a `designation` key (`time:beatsPerMinute`, `time:speed`), the host harness runs a
plugin without audio output and compares the MIDI output across block sizes for every plugin that has one (the
generic test uses blocks up to 256 for midiscript: its default latency of 12 ms covers two blocks of 256 frames, a block
of 4096 makes messages late, as documented), and `scripting/ScriptDirectory.h` took over the user folder from Morphexsynth.
The Dwarf is step 9 below.

Measured in step 9 (Dwarf, 2026-10-18, MOD OS 1.13.5.3315, 128 frames at 48 kHz): `tools/jp.py test-device midiscript` (72
checks) cables the board's MIDI input to midiscript and its MIDI output to the probe, plays MIDI like a USB controller and
reads the probe's counters over the websocket while the sender runs, so every message has the time at which the host
reported it. Passthrough: 16 note ons and offs on 16 channels, a burst of 500 messages 1 ms apart and 200 note pairs all
arrive, nothing late, nothing dropped. **The round trip of the worker:** with 200 note ons and 200 offs 4 ms apart the
messages that came late were 400 of 400 at a latency of 0 ms, 13 to 19 at 2 ms and none at 4, 6, 8 and 12 ms, so the
default of 12 ms has three times the margin (one block is 2.7 ms). The arpeggiator on the board's tempo (`transport-bpm`
120, 90 and 60 over the websocket; mod-host fills the port with the designation `time:beatsPerMinute`): the pitches go
60, 64, 67 round and round without a miss, the mean time between steps is 125.2, 166.2 and 249.9 ms against 125, 166.7 and
250 (the host reports in steps of about 12 ms, which averages out over 25 to 48 steps), every note on has its note off,
late 0, dropped 0, sounding 0 afterwards. Sync = Transport: no message while the transport is stopped, 49 in 2.5 s of
rolling (`transport-rolling 1`; the port with the designation `time:speed` follows it), none in the 2 s after it
stopped. User scripts written over ssh: loaded, an edited file reloaded within 1.2 s with its three sounding notes ended
(3 note offs), a broken file gives status 3 and the log names `user-8:<line>:` while the previous script keeps playing, a
panic sends the note offs and All Notes Off on 16 channels (19 messages for 3 notes), a hook that fails gives status 4,
ends the note that was on and the call says nothing, a removed file gives status 2. `perf` (probe-less graph, 3 cycles):
idle 0.6 points of mod-ui's DSP load, the arpeggiator with 3 or 8 keys held 3.4 and 3.3 (a script with `OnTick` wakes the
worker for every tick: 48 a second at 120 BPM), Chord Player 0.9, Scale Quantiser 0.5, Octave Doubler 0.9; the plugin
allocates 50 kB, the binary is 363 kB, no xruns in the windows. Xruns: a steady arpeggio of 90 s gave none three times
(arpeggio, passthrough and an empty graph alike), but 2 of 5 full runs of the device test had one xrun at a time when the
sender and a steady arpeggio ran, and 3 runs had none; they were not tied to a step the log could name, so they are
not attributed to the plugin yet (the test names the step of any xrun). While finding the test's own mistakes the core got
`ScriptHost::hookFailures()`: a failed hook is now a panic and the script goes on, a file that does not load changes
nothing (the first version silenced the script whenever `state()` was not Ok, which also silenced it while a broken
file was waiting to be fixed; the device test found it).

Measured in step 10 (Mac, then the Dwarf 2026-10-18): `WaveTableOscillator` has three slots (`setWaveset()`, slot 0 is
`setWave()`) and a position `setMorph()` in 0 to 1 that moves in 10 ms per sample; `MorphexsynthVoice::setWaveMorph()`,
`WaveMorphPatch` (second, third, position; a field of `MorphPatch` of its own, so `SetOscillator` does not undo it) and the
command `SetWaveMorph` (`API.md` follows from the binding table). 905 tests (14 new: 10 oscillator, 1 voice, 2 script, 2
pedal), clean under ASan and UBSan; mutations (no finish of a move at the note start, the two weights swapped, noise by
weight ignored) fail the tests. Position 0 is bit identical to an oscillator that never morphs (also in the voice and in
the pedal, with the factory patches: the compiled-patch oracle is unchanged), a slot position reads that slot alone through the
same loop, the linear mix of two slots is exact to 2e-6, the inharmonic power of morphs of saw, square and shark fin from 55
Hz to 11 kHz is at most -64.3 dB (plain: -67.2 dB), the pitch is unchanged to 0.02 cent, a sweep has no step larger than that
of the plain waves. A note starts in the configured position: the move that `loadPatch` begins while the voices are silent
is finished at the trigger (without that a note started 10 ms into a move; found by the pedal test). Device test
(`test_morphexsynth.py`, 151 checks): the level of morph 0 equals the plain saw (-24.74 dB) and morph 1 to a triangle equals the
plain triangle (-24.16 dB); 8 notes of 3 oscillators cost 41.7 points of DSP load plain, 42.2 with the morph on a slot, 46.3
with all 24 oscillators crossfading. Morphexsynth `perf` before and after: A/B with the morph paths compiled out gave the same
figures (12 notes of Retro Poly 44.5 and 44.5 points, Section Brass 47.5 and 47.5), so the morph costs nothing while it is not
used; the figures of this day are about 4 points above those of 2026-10-17 (idle 7.2 against 7.0, 12 notes of Retro Poly 44.9
against 40.6) with the same code in the unmorphed path, so the device itself has drifted. The chord phase of the CPU step of
the device test (12 notes of Retro Poly with the meter in the graph, 63 % DSP load) gave xruns in two runs (outside the window
that the test judges, which showed none in this run); the test names the step of any xrun.

## Morphexsynth attack and release

Two controls on the pedal face that offset the amplitude envelope of the loaded patch or script (plan of 2026-10-18,
`.claude/done-morphexsynth-attack-release.md`). Decisions of the author: milliseconds as offsets (a 0 ms attack can be
lengthened), amplitude envelope only, Chorus and Phaser leave the face (still settings) because the face holds at most
six knobs.

| Step | Content | Status |
|---|---|---|
| 1 | Ports `attack` (-500 to 2000 ms) and `release` (-1000 to 3000 ms), `Engine::applyEnvelope()` (a script's `SetAmpEnvelope` is offset too, at least 1 ms remain), unit and host tests, device check, plugin page, snapshot | done |

Measured in step 1 (Mac, Dwarf 2026-10-18): 909 tests, clean under ASan and UBSan, 0 warnings. Warm Bass (attack 2 ms): the
level of the first 40 ms falls from 0.0669 to 0.0081 with +300 ms; Airy Pad (attack 800 ms) rises from 0.0024 to 0.0182
with -700 ms; the tail 60 to 100 ms after a note off (release 120 ms) is 0.0066, with +2000 ms 0.0376 and with -110 ms 0.000008;
both offsets at 0 are bit identical to a pedal that never had them. The generic host test cannot show Release (its notes are
never released), so it is exempt there and `Morphexsynth_test.cpp` checks it through the bundle. Device test
(`test_morphexsynth.py`, 162 checks): a Warm Bass note released 1.5 s ago is silent (-240 dB) with the patch's release and
sounds at -48 dB with +3000 ms; every new control at both extremes gives no non-finite or denormal sample. `perf` is
unchanged (12 notes of Section Brass 48.1 points against 47.5). One of three device runs had an xrun in the step 'wavetable
morph' (the known sporadic xruns of the Dwarf; the two other runs had none while processing).

## Morphexsynth expressive patches

Every factory patch answers pitch bend, pressure and velocity (plan of 2026-10-18,
`.claude/done-morphexsynth-patch-expressiveness.md`; the author's observation was that pressure was weak, not all patches
bent and the velocity did nothing audible).

| Step | Content | Status |
|---|---|---|
| 1 | Voice default velocity range 0.15 (12 dB from velocity 30 to 127, was 2 dB); in the ten scripts and in the oracle table: a pitch bend slot in the four patches that had none, a Z slot in the six that had none, a velocity slot in nine (bipolar around 64: the sound at velocity 64 is as before); tests `MorphexsynthPedal.EveryPatch*` (pressure, velocity, bend per patch), a voice test of the default, a device check of a soft and a hard note; the patches page rewritten | done |
| 2 | A voice that is silent starts its oscillators and its LFO at phase 0 (`MorphexsynthVoice::triggerVoice()`); a voice that still sounds keeps its phases (a steal does not jump) | done |

Measured in step 1 (Mac, Dwarf 2026-10-18): 913 tests, clean under ASan and UBSan, 0 warnings; device test 174 checks passed
(velocity 40 and 127 of Warm Bass within 0.01 dB of the Mac); `perf` unchanged (12 notes of Section Brass 47.8 points against
48.1; 49.2 after step 2, within the 47.5 to 49.2 that the runs of the day gave for the same per-sample code: step 2 adds a few
operations per note start only); device test with step 2: 174 checks passed, no xrun while processing. Per patch (the tables are on `docs/plugins/morphexsynth-patches.md`): pressure 0 to 127 raises the energy above 1 or
3 kHz by 7 to 52 dB (before: 0 in six patches), velocity 127 against 40 gives 10 to 18 dB of level and a brighter sound,
the full bend moves the spectral centroid by x1.4 to x2.0 up and x0.5 to x0.74 down in all ten patches. Findings on the way:
the bright patches (Cutting Lead, Grand Stage) have their cutoff open already, so their pressure shows above 3 kHz only (the
tests take the better of the bands above 1 and 3 kHz); Crystal Bells sends Z to the resonance, because the filter envelope has
opened the cutoff on the strike; the new velocity slots opened the cutoff at the tests' velocity 100 and broke the pressure
tests until the slots became bipolar (a patch at velocity 64 sounds as before).

Found on the author's report (2026-10-18): the same note played again while it still sounds had a level that depended on the
voice it landed in, because an idle voice kept the oscillator and LFO phases where it stopped and two notes of one pitch added
or cancelled at random (Retro Poly, second note after 150 ms, over the voice histories 0 to 10 used voices: a spread of 3.1 dB at
the default release and 5.7 dB with Release +2000 ms). Measured after step 2: 0.05 dB and 0.27 dB. Tests: the pedal test of
that scenario (it fails without the reset), a voice test that a fresh and a used silent voice agree from 20 ms on (a lead of 4
samples, about 4 degrees at middle C, remains), a voice test that a retriggered sounding voice equals an undisturbed one (it
fails when a sounding voice is reset). Not examined: in the first 16 samples a fresh voice ramps up from silence while a voice
that played before starts higher (a difference of 0.24 of an amplitude of 0.33 in the first block, gone after 20 ms).

## Ship Ahoy scripting

Lua control of the Ship Ahoy filter frequency (plan of 2026-10-22): sequenced targets with
glide from a script (`OnTimer`, `SetTimer`, tempo clock of the scripting core), continuous OU modulation added to the
base trajectory, and monophonic YIN pitch detection that feeds an `OnNewPitch` hook and a five-note memory. One
filter, the host stays at 48 kHz, nothing of Ship Ahoy was in production (no golden master, no migration). Decisions
taken with the plan: the script owns the frequency while one is selected (until the script-driven rework below: then the knobs of the hopper were removed and the first factory script became the classic sound); global commands with a field table and numeric hooks; `SetCutoff` was refused inside `OnNewPitch` (allowed since the script-driven rework); a
clock lead like MIDI Script's (a setting at first, a derived value since the fixed-lead plan below); where YIN runs is decided by a benchmark on the Dwarf. Timing (step 2, the author's
model): steps per second = tempo / 60 x steps per beat x speed ratio, with the Tempo knob (BPM, no designation: mod-ui drives a designated port from the board's tempo, which overrode the
knob on the Dwarf) in free run or the board's tempo (port Board tempo) with Sync = Transport, one
global Step setting from 8/1 to 1/64 and a script's `SpeedRatio` (0.1 to 10); the knob generator hopped on the same steps (until the rework below).
A failing hook or a reload holds the frequency; `SetCutoff` glides default to 5 % of the step.

| Step | Content | Status |
|---|---|---|
| 1 | `dsp/Generators/CutoffTrajectory.h` (linear glide, retarget from the current value, exact endpoint) with tests; the knob generator of the pedal runs on it | done |
| 2 | Scripting on Ship Ahoy: `ShipAhoyEngine` (the DSP, hops and script mode) and `ShipAhoyPedal` (clock, steps, worker, ports), `ShipAhoyScript.h` (`OnStep`, `SetCutoff`, `SpeedRatio`), Tempo/Step/Sync/Script ports, 96 ticks per quarter and a tempo clock from 1 to 3000 BPM, factory scripts (sequence, random hold, alternating), tests, device test, perf on the Dwarf | done |
| 3 | Continuous modulation: additive OU (`setModDepth`, `setModRate`, ports Drift depth and Drift rate, charts in `documentation/ShipAhoy/`) | done |
| 4 | `dsp/Analysis/YinPitchDetector.h` (YIN port, optional decimation), `PitchNoteTracker.h`, `frequencyToNote()`, tests, `documentation/YinPitch/` with the benchmark on the Dwarf; decision D2: the detector runs on the audio thread, decimated by 4, hop 50 Hz | done |
| 5 | Played notes into scripts: the pedal runs the detector and the tracker while the script defines `OnNewPitch`/`OnPitchEnd`, `notes.lua`, factory scripts `follow`, `answer`, `echo`, preset "Follows playing", the Script port gets three entries (the user slots move from 4 to 7), device check with siggen as the player | done |
| 6 | Workflows and docs: editor `apis/shipahoy/api.json` regenerated (pitch hooks, `notes` library), editor tests, `API.md` checked in the built bundle, plugin page, README | done |

Measured in step 2: 957 unit and host tests (the plugin's steps land on the same samples for blocks of 1, 7, 16, 128,
256 and 4096, glides of 0.25, 0.5 and 1 of the step end on the target at the predicted sample, `SpeedRatio` 0.5, 2 and 10
change the step interval to 6000, 1500 and 300 samples at 240 BPM, 60 BPM at 1/8 steps at 2 Hz), clean under ASan and
UBSan, no warnings, GCC 9.4 build; the hop positions read from the audio match a reference engine driven on the clock
instants within 1e-4; a user script holding A5 matches the knobs at 880 Hz within 1e-3 through the binary; on the Dwarf a
user script written over ssh gives status 1, sounds like the knobs within 0.2 dB, a broken edit keeps it (status 3), a
removed file gives status 2. CPU on the Dwarf (points of mod-ui's DSP load, 3 cycles, rerun 2026-10-22 on the final code): 1.6, 4.2 and 1.8 for the defaults and
the presets Slow drift and Toggle trill, against 0.8, 3.0 and 1.0 measured the same day with the 0.1.0 code (the clock, the
step events and the script host cost about 0.5 to 0.7 points; the first version of `process()` copied the due events into
an array and measured 2.0, 4.3 and 1.9); Jays Twin, which did not change, read 1.7 to 2.2 that day after 0.5 to 0.7 in
its own table, so the device's baseline moves from day to day and only same-day figures compare. The plugin allocates
52 kB when created (script host, queues) instead of 0.6 kB and the binary is 367 kB (Lua) instead of 18 kB; nothing is
allocated while it runs.

Measured in step 3: 969 unit and host tests green (depth 0 gives the same output whatever the rate; depth 3 at 5 Hz over
60 s measures standard deviation 3.08 semitones; the autocorrelation at the lag 1 / (2 pi fc) is 0.400, 0.384 and 0.351 for
0.5, 2 and 8 Hz against 0.368; blocks of 1 to 4096 give the same output; the plugin matches the engine within 1e-3),
clean under ASan, no warnings, GCC 9.4 build. CPU on the Dwarf (3 cycles, 2026-10-22): 1.8, 4.7 and 2.0 for the defaults
and the presets Slow drift (now with drift 1.5 semitones at 0.5 Hz) and Toggle trill, against 1.6, 4.2 and 1.8 before the
step: the drift costs about 0.5 points while it runs and nothing at depth 0 (a first version that advanced it per sample
even at depth 0 read 2.6, 5.2 and 2.7). Device test (after a mod-ui restart: it had stopped sending output values, so every meter measurement failed): all checks
pass, including the new drift check (a 1.5 kHz sine on the slope of a lowpass: depth 6 at 5 Hz moves the level by 1 dB,
depth 12 at 20 Hz stays finite, depth 0 gives the fixed filter's level back exactly).

Measured in step 4: 1004 unit tests (the detector and the tracker as listed in `documentation/YinPitch/README.md`; the
tracker checked by mutation of six rules). A sine over 80 Hz to 1 kHz is read within 0.34 cent at full rate, 1.4 cent
decimated by 2 and 5.85 cent decimated by 4 (987 Hz; the plan's budget was 5 cent, it holds up to about 950 Hz); a step of
pitch is complete in the readings 40 ms after the step and a note 80 ms after it; a vibrato of +-30 cent reads as -29.0 to
+29.4 cent. Findings against the source: its cumulative sum started with a constant that made quiet noise look periodic
(fixed), the window of its gate and of its difference function differed (one window now, stamped with its centre), the lag of
the lowest frequency lay outside the range (fixed), and broadband noise of +-0.5 on a sine of amplitude 1 made 76 % of the
full rate readings wrong while decimation by 4 had none (the lowpass removes the noise). On the Dwarf the detector as
published costs 1.8 ms per block of 128 frames on average and 14 ms in the block with a reading (the period is 2.67 ms); with
linear indexing and early exit 144 us and 1.3 ms; decimated by 2 45 us and 0.5 ms; decimated by 4 19 us (0.7 % of the period)
and 0.14 ms (5 %) at a hop rate of 50 Hz, which meets the criteria (worst block at most 10 %, mean at most 1 point). Time
slicing was built, measured and removed (not needed).

Measured in step 5: 1015 unit and host tests green (a phrase of two notes, B3 and F4, reaches `OnNewPitch` as 59.0015 and
65.0377 and about 80 ms after the onsets (a first measurement through a timer: 92 and 72 ms with the 11 ms of timer and
latency in it), `OnPitchEnd` 60 to 70 ms after the sound stops, white noise of amplitude 0.5 and silence give no note,
`SetCutoff` inside the hooks is refused with a warning and the refusal ends with the hook, also after a hook that fails,
the detector runs only while a pitch hook is defined, the output is the same for blocks of 16, 128 and 512, `notes.lua`
is checked inside Lua, `follow`, `answer` and `echo` follow the notes as an oracle computed in the test says; the refusal
flag, its reset and the "listen only with a hook" switch were checked by mutation), clean under ASan, no warnings, GCC 9.4
build. Finding on the way: resetting the detector when a script was taken over, after the first block's samples had gone
in, made the reading instants depend on the block size (the first test of block independence failed); the reset is now
at the start of the block, before its samples. On the Dwarf (3 cycles, 2026-10-22): 1.6, 4.6 and 2.0 for the defaults and
the presets Slow drift and Toggle trill, 4.0 for the new preset "Follows playing" (white noise in, the detector's worst
case: no early exit); the same preset with the script `sequence`, which has no pitch hook, read 3.5 against 1.7 for the
defaults the same day (a script glides the filter 70 % of the time, the knob mode retunes only at hops), so the detector,
the tracker and the note memory cost about 1 point, consistent with the 0.7 of the benchmark. The plugin allocates 62 kB
(52 kB before: the detector's buffers) and nothing while it runs. Device test: all checks pass, including siggen playing
an A5 and then an A4 into a script that sends the filter to the note (within 0.03 dB of the knobs at the same
frequency). Deviations from the plan: the hook lives in `ShipAhoyScript.h`, not in a `scripting/PitchHook.h` (one plugin
uses it; extract when a second does), the end of a note is a separate hook `OnPitchEnd(note)` instead of `note = 0`, no
`scales.lua` copy (the scripts need only pitch classes), and no new chart: the detector and the tracker are charted in
`documentation/YinPitch/`, the wiring adds no algorithm.

Measured in step 6: `lua-editor/apis/shipahoy/api.json` was out of date (no pitch hooks, no library) and is regenerated:
eight hooks, the `notes` library with its twelve members, the four new scripts in `sources`. The bundle's `API.md`
already carried both pitch hooks, the five fields and the refusal rule, so no doc string changed and no C++ with it.
Editor tests: 274 (a new file test for the hooks and the library, a completion test, the factory scripts check
clean, the factory script count is 26). No code changed in step 6; the device test and perf were rerun as the closing
check of the rework. Settled by judgement, to be corrected after hands-on use and a live performance: the tracker
thresholds stay constants, the lowest note stays 80 Hz, no editor lint for commands inside the pitch hooks.

### Ship Ahoy: fixed clock lead

The Latency port is removed (plan of 2026-10-23). It was the round trip of the worker, which depends on the block size
only, not a taste setting. The lead of the clock is now `c_leadBlocks` (2) times the largest block the pedal has been
called with since the activation (`ShipAhoyPedal::noteBlock`; `setExpectedBlock` is a floor for tests and hosts that
know their block size). Finding on the way: the pitch hooks could not command the filter then, so the first idea of the
plan (commands answering played notes without the lead) had nothing to act on; it was done in the script-driven rework
below, where a command of `OnNewPitch`, `OnPitchEnd` or `OnControl` is applied at the first block boundary after it
arrives, without the lead.

| Step | Content | Status |
|---|---|---|
| 1 | Port removed; lead derived from the largest block; tests (lead follows the block, nothing late for blocks of 16 to 1024, late counted with a slow worker, steps at their samples for blocks of 1 to 4096 with a fixed lead); generic block-size test skips shipahoy; docs | done |
| 2 | Dwarf: build, device test (new check: nothing late, nothing dropped after a script ran), perf; the snapshot did not change | done |

Measured in step 2 (2026-10-23): 1017 unit and host tests green (ASan green for the Ship Ahoy tests), no warnings, GCC 9.4
build; on the Dwarf (128 frames, lead 256 samples = 5.3 ms) a script runs with `Late` 0 and `Dropped` 0; a lead of one
block was not measured (the factor 2 stays until a reason to try less). CPU (3 cycles): 2.0, 5.0, 2.1 and 4.5 for the
defaults and the presets Slow drift, Toggle trill and Follows playing, against 1.6, 4.6, 2.0 and 4.0 of the day before:
the change removed a port and one comparison per block, so the difference is the device's baseline moving (only same-day
figures compare), not measured against the old code. The first device run after a deploy once read 5.08 points added
against the limit of 5; two reruns read 3.1 and 3.3.

### Ship Ahoy: script driven

OpenSpec change `ship-ahoy-script-driven` (2026-10-23, `openspec/changes/ship-ahoy-script-driven/`). Ship Ahoy had
sixteen controls and a script selector with a "None" entry: a patch and a script both claimed to define the sound. Now
the script is the engine and the face has five knobs (Mix, Tempo, Script, X, Y, three over two); X and Y have no
meaning of their own, each script says in its header what they do (`-- X:`, `-- Y:`; the table in
`docs/plugins/shipahoy.md` is generated from them). Ports removed: Glide up/down, Random, Resonance, Frequency, Range,
Type, Drift depth/rate (Latency went before). The built-in hopper became the first factory script `hop.lua` (X glide,
Y resonance, defaults 5 and 85 reproduce the old default); random types, "Slow drift" and "Toggle trill" became scripts
(`slow-drift.lua` brown, `toggle-trill.lua` toggle; violet and gauss are not shipped). New script commands
`SetResonance`, `SetFilterType` (with the `FilterType` constants), `SetDrift`; no glide setter, because `SetCutoff` has
its own `glide` and a script knows the direction of its hop. Scripts read X and Y with `GetX()`, `GetY()` and are told
by `OnControl(x, y)`. The scripting core got value getters for this (`GetterSpec`, `ScriptHost::setValue`), usable by
every plugin (MorphExSynth later). Commands of the hooks of external events (`OnNewPitch`, `OnPitchEnd`, `OnControl`)
and of a script's load are applied at the first block boundary after they arrive, without the clock lead; the pitch
hooks no longer refuse commands. Version 0.3.0; saved pedalboards and presets of 0.2.x may load with defaults. The
engine lost the hop, the range, the random types and the knob mode; the hopper survives as the reference
implementation `tests/unit/ShipAhoyClockedEngine.h` for the charts of `documentation/ShipAhoy/` and the tests of the
algorithms. The move was checked once against the old engine path: 20 hops, the same frequencies within 0.2 %.

Measured (2026-10-23): 1026 unit and host tests green, ASan green for the Ship Ahoy and scripting tests, no warnings; the
device test passes (new checks: every factory script, X and Y, GetX and OnControl, commands of a hook applied at once, a
script that holds A5 against one that steps to it, nothing late and nothing dropped). CPU on the Dwarf (3 cycles): 1.7,
4.4, 2.0 and 4.0 points for the defaults and the presets Slow drift, Toggle trill and Follows playing, against 2.0, 5.0,
2.1 and 4.5 of 0.2.0 (the Lua hop replaced the C++ hop, and the engine lost its knob smoothers; the device baseline also
moves from day to day). Memory 71 kB against 62 kB: the first script now always runs, so its Lua state (about 9 kB) exists
from the creation of the plugin; nothing is allocated in `run()`, nothing is left after `cleanup()`.

| Step | Content | Status |
|---|---|---|
| 1 | Interface settled against the code (design.md), port list and presets in `shipahoy.json`, version 0.3.0 | done |
| 2 | Core: value getters in the scripting layer; Ship Ahoy: setters, `GetX`, `GetY`, `OnControl`, immediate external commands | done |
| 3 | `hop.lua`, `slow-drift.lua`, `toggle-trill.lua`; hopper, knob mode and old ports removed; engine and pedal tests, host tests | done |
| 4 | X and Y lines in the headers, generated table (`tools/script_headers.py`) and its test | done |
| 5 | Documentation, snapshot, README, charts | done |
| 6 | Device: build-dwarf (GCC 9.4), test-device, perf done; the author listens to the presets | open |

## References

Bring `docs/references.md` section 2 and the header briefs up to the rules of `CLAUDE.md` ("References"): durable sources,
archive links, traceable numbers, checked links. Planned, not started; the rules apply to new work already, this plan
retrofits the existing rows.

| Step | Content | Status |
|---|---|---|
| 1 | Audit: list every row of section 2 by durability (standard, paper, Wikipedia, university, manufacturer, personal site, blog) and every `dsp/` and `plugins/` header brief without a source; result as a table in this section | open |
| 2 | Link checker `tools/check_references.py` (standard library; HTTP status of every URL in `docs/references.md`, `documentation/*/README.md` and header briefs; non-200 listed, no network use in the unit tests, run by hand and named in the verification routine) | open |
| 3 | Archive snapshots (archive.org) for every non-durable row, the date of each check in the row; replace forum or blog sources by a paper or a wiki page where one exists | open |
| 4 | Keep the excerpts of datasheets and charts that targets were traced from in `documentation/<folder>/` where the licence allows (Jensen C12K sheet for Jays Twin first); the row says where it lives or why it does not | open |
| 5 | Header briefs without a source get one (from the audit), the table of section 2 is sorted by topic, the header sentence about the HTTP 200 check is replaced by the date column | open |

## Licence split

Code stays MIT; the pedal artwork and the brand names are reserved (plan of 2026-10-23). Artwork covered:
`generator/graphics.py`, `generator/style.py`, `generator/assets/*.png`, `generator/templates/modgui/icon.*`,
`documentation/Graphics/`, `docs/plugins/images/` (also the README images), later any logo. Not covered: the Inter font,
`third_party/`, the blueprints in `plugins/` (categories and ports, no design). Rule: building and using on your own
devices is allowed; forks under MIT must replace the artwork and not use the brand names (`jaypack`, `Krachpedal`,
`Krachpedale`); redistributing or selling built bundles with the artwork needs written permission. The generated TTL keeps
`doap:license MIT` (it describes the plugin code); `ARTWORK-LICENSE.txt` says the modgui folder of a bundle falls under
the artwork terms.

| Step | Content | Status |
|---|---|---|
| 1 | `ARTWORK-LICENSE.txt`, `BRANDING.md`, `LICENSE` exclusions note and "Jürgen", README licence section and layout row, references row, roadmap, `CLAUDE.md` rule | done |
| 2 | A logo, if the author adds one: its path into `ARTWORK-LICENSE.txt` and `BRANDING.md` | open |
| 3 | Optional: lawyer review of the custom artwork terms; trademark registration of the brand names | open |

## Next

- `jp.py`: warn when a bundle in `build-dwarf/bundles/` is older than the generator or its
  templates (siggen and meter went to the device with the modgui from before the drag-handle
  fix until Jays Twin step 3).
- Eigenghost: the author's listening test in Reaper (the bundle is installed on the Mac) and on
  the Dwarf (the test board is loaded, knob 1 wet, knob 2 decay, footswitch B bypass).
- A look at the new pedal faces in the device web UI (step 13: absolute positions, faces
  190 to 280 px wide); readability of labels at mod-ui's 100 % zoom (pedals at half size).
- Publishing on krachpedale.de: `HOMEPAGE` and possibly `URI_BASE` (a URI change breaks
  saved pedalboards; decide before a release).
- `lua-editor/`: a desktop editor for the plugins' Lua scripts (PySide6, ssh to the Dwarf; `lua-editor/README.md`,
  `PLAN.md`). The tree has `repo/` (the plugins' `scripts/`, editable) and `device/` (ssh, connected on first use);
  push to a user slot, pull, remove from device, push on save, an Activity list and explained errors, auto indent, F1 opens
  the plugin's API as an HTML page in the browser (tested on the Dwarf at the file level; the author's try-out of the whole loop is open). The page also shows each plugin's examples and complete factory scripts, with "Used in" links, and Help > Copy API brief for an LLM builds one Markdown text (guide, API, examples). Open: packaging (PLAN.md
  milestone 8) and the DoD items (references).
- More plugins; each gets its page with techniques, a performance measurement and, when it
  has a pedal face, a snapshot.
- Morphexsynth round robin: instead of the same start phase for every note (step 2 above), the start phase of the oscillators
  could be spread in a controlled range, for example +-30 degrees, taking the voices in turn (or the notes of one pitch in
  turn), so that repeated notes of one pitch differ slightly and add up less coherently, the way a sampler's round robin does;
  the range would be a setting (0 degrees is the behaviour of step 2).
- Lua scripting examples and AI guide: done for the three scripted plugins (`plugins/<name>/scripts/examples/`, seven each,
  and `AGENTS.md`, shipped in the bundles; checked by `lua-editor/tests/test_examples.py` and by a unit test per plugin).
  Open: one example per plugin tried and listened to on the Dwarf, and the practical test of the guide (an assistant
  given `AGENTS.md`, `API.md` and the examples writes a small script per plugin, `<name>.log` clean).
- Lua scripting: the plan (steps 1 to 10) is done. Candidates for later (`docs/outlook-lua.md`): a Script Shaper (a distortion curve that a script samples into a table at load; Lua computing arrays at load could also give custom wavetables), scripted macros on the existing effects, the morph position as a target of the routing slots (Y, Z, velocity), `OnTick` without a wake-up per tick when no key is held.
- A cross-built benchmark that times `run()` per plugin on the device (cycles per sample), to
  separate the DSP's cost from mod-ui's DSP load figure (`docs/device.md`).
- L2: the host tests against the Dwarf binary in the container.
- REAPER: scripted renders of test projects (ReaScript), if listening tests need repeating.
- Look up which versions mod-ui calls "stable" before a release.

Deviations from the original plan:

- Step 1 already contains `tests/unit/CMakeLists.txt` (planned for step 2)
  with one toolchain test, so the dev-scripts can be verified from the start.
- Step 2: the Ornstein-Uhlenbeck port uses the exact transition instead of
  the Euler step; the test helper `tests/unit/TestHelpers.h`
  (sine gain via a windowed single-bin DFT) was added; the headers were also
  compiled with the device GCC 9.4 in the `dwarf-new` image (no warnings with
  `-Wpedantic`).
- Step 3: the JSON keeps the old key names (so the old generator logic ports
  with few changes) but has no `transform` and no `smoothing` field (the plan
  had proposed a `smoothing` field). Defaults equal the "Ship
  Ahoy" preset. Beyond the seven listed fixes: resonance as a fraction
  of the self-oscillation threshold with a cutoff clamp, phase
  accumulator timing and glides that start where the cutoff is, toggle
  and gauss fixed. Added to `dsp/`: `poleMixingOscillationThreshold()`
  (exact) and `poleMixingTuningForPeak()` (closed form, per sample),
  `OrnsteinUhlenbeck::setValue()`. The JSON was checked against the pedal
  constants (defaults, menu labels) by a one-off script; step 5 makes this a
  test.
- Step 3, after review: the frequency knob sets the resonance peak, tuned in
  closed form (following the author's pointer to the AbacDsp
  corrections); the polynomial threshold fit was removed, the clamp moved
  from fs / 8 to fs / 6. `PoleMixingFilter::setPole()` added.
- Step 4: each folder has a `plots.sh` with only its render commands, and
  `dev-scripts/dev-docs-plots.sh` builds, creates the venv and runs them (AbacDsp
  has a self-contained `generate.sh` per folder). `PyConPlot.py` is copied
  unchanged, so the PNG size is kept down by smaller figures, not by a lower
  dpi (about 50 to 450 KB each). Charts not made: the float dead band of the
  one-pole smoother (covered by its unit test); the "before/after" of the menu
  mapping shows 4 of the 12 wrong entries. Added beyond the plan: Euler
  against the exact OU step, the threshold and peak tuning chart with
  AbacDsp's measured correction, glides against jumps, THD of the clamp, and a
  `summary.txt` per folder with the numbers the READMEs quote.

Findings that later steps must handle:

- Step 3 (handled): the old Regen range could never reach
  self-oscillation (r * a up to 3.4, threshold 4.05 at 100 Hz, 4.58 at 1 kHz).
- Step 5: the adapter calls `reset()` in `activate()` after the first port
  values are set, so smoothers start at the host values instead of gliding
  from the constructor defaults; setters receive port values unchanged;
a host test compares the JSON defaults, ranges and menu labels with
  `ShipAhoyPedal::c_default*` and `c_filterTypeLabels`; the generator emits the
  `presets` block as LV2 presets.
- Step 5: the seed per instance is the adapter's choice (constructor
  argument); the same seed gives the same hops on Mac and Dwarf up to the
  rounding of the Gaussian values.
- Step 5: with a silent input the filter states decay into denormal floats.
  The adapter should set flush-to-zero for the duration of `run()` (FPCR.FZ on
  aarch64, the Dwarf and Apple Silicon alike) instead of relying on the host.
- Step 4 charts: the threshold over the cutoff, the untuned resonance peak
  (1.07 kHz at 1 kHz, 3.7 kHz at 3 kHz, 9.8 kHz at 6 kHz, fs = 48 kHz) against
  the tuned one and AbacDsp's `warpCutoffForSampleRate` fit, the peak
  offset below the threshold (1 to 2 % at 95 %, 4 to 5.5 % at 85 %); shipahoy hop targets per
  random type (histograms, brown timeline); the before/after of the filter
  type mapping. Also: one-pole -3 dB drift (+1.3 % at fs/16, +5.6 % at fs/8, +34 % at
  fs/4), the self-oscillation threshold over the cutoff, the allpass error
  (about 1.2 dB per order at fc = 1 kHz), and the float dead band of one-pole
  smoothing are all worth a chart.
- Step 5: no `modgui.ttl` yet (moved to step 6). The `ports.h` template
  is `generator/templates/ports.h.in`, so editor formatters leave it alone. The
  deprecated `lv2plug.in/ns/lv2core/lv2.h` redirect is not vendored (nothing
  includes that path). Added: generator unit tests (`generator/test_jpgen.py`,
  run by ctest), `lv2/FlushDenormals.h` with tests, a generic test that every
  control reaches the plugin, and `docs/plugin-json.md` (planned for step 11,
  written now because the format is fixed here). `dev-scripts/dev-build.sh`
  and `dev-test.sh` now build everything (unit tests, plugins, host tests).
  The plugin also compiles with the device GCC 9.4 and flags (only
  `lv2_descriptor` exported, GLIBC 2.27 at most); the real Dwarf build is
  step 7.
- Step 6: the modgui text files come from `jpgen.py --modgui` (no separate
  template copy), the images from `generator/graphics.py`, both behind the
  CMake option `JP_GRAPHICS`. The charts' venv moved to the repository
  venv `.venv` with Pillow (`dev-scripts/dev-venv.sh`, `requirements.txt` at
  the root). Instead of reproducing the old label and screenshot positions,
  one layout function computes the knob positions of the old stylesheet and
  labels and screenshot follow it; the old ones did not match mod-ui.
  Shared images: only the knob strip and the footswitch (`gui/modgui/`) and
  the two jack images for the screenshot (`generator/assets/`). Added:
  `generator/test_graphics.py` (ctest `generator.graphics`). Step 13 replaced the
  layout, the shared images and the face.
- Step 7: the device bundle is assembled from the Mac build's generated files
  and the Dwarf `.so`, with an identity check of `ports.h` and the TTL;
  `tools/modui.py` has only the calls used on the device so far, the
  output-port readback moves to step 9. Added: `modui.py status`
  (read-only) and `tools/test_jp.py` (ctest `tools.unittest`). Not done: the
  optional L2 (host harness inside the container).
- Step 8: the integration tests are `tests/host/ShipAhoy_test.cpp` (the
  `<Name>_test.cpp` rule of the host tests) and judge the audio with the
  analysis blocks; the filter shapes are measured as H1 transfer functions
  against the theory instead of band energies, hop positions are read from
  the audio and compared with a pedal running alongside. The analysis
  blocks are `SignalStats`, `ToneFit`, `GoertzelBand`/`TransferBand` and
  `ZeroCrossingFrequency` with unit tests in
  `tests/unit/Analysis/SignalAnalysis_test.cpp`; `Lv2Host.h` can change controls
  between blocks. Not done: the optional L2 (no Dwarf binary in the
  container yet).
- Step 9 (own mini-plan):
  the plugins are named `probe`, `siggen` and `meter` (bundles
  `jaypacks-<name>.lv2`, display names "JP Probe", "JP Signal Gen", "JP Meter")
  instead of `jp-probe` etc., following the naming rule. The meter
  protocol uses a `measure` counter and a `resultId` output instead of a
  trigger port, a `reset` input and a `sequence` output. Bands are
  `TransferBand`s against a reference (level, gain, coherence) instead of
  band energies alone. siggen has no pink noise and no WAV player (left out
  with the author's approval of the mini-plan). Added beyond the plan: control
  outputs and source plugins in the generator, adapter and host tests,
  `SignalStats` non-finite detection by bit pattern and the GCC 9 constructor
  fix, `tools/device_tests/` with `probe_readback.py` and
  `test_plugins_check.py`, `tests/host/TestPlugins_test.cpp` (the device
  checks on the Mac), `documentation/Analysis/` charts. The author allowed
  free device use and keeping test graphs loaded.
- Step 10: the filter types are checked as gains against the transfer
  function at the eight meter bands (as in steps 8 and 9) instead of band
  energies; the expected values come from a small C++ program
  (`tests/host/ShipAhoyExpected.cpp`, target `shipahoy_expected`) so the
  theory exists only once. The zipper check jumps the mix between -100
  and 100 every 50 ms instead of sweeping it. The CPU ceiling is the increase
  of mod-ui's mean CPU figure when shipahoy joins the graph (5 percentage
  points). The shared parts of the device tests moved into
  `tools/device_tests/devicetest.py`; `test_plugins_check.py` uses it too.
  `jp.py test-device` has `--build`, `--no-deploy` and `--restore`. Added
  `modui.parse_hw_map()`.
- Step 11: written as planned: `architecture.md`, `building.md`,
  `testing.md`, `device.md`, `caveats-and-limits.md`, `plugins/shipahoy.md`,
  plus `docs/README.md` as the index of all docs and, at the author's request,
  `docs/working-with-claude-code.md` (how an LLM session continues the work,
  linked from `CLAUDE.md`). "How to add a plugin" went into
  `docs/plugin-json.md`. The shipahoy background cites its sources: the old
  JSON's "Oberheim Matrix" cannot be the 1976 unit (the Matrix came out in
  1985), the exact filter unit is not confirmed, so none is named. While
  checking facts, a note on the Dwarf compiler flags was corrected (the Dwarf flags include
  `-fno-finite-math-only`), and the notch positions of the notch and phaser
  types were computed for the filter type table. At the author's request the
  README got a Goals section (organic design for highly creative use, heavy
  LLM assistance, after AbacDsp's README, plus trust through measurement).

Findings that later steps must handle (continued):

- Step 6 checks (steps 8 and 10): the look of the installed bundle in the web UI
  (step 13 replaced the inline knob layout by absolute positions and the
  hand-drawn filmstrip by a generated one).
- Step 7 results: `tools/jp.py build-dwarf` builds shipahoy without
  warnings (needs GLIBC 2.27, GLIBCXX 3.4, CXXABI 1.3.9); deployed
  2026-10-15 with the author's go-ahead (`/root/.lv2/jaypacks-shipahoy.lv2`,
  next to the old `krachpedal-*` bundles). mod-ui reads all ports with units,
  ranges, logarithmic and enumeration properties, the three presets and the
  modgui (panel `2-knobs`, Mix and Speed). It shows version 0.1.0 as "1.0"
  with stability "testing" (odd minor version; the old gain plugin showed
  "experimental"); which versions mod-ui calls stable is to be looked up
  before the first release. Not yet done on the device: loading the plugin
  into a pedalboard (step 10).
- Step 5 check for the author: `tools/jp.py build-mac shipahoy --install`
  installed `~/Library/Audio/Plug-Ins/LV2/jaypacks-shipahoy.lv2`; loading it
  in Reaper is not scripted.
- L2 (optional, not done in step 7): GCC 9 needs `-lstdc++fs` for
  `std::filesystem` if the host tests are built in docker.
- Step 8 results (Mac, fs = 48 kHz, through the `.so`; printed by the
  tests): every filter type within 0.08 dB of its transfer function (probes
  63 Hz to 16 kHz where the theory is above -40 dB); at 99 % resonance an
  impulse rings 0.26 to 0.3 % below the frequency knob (299.2, 997.3 and
  3987.9 Hz for 300, 1000 and 4000 Hz); a jump of the mix between dry and
  wet adds nothing to the largest sample step (0.01440 against 0.01440 dry);
  with range 0 a sine stays a sine to numerical precision, with range 3 the
  residual is -10.7 dB; hop positions read from the audio match the pedal's
  targets within 2e-6 for all five random types; brown over 959 hops: mean
  0.464, deviation 0.186, lag-one correlation 0.889; speed 1, 7 and 16 Hz
  measured as 1.0001, 7.001 and 16.000 hops per second.
- Step 8 findings for the meter (step 9): a Hann block of 4096 samples is
  too short for probes at 63 and 125 Hz on steep responses (leakage lowers
  the coherence below 0.99); 16384 samples (2.9 Hz bins) work. A 1 kHz sine
  of 0.25 at 85 % resonance and a 1 kHz frequency drives the stages into
  their +-1 clamp (the resonance peak lies 5 % below the knob), so level
  tests near the peak need small signals.
- Step 9 (resolved): `GenericPlugin.ActivateClearsTheState` expected silence
  out for silence in, which a signal generator does not do; plugins without an
  audio input now skip it (and the in-place test), no JSON flag needed.
- Step 9 results: readback through mod-ui works (`output_set`, six decimals,
  about every 12.5 ms, 23 ms from `param_set` to the changed output).
  On the device siggen -> meter measured a 1 kHz sine at -20 dBFS as peak
  -20.002 dB, RMS -23.0103 dB, 1000.0 Hz, tone residual -153.6 dB, and white
  noise flat in all eight bands with coherence 1, no xruns
  (`docs/test-plugins.md`). On the Mac the meter measures shipahoy's 15
  filter shapes within 0.066 dB of the transfer functions
  (`TestPlugins_test.cpp`).
- Step 10 notes from step 9: set a plugin's ports, wait a little (filters,
  20 ms smoothing), then send a new `measure` value; use windows of 2 s and up
  for band results (16384-sample blocks; a window shorter than one block has
  no band result); read the results about 0.3 s after `resultId` changes. The
  mod-ui CPU figure varied between 10 and 16 % for the test graphs, so a CPU
  ceiling for shipahoy needs a baseline from the same run. The `probe` plugin
  stays as a smoke test of the readback path (the author's choice).
- Step 10 results (2026-10-15, `tools/jp.py test-device shipahoy --build`,
  284 checks, all passed; then three runs with `--no-deploy`, see the next
  item for the two that failed one check each): mix -100 and
  bypass on give exactly the reference (difference at the -240 dB floor);
  the 15 filter types match their transfer functions within 0.126 dB at the
  bands where the theory is above -40 dB (coherence at least 0.98, "notch
  octaves" at 500 Hz 0.994); 41 mix jumps in a 2 s window leave the largest
  sample step at -42.83 dB against -42.81 dB dry and -43.28 dB wet; hops with
  every random type, every control at its minimum and at its maximum, and
  silence at resonance 100 give no non-finite and no denormal samples (peak
  +3.8 dBFS with every control at its maximum); shipahoy adds 2.0 to 2.5
  points to mod-ui's CPU figure (10.6 to 12.6 %); knob 1 -> mix and footswitch
  B -> bypass show up as `hw_map`; no xruns; the jack2 journal had no new
  lines (the cursor query was checked on older entries).
- Step 10 findings: (1) the exact check "mix -100 equals the reference"
  failed once with a difference of -135 dB: the test waited 0.3 s after the
  change, and the 20 ms mix smoother snaps to its target only after about
  0.34 s (`OnePoleSmoother` snaps below a distance of 1e-7); exact checks now
  wait 1 s. (2) One xrun appeared once at the moment shipahoy was added to the
  running graph; 40 add/connect/remove cycles of probe and shipahoy gave
  none, so xruns are now judged only while the graph processes and graph
  changes are reported. The last run passed all checks: worst filter
  deviation 0.102 dB, shipahoy adds 2.2 points of CPU.
- Still open: a look at the faces in the web UI (step 13); the test board
  with shipahoy stays loaded for that. The hardware knob and footswitch were
  addressed but not turned or pressed by hand.
- Old project: its generator wrote `lv2core#logarithmic` instead of
  `pprops:logarithmic`. mod-ui on the Dwarf showed those ports as
  logarithmic anyway (it matches the name; checked 2026-10-15); other hosts
  were not checked.
- Step 4 finding, decided after step 5: "1 pole phaser" was the 1-pole
  highpass under another name and is removed from the menu and the table.
  "2 pole phaser" stays, although it equals "notch" (same weights and
  resonance shaping, see `documentation/Filters/PoleMixing/README.md`).
- Step 11 (shipahoy page): measured on the Mac with a decaying 110 Hz
  sawtooth at the defaults: output level +2.5 dB rms (resonance 85 %), +7 to
  +8 dB at 100 %, peak below 0.9 for a 0.5 input; 18 to 24 ns per sample on an
  Apple M-series core. The frequency knob is the resonance peak; without
  resonance a lowpass sounds darker than the knob value.
- Step 14 (own mini-plan; Eigenghost, a reverb after AbacDsp's minireverb): the decisions
  were order 16, lines of at most 0.5 s, minireverb's block FDN with a crossfade on size
  changes (not `FdnTankGlide`), damping on 2 lines, a 6-stage pre-diffuser, the four face
  knobs Decay, Dry, Wet, Size. Measured: the tank is lossless at infinite decay (0.02 dB over
  10 s); T60 0.515, 2.02 and 5.02 s for 0.5, 2 and 5 s; a size change by fade leaves
  -109 dB of energy above 5 kHz on a 237 Hz sine, a hard change -46 dB (steady -132 dB);
  left/right correlation of a noise tail 0.20; the 6-stage diffuser is flat within 1e-6 dB
  and reproduces AbacDsp's impulse response within 1e-6; the pitch shifter lands within
  +0.9 .. +6.0 cents (spectral centroid, 237 Hz, -12 .. +12 semitones) and the grain
  handoff leaves -96 dB (unity) and -93 dB (+7) above 5 kHz. The headers compile with the
  device GCC 9.4 (`-Wall -Wextra -Wpedantic -Wshadow`, no warnings). Deviations: the
  Walsh-Hadamard stays unnormalised as in AbacDsp (the 1 / sqrt(N) is in the line gains);
  the pitch tolerance is 8 cents, not 5, and the pitch is measured as the spectral centroid
  (zero crossings miss periods where two grains cancel, the highest bin is a random point
  in the carrier's spread); no allocation check in the unit tests (the generic host test
  does it for the plugin in step 15). Fixed while porting: the wrap of a new read position
  in `ParallelPlainDelay` (two blocks too long when set while running), the 0.001 start
  rate of the pitch shifter, the collapsed diffuser lengths before `setBulge()`, the
  allpass's initial full-buffer delay, the inverted polarity of AbacDsp's block highpass
  (replaced by `OnePoleHighpass`).
- Step 15: `plugins/eigenghost/` (JSON with 4 face knobs and 11 settings, `EigenghostPedal.h`
  with a 16-sample FIFO to the diffuser and tank, the engine built in `setSampleRate()`),
  12 pedal unit tests, 6 host tests, `documentation/Eigenghost/` (8 charts). Measured: all
  generic host tests pass (7.8 us per 128 frames on the Mac, 0.3 % of real time); the decay
  knob gives 0.499, 0.986, 2.86 and 8.99 s for 0.5, 1, 3 and 10 s (the 20 kHz damping
  lowpass still takes treble, so long decays read low broadband; octave bands up to 2 kHz
  stay within 3 %); wet level for noise +0.7, +4.8, +7.8 dB at 0.5, 3, 10 s (input into the
  lines x 1 / sqrt(16), not minireverb's 1 / N); left/right correlation 0.13 at width 100 %;
  a fifth-up pitch line lifts the fifth in the tail by 43 dB; an octave-up shimmer at decay
  30 s settles (-0.5 dB rms between 20 and 40 s, peak 4.6); dry/wet jumps stay as smooth as
  the steady outputs; decay jumps leave -97 dB and size sweeps -103 dB above 5 kHz; echo
  density 0.9 at 163 ms (diffusion 60 %) against 408 ms without diffusion. Changed
  while doing it: `FdnTank` ramps gain changes per sample (stepped per block, decay jumps
  measured -57 dB above 5 kHz), `SchroederDiffuser::setSizes()`, the generator unit `m`.
  Findings: (1) the damping lowpass in 2 of 16 lines can add at most about 0.6 dB of loss
  per pass, so at decay 3 s the high bands bottom out near 2.1 s whatever the cutoff
  (`documentation/Eigenghost/`); a stronger damping needs more damped lines. (2) At
  diffusion 0 the six allpasses are pure delays: a pre-delay of about 2.7 x the diffuser
  size (41 ms at 15 ms); documented in the plugin description. (3) The step 14 tank test's
  hard-change control read lengths from a tank that had not processed a block yet
  (constructor defaults); fixed, the conclusion held.
- Step 16: the first device run cost 14.8 points in the integration test and 8.8 with
  `tools/jp.py perf`; at the author's request the tank was optimised first, all changes
  bit-identical (a recorded 4 s output with size and decay changes compared with `cmp`):
  the Hadamard mix and tap sums on whole rows, two alternating output buffers instead of
  copies, prefetching in the block delay, a local copy of the input block (aliasing), a block
  loop in the allpass, cheaper reads in the pitch shifter. A throwaway benchmark on the device
  (us per 128 frames): delay 56 -> 28, tank 101 -> 76, diffuser 23 -> 15, pitch line 20 ->
  18, whole pedal 213 -> 151 to 159. `perf` now measures 4.7 points at the defaults (4.5 to
  4.7 for the presets), the integration test 9.6 to 9.8 points (graph with siggen and two meters).
  Device test (`tools/jp.py test-device eigenghost`): all checks passed; wet -60, bypass and
  width 0 exact (-240 dB); wet levels of noise within 0.012 dB of the Mac (0.670, 4.790,
  7.762 dB at 0.5, 3, 10 s); the fifth rises 77 dB with the pitch lines; dry/wet jumps
  -27.8 dB against a bound of -25.4 dB; no non-finite or denormal samples for decay jumps,
  size sweeps, the control extremes and silence after noise; addressing confirmed; no
  xruns while processing, no journal lines. The dry/wet bound was first the larger steady
  step (+10 %) and failed once at -26.6 dB: a crossfade of two sines of different phase can
  be louder than either, so the bound is now their sum (Mac host test likewise). The
  generic CPU check now skips in sanitizer builds. Snapshot, page `docs/plugins/eigenghost.md`,
  Mac bundle installed. Finding: the `perf` memory figure misses allocated but untouched
  pages (eigenghost allocates about 1.6 MB, reported 4 kB).

## Target layout

```
jaypacks-dwarf-dsp/
  CLAUDE.md                 project rules for LLM sessions, points to the
                            reference docs (read first)
  README.md                 entry point: what, quick start, layout, the loop
  LICENSE                   MIT (code), exclusions noted at the end
  ARTWORK-LICENSE.txt       reserved terms for the pedal artwork
  BRANDING.md               reserved brand identifiers
  CMakeLists.txt            one project: dsp tests, host tests, plugins, docs
  CMakePresets.json         mac-debug, mac-release, mac-asan
  .clang-format             Allman, 4 spaces, 120 columns
  .gitignore                build-*/, scratch/, explore/, __pycache__/, .venv/,
                            documentation/**/generated/
  dsp/                      header-only DSP blocks, C++17, namespace jp
  plugins/<name>/           <name>.json (description) + <Name>Pedal.h
  lv2/Lv2Adapter.h          the one LV2 wrapper
  third_party/lv2/          vendored LV2 headers (ISC), from tests/lv2
  generator/                jpgen.py (JSON -> ports.h, TTL, modgui, presets),
                            graphics.py and style.py (face, knob filmstrip,
                            footswitch, screenshot, thumbnail),
                            templates/, assets/ (jack images, Inter font)
  tests/unit/               GoogleTest DSP + pedal tests (FetchContent, pinned)
  tests/host/               Mac host harness: dlopen the .so, generic property
                            tests + per-plugin integration tests
  tools/jp.py               generate, build-mac, build-dwarf, deploy, test-device
  tools/modui.py            device client (from dwarf_poc.py)
  tools/device_tests/       on-device tests per plugin
  docker/Dockerfile         moddwarf-new image
  dev-scripts/              dev-configure/build/test/warnings/explore/docs-plots
  docs/                     prose: references, roadmap, architecture,
                            building, testing, device, caveats, plugins/
  documentation/            algorithm charts and analysis (AbacDsp style)
```

## Steps

### Step 1: clone, skeleton, references

| File | Change |
|---|---|
| (clone) | `git clone https://github.com/scjurgen/jaypacks-dwarf-dsp` |
| `CLAUDE.md` | Project rules: C++17, ASCII, style sources, generated files location, commit rules (pointer to the global ones), "update docs with every change". Section "Read first": `docs/references.md`, `docs/roadmap.md`. Rule: before analysing old code, look up the section map in `docs/references.md`; when porting, add a provenance row. |
| `docs/references.md` | AbacDsp at `17dfc3b` (where to look, candidates for later ports), external references (mod-ui, mod-host, LV2, pole mixing), provenance table of ported files. |
| `docs/decisions.md` | Dated decision log (removed on 2026-10-15: the docs describe the current state). |
| `docs/roadmap.md` | The step list of this plan with status per step, open questions, ideas for later (layout redesign, more plugins, PoleMixingFilter port). |
| `README.md` | Purpose, status, layout table, planned workflow (filled in per step). |
| `LICENSE` | MIT, "Copyright (c) 2026 Jürgen Schwietering", with a note on the exclusions (artwork, brand). |
| `.gitignore`, `.clang-format` | As above; `.clang-format` copied unchanged. |
| `CMakeLists.txt`, `CMakePresets.json` | C++17 REQUIRED, extensions off, `-Wall -Wextra`, options `JP_BUILD_TESTS`, `JP_BUILD_PLUGINS`, `JP_BUILD_DOCS`; empty subdirs wired. |
| `dev-scripts/*.sh` | Adapted copies of the abacdsp scripts (paths, `build-tests/`, logs in `build-tests/scratch/`). |
| `docs/README.md` | Docs index. |

The plan itself became the starting content of `docs/roadmap.md`.

### Step 2: DSP for shipahoy plus unit tests

Only what shipahoy needs, ported from `src/dsp`, cleaned up, C++17, `jp::` namespace.
Every ported file gets a provenance row in `docs/references.md`.

| File | Change |
|---|---|
| `dsp/Filters/OnePoleLowpass.h` | `Lowpass6db` with sample-rate parameter (old: fixed 48 kHz), coefficient computed once and shared by the 4 stages. Keeps the state clamp at +-1 (it is part of the sound; documented). |
| `dsp/Filters/PoleMixingFilter.h` | `Multimode4PoleMixer` + the Oberheim table (`MulitmodeFilterSettings` renamed). Drop the commented-out old class, `<iostream>`, `<sstream>`. Lookup by name for the enumeration mapping. |
| `dsp/Numbers/Random.h` | Per-instance xoshiro256** with seed. Fixes: functions were non-inline in a header (ODR), one global state shared by all instances (non-deterministic, two shipahoy instances interfere), `getRandDouble` produced only values below 2^-32, `getRandDistFloat` used the same bits twice (f1 == f2). |
| `dsp/Generators/OrnsteinUhlenbeck.h` | Port of AbacDsp `OrnsteinUhlenbeckProcess` (dx = theta(mu - x)dt + sigma sqrt(dt) dW, theta and mu derived from sigma). Change: the Gaussian comes from `dsp/Numbers/Random.h` (seeded xoshiro + Box-Muller) instead of `std::mt19937` + `std::normal_distribution`, whose output differs between libc++ (Mac) and libstdc++ (Dwarf); this keeps Mac and device output identical and avoids `std::random_device` in the constructor. `dt` is a parameter (shipahoy steps it once per hop, not per sample). |
| `dsp/Parameters/ParameterSmoothing.h` | One-pole per-sample smoother (time constant in ms, sample rate aware), like `GainPedal`. The old `Smoother` with `std::function` is not ported. |
| `dsp/Numbers/PanLaw.h` | `getPanFactorNormalized` (mix as constant-power dry/wet). |
| `tests/unit/CMakeLists.txt` | GoogleTest via FetchContent (tag v1.15.2), every `*_test.cpp` registered by glob, ctest. |
| `tests/unit/Filters/OnePoleLowpass_test.cpp` | -3 dB at cutoff for 44.1/48/96 kHz, DC gain 1, stability. |
| `tests/unit/Filters/PoleMixingFilter_test.cpp` | Per table entry: response class checks (LP: gain at 0.1 fc vs 10 fc, HP inverse, BP peak, notch dip), resonance does not blow up across the cutoff range, finite output for extreme settings, out-of-range index ignored. |
| `tests/unit/Generators/OrnsteinUhlenbeck_test.cpp` | The five AbacDsp tests (zero sigma constant, higher sigma larger spread, reset reproduces, 3-sigma confinement, mean reversion) plus: same seed gives the same sequence. |
| `tests/unit/Numbers/Random_test.cpp` | Range [0,1), mean/variance, determinism per seed, two instances independent. |
| `tests/unit/Parameters/ParameterSmoothing_test.cpp` | Time constant, no overshoot, max step per sample. |
| `tests/unit/Numbers/PanLaw_test.cpp` | -1: dry only, +1: wet only, 0: constant power. |
| `docs/DOCSTYLE.md` | Adapted from AbacDsp `docs/DOCSTYLE.md` (class briefs: contract and reasoning, not callers); applied to the new headers. |

Not ported: `SlowFastLFO.h` / `SpeedyLFO.h`. The old `src/pedals/ShipAhoyPedal.h` holds an
`m_lfo` member but never steps it, so it has no effect on the sound; the
"LFO" of shipahoy is the sample-and-hold timer. Recorded in
`docs/references.md` as available for later.

### Step 3: the shipahoy pedal

| File | Change |
|---|---|
| `plugins/shipahoy/shipahoy.json` | From `bundle/ahoi.json`, renamed to `shipahoy`: ASCII description that names the Zappa reference (no trademark signs), new fields (`uri` derived, `smoothing` as `{"type": "onepole", "ms": 20}` or `"none"`), version 0.1.0. Presets: "Ship Ahoy" (speed about 7 Hz, white random, strong resonance, short glide, a resonant lowpass type) plus a few others ("slow drift" with brown/OU, "toggle trill"). Whether the plugin defaults should equal the "Ship Ahoy" preset is decided at this step (old defaults: 2.5 Hz, Q 50). |
| `plugins/shipahoy/ShipAhoyPedal.h` | From the old `src/pedals/ShipAhoyPedal.h` (same class name, kept on purpose); fixes listed below, each covered by a test. |
| `tests/unit/Plugins/ShipAhoyPedal_test.cpp` | Mix extremes, S&H period = 1/speed, frequency stays inside [f/2^(r/2), f*2^(r/2)], octRange 0 freezes the cutoff, glide times, every filterType and randomtype value runs finite, identical output for any block partition, deterministic per seed. |

Bugs to fix in the pedal (behaviour changes marked *, both agreed):
1. *Filter type list does not match the table: the 16 menu entries are mapped
   to table index 0..15, so e.g. "bp 6dB" plays "1 pole hp" and "hp 6 dB"
   plays "3 pole hp". Fix: explicit name -> table entry mapping.
2. Hard-coded 48000 in `setSpeed` and the filter; `setSamplerate` is empty.
3. *`brown` is identical to `violet` (both r1*r2*r3). Fix: brown is an
   Ornstein-Uhlenbeck random walk (`dsp/Generators/OrnsteinUhlenbeck.h`), stepped once
   per hop and mapped around the centre of the range (0.5) with a clamp to
   [0, 1]; the scaling is chosen so the walk uses most of the octave range
   without sticking at the edges (checked by a test on the distribution).
4. `blockFill<blockSize>` ignores `blockSize` and always processes 16 frames
   (W11): pedal gets `process(in, out, n)` for any `n`.
5. Mix is smoothed per sample inside the pedal (dry/wet gains), not by the
   wrapper per 16-sample tick.
6. Coefficients: 4 `exp` calls per sample while gliding; compute once.
7. Dead code removed: `m_lfo`, `setSpeedSlow/Fast` (`setSpeedFast` called
   `setSpeedSlow`), `setToggleSlowFast`, `setTransition*`, `calcSmoothingSteps`.

### Step 4: algorithm documentation (charts and analysis)

Mirrors AbacDsp `documentation/`. Explore programs are C++17, built behind
`JP_BUILD_DOCS`; one shared venv (`documentation/.venv`, since step 6 the
repository venv `.venv`; gitignored) instead
of one per folder; data in gitignored `generated/`; PNGs checked in at a
lower dpi than AbacDsp (theirs are 100-700 KB each).

| File | Change |
|---|---|
| `documentation/README.md` | Folder table (kind: documentation / exploration / generation, what it covers), how to regenerate, link to AbacDsp's tree for algorithms not ported. |
| `documentation/DSP_MODULES_USED.md` | Header -> explore program table (AbacDsp idea). |
| `documentation/requirements.txt` | numpy, matplotlib pinned (AbacDsp versions). Moved to `requirements.txt` at the root in step 6, with Pillow. |
| `documentation/Plot/PyConPlot.py`, `README.md`, examples | Copied unchanged from AbacDsp. |
| `documentation/OrnsteinUhlenbeck/` | Transformed from AbacDsp: timeline, autocorrelation vs exp(-theta tau), distribution vs Gaussian; C++17 (no `std::numbers`), our class; README notes the RNG change and the per-hop `dt`. Extra plot: the shipahoy mapping to [0, 1] and how often the clamp is hit. |
| `documentation/Filters/PoleMixing/` | Method from AbacDsp, code rewritten for our filter: exact magnitude/phase from the five weights for all 16 shipahoy menu entries, measured response (steady-state sine probe) against it, `Lowpass6db` stage response at 44.1/48/96 kHz, resonance up to self-oscillation (bisection helper idea), cutoff accuracy, behaviour while the cutoff glides, the +-1 state clamp at high input levels. Plus a before/after chart of the menu mapping fix. Not taken: corrections fit and generated header (we do not port that filter). |
| `documentation/ShipAhoy/` | README opens with the background (Zappa, "Ship Ahoy", VCF in sample-and-hold mode, about 7 Hz hops, high Q). Charts: the "Ship Ahoy" preset itself (cutoff steps at 7 Hz over a few seconds, spectrogram-style band energies of noise and of a sawtooth through it, the resonance peak riding on each step), hop trajectories per random type (cutoff over time, distribution of hop values), glide shapes for the two smoothing controls, mix law. |
| `dev-scripts/dev-docs-plots.sh [folder]` | Builds explore programs, runs them, renders the PNGs (AbacDsp has no wrapper for this). |

### Step 5: LV2 adapter, generator (no graphics yet), Mac build

| File | Change |
|---|---|
| `lv2/Lv2Adapter.h` | Template over the pedal and the generated ports table: `new`/`delete` (W1), any `n_samples` (W2), `activate` resets DSP state (W6), no file I/O or allocation in `run` (W7), no false `extensionData` (W3), only required features declared (W4). Discrete controls applied when changed, continuous ones passed to the pedal every block (pedal smooths). |
| `generator/jpgen.py` | Stdlib Python, `pathlib`/`subprocess`, no bare `except`. Validates the JSON (types, ranges, default inside range, unique symbols, enumeration values) and writes into `build-*/gen/<name>/`: `ports.h` (constexpr table + setter dispatch), `<name>.ttl`, `manifest.ttl`, presets, `modgui.ttl`. |
| `generator/templates/*` | TTL/manifest/preset templates (from `build-bundle/templates/generic`) with the new URI, brand and binary name. |
| `plugins/CMakeLists.txt` | Per plugin: run the generator (re-run when JSON, templates or generator change, fixes B10), build `jaypacks-<name>.so`, assemble `build-*/lv2/jaypacks-<name>.lv2`. |
| `third_party/lv2/` | Vendored LV2 headers incl. the `lv2.h` redirect fix from step 2 of the old cleanup, plus the LV2 licence file. |
| `tests/host/` | Minimal host (dlopen + `lv2_descriptor`, URID map stub), port table read from the generated TTL. Generic tests for every plugin: finite output on silence/noise, all controls at min/default/max, block sizes 1, 7, 16, 128, 4096 give identical output, 44.1/48/96 kHz, activate clears state, CPU time per block. |
| `tools/jp.py` | `generate`, `build-mac [--install]` (copies to `~/Library/Audio/Plug-Ins/LV2` for Reaper). |

### Step 6: graphics and modgui

| File | Change |
|---|---|
| `generator/graphics.py` | Port of `createBox.py` + `createPreviewImage.py` as functions: face, screenshot, thumbnail, from the JSON colour, name and UI controls. One layout function computes the knob positions; for now it reproduces today's positions (layout redesign comes later). Done: positions from the old stylesheet. |
| `generator/templates/modgui/*` | Icon template html/css from `templates/generic/modgui`. Done, plus `generator/templates/modgui.ttl`; written by `jpgen.py --modgui`. |
| `gui/modgui/`, `generator/assets/` | Shared knob strips, plugs, footswitch, dropdown arrows (only the files that are referenced). Done: knob strip, footswitch, two jack images (no dropdowns: the template has no enumeration list). Step 13: knob strip and footswitch generated, `gui/modgui/` removed. |
| `tests/host/modgui_check.py` | Every UI symbol exists as a control port, all referenced images exist and have the expected size. Done (ctest `host.modgui`), also no unused files in `modgui/`. |
| `dev-scripts/dev-venv.sh`, `requirements.txt` | Added: the repository venv for charts and images. |

### Step 7: docker, Dwarf build, deploy

| File | Change |
|---|---|
| `docker/Dockerfile` | From the old one (`moddwarf-new`); image name default `dwarf-new` (exists locally), override `JP_DWARF_IMAGE`. Done (comments only changed). |
| `tools/jp.py` | `build-dwarf` (container with repo mounted, same CMake project, toolchain from `local.env moddwarf-new`, incremental `build-dwarf/`), `deploy [--unload]` (tar.gz in Python, `/sdk/install`, `lv2info` check), symbol/GLIBC check of the `.so` (exports only `lv2_descriptor`, needs at most GLIBC_2.27). Done, plus GLIBCXX/CXXABI limits, the Mac/Dwarf identity check of the generated files and the modgui check. |
| `tools/modui.py` | Device client from `dwarf_poc.py`: http, websocket (`param_set` out; `stats`, output-port values and `hw_map` in), ssh, pedalboard build and restore, addressing. `DWARF_HOST` default 192.168.51.1. No `jack_capture` in the test path. Done without output-port readback and `hw_map` parsing; `status` added. Readback (`OutputReader`, `receive()` with arrival times) added in step 9; `hw_map` parsing (`parse_hw_map()`) added in step 10. |

### Step 8: Mac integration test for shipahoy (knobs and switches)

| File | Change |
|---|---|
| `tests/host/shipahoy_integration_test.cpp` | Through the real `.so`: mix -100 equals the input, +100 is filtered only, mix sweep has no steps (max sample delta), each filterType has the expected spectral shape (band energies at fixed probe frequencies on noise input), each randomtype runs and differs (brown: OU distribution), speed knob sets the hop rate, octRange 0 holds the cutoff, enumeration values out of range are clamped. Uses the same `dsp/Analysis/` code as the meter plugin in step 9, so Mac and device measure the same way (the analysis blocks are therefore written in this step). |
| `dsp/Analysis/*.h` + `tests/unit/*_test.cpp` | Level, band energy (Goertzel), frequency (zero crossings), zipper metrics, with unit tests. |
| (optional, L2) | Run the host harness inside the arm64 container against the Dwarf binary (catches `-ffast-math` differences). Only if loading the `.so` in the container works (glibc 2.31 vs 2.27, to verify). |

### Step 9: test plugins (own mini-plan first)

Principle: the plugins measure in place and report processed values; the Mac
only steers and reads numbers.

- Steering: mod-ui websocket `param_set` (verified for input ports), the same
  path the web UI uses; everything is also visible and adjustable in the UI.
- Readback: LV2 control output ports. mod-ui forwards output-port values over
  the websocket after the client echoes `data_ready`; the exact message and
  the update rate are verified on the device first (a tiny `jp-probe` plugin
  with a counter output), before the real plugins are built.
- Measurement protocol, so readings are stable despite the update rate: a
  `measure` trigger input starts a window of N seconds (input port
  `window`); outputs hold the results of the last finished window plus a
  `sequence` counter output that increments per result, so the Mac knows a
  value is fresh. A `reset` input clears accumulators.
- Outputs: peak, RMS, DC, crest factor, NaN/Inf and denormal counts,
  frequency, band energies at up to 8 probe frequencies (frequencies as input
  ports), max sample to sample delta, residual energy after removing the test
  tone (zipper). No FFT output arrays at first; if a spectrum is needed, a
  fixed set of band outputs (e.g. 1/3 octave, 31 ports) is the in-plugin
  route.
- Fallback only if output ports turn out unusable: the plugin writes results
  from a non-RT worker thread to a small file in `/tmp`, read over ssh.

| File | Change |
|---|---|
| `plugins/jp-probe/` | Minimal plugin to verify output-port readback (built as `plugins/probe/`, kept as a smoke test by the author's choice). |
| `plugins/siggen/` | `jp-siggen`: silence, sine, square, saw, white/pink noise (seeded), impulse train, log sweep, step; frequency, level, on/off; sample player for WAVs in its bundle. Also reports its own level and sequence, so the chain can be checked end to end. |
| `plugins/meter/` | `jp-meter`: stereo in (signal, optional reference), outputs as listed above. |
| `documentation/Analysis/` | Accuracy charts for the analysis blocks (frequency estimator error, Goertzel band leakage, level ballistics); `documentation/Filters/PinkFilter/` if pink noise is ported from AbacDsp. |

### Step 10: device integration test for shipahoy

| File | Change |
|---|---|
| `tools/device_tests/test_shipahoy.py` | On the Dwarf: deploy siggen, shipahoy, meter; board `siggen -> shipahoy -> meter` (plus `siggen -> meter` reference input); knobs and enumerations via websocket `param_set`; bypass (`:bypass`) on/off; checks on the meter readings: mix -100 equals reference, filter types by band energies, mix sweep zipper metric, no NaN/Inf; knob1 -> mix and footswitch -> bypass addressing checked via `hw_map`; xruns unchanged, CPU ceiling, jack2 journal clean; the test graph stays loaded, `--restore` reloads the previous board. |
| `tools/jp.py` | `test-device <plugin>` (done, with `--build`, `--no-deploy`, `--restore`). |

### Step 11: documentation (written along the way, completed here)

| File | Content |
|---|---|
| `README.md` | Overview, quick start, the development loop, layout, links. |
| `docs/architecture.md` | JSON -> generator -> adapter -> pedal, naming and URIs, versioning, where generated files go. |
| `docs/plugin-json.md` | Reference of every JSON field, how to add a plugin. |
| `docs/building.md` | Prerequisites (cmake, ninja, Python 3, Pillow, docker), generate, Mac build + Reaper, docker image build (about 15 min), Dwarf build, deploy, regenerating the charts. |
| `docs/testing.md` | Test pyramid L0-L3, commands, how to add tests, thresholds. |
| `docs/device.md` | Device facts and control (facts verified before the project marked as such). |
| `docs/caveats-and-limits.md` | C++17 / GCC 9.4, `-ffast-math` and denormals, CPU budget (2.67 ms per 128 frames), RT rules, plugin-in-use on reinstall, no SFTP (`scp -O`), device clock, modgui images made on the Mac (step 13: no macOS font any more), modgui only in the web UI. |
| `docs/plugins/shipahoy.md` | Background: the name and Frank Zappa's "Ship Ahoy", what a sample-and-hold VCF does, why about 7 Hz and a high Q give that sound; historical details (song release, the filter unit used) only with a cited source, otherwise left out (the old JSON's "Oberheim VCF-200 / Matrix" claim is checked before it is repeated). Technical: signal flow, pole mixing math, table, S&H, glide and the OU walk, parameters with ranges, CPU, links to the charts in `documentation/ShipAhoy/`. Musical: getting the "Ship Ahoy" sound step by step (speed about 7 Hz, resonance high, range 2-4 octaves, centre frequency by ear, glide short), then variations (slow sweeps, toggle trills, brown drift, rhythmic hops synced by ear), filter type guide, resonance warning (level jumps at high Q), guitar/bass/synth tips. |
| `docs/test-plugins.md`, `docs/plugins/siggen.md`, `docs/plugins/meter.md` | Measurement protocol, port reference, how to write a device test. |
| `docs/references.md`, `docs/roadmap.md` | Final consistency pass (they are maintained in every step anyway). |

### Steps 14 to 16: Eigenghost (own mini-plan)

A reverb after AbacDsp's `examples/minireverb`: mono in, stereo out; a 6-stage Schroeder
pre-diffuser feeds a 16-line block FDN (`FdnTank`, lines of at most 0.5 s) with pitch
shifters in lines 6 and 7, a lowpass (damping) in lines 0 and 1 and a low cut in lines 2
and 3; size changes crossfade in 50 ms. Face: Decay, Dry, Wet, Size; the rest in the
settings panel (pitch 1 and 2, pitch amount, reverse, diffusion, diffuser size, spread,
bulge, damping, low cut, width).

- Step 14: the DSP blocks and their unit tests (done).
- Step 15: `plugins/eigenghost/` (JSON, `EigenghostPedal.h` with a FIFO to the 16-sample
  core, smoothing, dry/wet in dB, M/S width), pedal unit tests, `tests/host/Eigenghost_test.cpp`,
  charts in `documentation/Eigenghost/` (done).
- Step 16: `tools/jp.py build-dwarf`, the tank optimisation, `perf`, the device test,
  `docs/plugins/eigenghost.md`, snapshot, README (done); Reaper listening by the author.

## Risks and unknowns

- GoogleTest via FetchContent needs network at the first configure; the
  charts and the modgui images need numpy, matplotlib and Pillow in `.venv`
  (`dev-scripts/dev-venv.sh`, pip, network once).
- No `lilv`/`sord_validate`/`lv2lint` installed; TTL checks are our own
  structural checks. `brew install lilv` would add `lv2info`/`lv2ls` on the
  Mac; I would ask before installing anything.
- Output-port readback through mod-ui was the main unknown of step 9; the
  probe plugin resolved it: values arrive with a script as the only
  client, so the file fallback is not needed. Not checked: whether an open
  browser tab on the same board changes the update rate.
- Fix 1 and 3 change how existing ahoi settings sound (only matters for
  saved presets; there are none for the new URI).
- The OU port drops `std::normal_distribution`, so its sequences differ from
  AbacDsp for the same seed; the statistical tests still apply.
- AbacDsp has uncommitted changes; a file ported from its working tree may
  differ from `17dfc3b`. The provenance row then says so.
- Reaper loading the Mac bundle is checked by the author (Reaper is not scripted here).
- The new URI is not on the device yet, so old `krachpedal-ahoi` and new
  `jaypacks-shipahoy` coexist; nothing old is removed.
- L2 (harness in docker) depends on the container being able to load the
  cross-built `.so`.

## Verification per step

- Every step: `dev-scripts/dev-test.sh` (unit + host tests) green,
  `dev-scripts/dev-warnings.sh` reports 0 warnings, `clang-format --dry-run`
  clean, ASCII check on all tracked files, references/roadmap
  updated.
- Step 4: `dev-scripts/dev-docs-plots.sh` regenerates every PNG; the author reviews
  the charts.
- Step 5/6: `tools/jp.py build-mac shipahoy --install`, bundle checked by the author in
  Reaper.
- Step 7: `tools/jp.py build-dwarf shipahoy` with 0 warnings, symbol/GLIBC check,
  `deploy` returns ok and `lv2info` finds the plugin (after the author's go-ahead for
  device changes).
- Step 8: shipahoy host integration tests green on the Mac.
- Step 9: probe readback confirmed on the device; siggen -> meter chain
  reports the expected values for known signals (done 2026-10-15).
- Step 10: `tools/jp.py test-device shipahoy` passes (and with `--restore` reloads the previous board); done 2026-10-15.
