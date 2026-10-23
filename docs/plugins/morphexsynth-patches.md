# Morphexsynth: the ten factory patches

The patches are AbacDsp's factory patches (`examples/morphexsynth/factory-patches/`), now the Lua scripts
`plugins/morphexsynth/scripts/<name>.lua` (the Patch port selects the file; they were translated by hand into a table of
values first, which is kept as `tests/unit/Plugins/MorphexsynthFactoryPatches.h` and compared with the scripts). This page says what is in them, how the
controllers (X, Y, Z, velocity) reach the sound, and what each one does to the sound, measured. The plugin itself is on
`docs/plugins/morphexsynth.md`.

## How to read the numbers

- **Notes and Hz.** A cutoff is a note number (69 is A at 440 Hz, 60 is middle C, 12 per octave); the Hz value is that note's
  frequency. The filter is a 4-pole lowpass whose resonance peak sits at the cutoff.
- **Resonance** is normalised: 1 is the self-oscillation threshold at the cutoff. The voice multiplies it by 1.5 on its
  way to the filter and limits it to 2.
- **Envelopes** are attack, decay, sustain level and release (times in ms). The filter envelope also has a contour in
  octaves: it moves the cutoff up by that much at its peak (contour 1.6 is 19 semitones).
- **LFO** depths are in units of 12 semitones: a filter depth of 0.125 moves the cutoff by +-1.5 semitones, an oscillator
  depth of 0.02 bends the pitch by +-0.24 semitones (vibrato). An LFO speed is the speed at middle C, it follows the note
  only with a key follow (none of the patches has one).
- **Oscillator** levels are fractions; "detune" is in semitones (0.15 is 15 cent), "factor" is a frequency multiplier against
  the note (0.5 an octave down, 2 an octave up), "pwm" is the pulse width of a square (0 is a 50 % square).
- **Effects** are the patch's own values: phaser rate (Hz), depth, feedback and mix; chorus rate, depth and mix; reverb size
  (m), decay (T60, ms) and mix (dB). The plugin's dials move them: Reverb adds dB to the reverb mix (default -10), Chorus and
  Phaser scale the mixes (default 100 %).
- **Level.** Each patch has its own level in dB; the Vol dial (default -6 dB) is added.

### The controllers

| Controller | MIDI | Reaches the sound as |
|---|---|---|
| X | pitch bend of the note's channel (14 bit) | pitch: the Bend range setting (default 12 semitones, 0: the patch's own range) is the range for +-full bend |
| Y | CC 74 (and CC 1) of the note's channel | what the patch routes it to; 0 to 1 over 0 to 127 |
| Z | channel pressure or poly pressure | what the patch routes it to; 0 to 1 over 0 to 127; the Pressure setting (default 200 %) scales the depth |
| Velocity | note on velocity | what the patch routes it to, plus the level (see below) |

A routing slot reads one controller through a curve (linear unless stated) and adds `depth` times it to a target. The units
of depth: for the **cutoff** depth 1 is 24 semitones at full controller; for the **resonance** depth is added to the
normalised resonance (and multiplied by 1.5 inside); for **pitch** depth 1 is 12 semitones, but the Bend range setting
replaces it. **Slots on one target add** (since 2026-10-17). A bipolar slot reads the controller around its centre: -1 at 0,
0 at the middle (velocity 64) and +1 at 127.

Two things that hold for all patches:

- **Velocity changes the level by 12 dB** from 30 to 127 and by 7 dB from 64 to 127 (the gain is `(v * 0.47 + 0.53)^3`,
  AbacDsp's range of 0.15; the voice started with a factor of 0.1 until 2026-10-18, which gave 2 dB). Every patch also sends the
  velocity to the cutoff, in nine patches with a bipolar slot (soft is darker, hard is brighter, velocity 64 leaves the
  patch as it is).
- **Every patch answers the three controllers** (2026-10-18): a pitch bend slot (X), a pressure slot (Z) and a velocity slot.
  AbacDsp's patches had a bend in six of the ten, a Z routing in four and a velocity routing in one; the slots that are added
  are marked in the scripts.
- **A controller at 0 gives the patch's own sound** (the slots add something to a target; none takes the sound away).

## The controllers of the patches at a glance

| Patch | X (pitch bend) | Y (CC 74) | Z (pressure) | Velocity | Filter envelope |
|---|---|---|---|---|---|
| 4 Ever Mini | pitch | | cutoff (0.4) | cutoff (0.3, bipolar) | cutoff (0.41) |
| Airy Pad | pitch | cutoff (0.7) | cutoff (0.4) | cutoff (0.25, bipolar) | contour 1.6 oct |
| Crystal Bells | pitch | | resonance (0.1) | cutoff (0.5, bipolar) | contour 3.5 oct |
| Cutting Lead | pitch | cutoff (0.5) | cutoff (0.6), resonance (0.25) | cutoff (0.3, bipolar) | contour 1.2 oct |
| Grand Stage | pitch | | cutoff (0.4) | cutoff (0.6) | contour 0.8 oct |
| Harp Pluck | pitch | | resonance (0.4) | cutoff (0.4, bipolar) | contour 2.8 oct |
| Kick Punch | pitch | | cutoff (0.6) | cutoff (0.4, bipolar) | contour 2.2 oct |
| Retro Poly | pitch | cutoff (0.6) | cutoff (0.5) | cutoff (0.3, bipolar) | contour 1.8 oct |
| Section Brass | pitch | resonance (0.4) | cutoff (0.7) | cutoff (0.4, bipolar) | contour 2.0 oct |
| Warm Bass | pitch | cutoff (0.8) | resonance (0.5, square root curve), cutoff (0.35) | cutoff (0.3, bipolar) | contour 2.6 oct |

Y is used by five patches (Airy Pad, Cutting Lead, Retro Poly, Section Brass, Warm Bass): it was not part of the request and
stays as AbacDsp has it.

### What the controllers do to the sound, measured

Middle C on channel 1 (a keyboard without MPE; the same on a member channel), the effects off, the plugin's defaults
(Pressure 200 %, Bend range 12). Pressure and velocity are measured in a window of 4096 samples from 20 ms into the note
(the percussive patches decay quickly), the bend in a window of 8192 samples from 150 ms. "Z" is the change of the energy in
the band from pressure 0 to 127 (velocity 100), "velocity" is the change from velocity 40 to 127 in the level and in the
share of the band above 1 or 3 kHz in the total (a bright patch changes above 3 kHz, where it still has room), "bend" is the
change of the spectral centroid with the full bend up and down (+-12 semitones, so x2 and x0.5 for a plain tone; a filter
and a noise layer make it less). The tests `MorphexsynthPedal.EveryPatch*` print these numbers and fail below 3 dB (Z),
8 dB level and 1 dB share (velocity), x1.25 and x0.85 (bend).

| Patch | Z: above 1 kHz, above 3 kHz | Velocity: level, share above 1 kHz, above 3 kHz | Bend up, down |
|---|---|---|---|
| 4 Ever Mini | +33.1 dB, +36.1 dB | +12.3 dB, +16.6 dB, +17.4 dB | x1.95, x0.54 |
| Airy Pad | +25.0 dB, +36.1 dB | +13.6 dB, +10.6 dB, +12.5 dB | x1.86, x0.57 |
| Crystal Bells | +15.6 dB, +17.4 dB | +10.5 dB, -3.9 dB, +31.3 dB | x1.99, x0.51 |
| Cutting Lead | +2.0 dB, +7.5 dB | +10.4 dB, +2.2 dB, +9.2 dB | x1.73, x0.59 |
| Grand Stage | +1.8 dB, +7.2 dB | +10.2 dB, +2.1 dB, +8.9 dB | x1.97, x0.51 |
| Harp Pluck | +12.3 dB, +0.8 dB | +13.0 dB, +8.2 dB, +21.1 dB | x1.51, x0.65 |
| Kick Punch | +51.6 dB, +50.9 dB | +17.6 dB, +11.1 dB, -1.5 dB | x1.70, x0.50 |
| Retro Poly | +5.1 dB, +18.9 dB | +10.7 dB, +3.6 dB, +14.4 dB | x1.74, x0.59 |
| Section Brass | +5.4 dB, +23.7 dB | +11.9 dB, +2.3 dB, +21.7 dB | x1.80, x0.58 |
| Warm Bass | +43.6 dB, +35.0 dB | +12.9 dB, +15.3 dB, +16.6 dB | x1.38, x0.74 |

What it shows: pressure and velocity move every patch by several dB in the band where it has energy; the patches with a
dark base cutoff (4 Ever Mini, Airy Pad, Kick Punch, Warm Bass) change the most, the bright ones (Cutting Lead, Grand Stage)
mainly above 3 kHz, because their filter is open already and more cutoff adds little. In Crystal Bells the filter envelope has
opened the cutoff on the strike, so Z goes to the resonance (the filter rings louder), and in Harp Pluck Z raises the
resonance and the rest of the sound gets darker. The level of velocity 127 against 40 is 10 to 18 dB (the voice's 11.6 dB
plus the cutoff slot). Y is not part of this table (it is unchanged from 2026-10-17: Warm Bass +4.6 dB, Airy Pad +2.2 dB).

## The patches

### 4 Ever Mini

A bass or lead sound with noise: two half-speed oscillators (a square with pulse width 0.3 and a shark fin, detuned
-17 cent) and a white noise layer. The filter is almost closed (190 Hz) with a resonance of 0.48 and is opened by its
envelope (slow attack of 41 ms, decay of 374 ms to a sustain of 0.28, no contour) and by the slot that sends the filter
envelope to the cutoff (depth 0.41, 10 semitones). A triangle LFO at 1.5 Hz bends the pitch by +-0.8 semitones. No
effects. X bends, Z opens the filter (depth 0.4) and the velocity brightens it (0.3). The only patch of the set without a
description comment in the source.

| Part | Setting |
|---|---|
| Oscillators | Square 0.68 (factor 0.5, pwm 0.3); Shark fin 0.78 (factor 0.5, -0.17 st); White noise 0.31 |
| Amplitude | 2 ms, 9.9 ms, sustain 1, release 9.9 ms |
| Filter | cutoff note 54.5 (190 Hz), resonance 0.48; envelope 41 ms, 374 ms, 0.28, 374 ms |
| LFO | triangle 1.46 Hz, oscillator depth 0.068 (+-0.8 st) |
| Routing | filter envelope to cutoff 0.41; X to pitch; Z to cutoff 0.4; velocity to cutoff 0.3 (bipolar) |
| Effects | none |
| Level | -2.4 dB |

### Airy Pad

Three detuned saws (+-15 cent) with a triangle an octave up, a very slow attack (800 ms) and release (1.2 s) and a filter
that opens over 1.5 s (contour 1.6 octaves), with a slow triangle LFO on the cutoff (0.18 Hz, +-1.5 st). The mod wheel (Y)
opens the filter further (depth 0.7, up to 17 semitones); X bends, Z opens it as well (0.4) and the velocity brightens it (0.25).
Wide chorus (60 % mix), a 24 m reverb with a 3.2 s decay and a
phaser at low depth.

| Part | Setting |
|---|---|
| Oscillators | Saw 0.5 (+0.15 st); Saw 0.5 (-0.15 st); Triangle 0.3 (factor 2) |
| Amplitude | 800 ms, 400 ms, sustain 0.8, release 1200 ms |
| Filter | cutoff note 66 (370 Hz), resonance 0.15; envelope 1500 ms, 500 ms, 0.7, 1000 ms, contour 1.6 oct |
| LFO | triangle 0.18 Hz, filter depth 0.125 |
| Routing | Y to cutoff 0.7; X to pitch; Z to cutoff 0.4; velocity to cutoff 0.25 (bipolar) |
| Effects | phaser 0.15 Hz, depth 0.6, feedback 0.15, mix 0.2; chorus 0.5 Hz, depth 0.6, mix 0.6; reverb 24 m, 3200 ms, -10 dB |
| Level | -6 dB |

### Crystal Bells

A bell after the inharmonic partials of a real bell: three triangles, an octave below (hum), at the note (prime) and a
minor third above (factor 1.2, tierce). High resonance (0.72) makes the filter ring as a fourth partial; a strong
filter envelope (contour 3.5 octaves, 2 ms attack, 900 ms decay to 0) lets the brightness die away; the amplitude decays over
2.2 s to nothing, so a held note fades. X bends (the Bend range setting), Z makes the filter ring louder (resonance, depth 0.1) and the velocity brightens the strike
(0.5). Chorus, a 30 m reverb with a 4 s decay and a little
phaser.

| Part | Setting |
|---|---|
| Oscillators | Triangle 0.55 (factor 0.5, +0.04 st); Triangle 0.6 (-0.03 st); Triangle 0.35 (factor 1.2) |
| Amplitude | 2 ms, 2200 ms, sustain 0, release 2200 ms |
| Filter | cutoff note 62 (294 Hz), resonance 0.72; envelope 2 ms, 900 ms, 0, 900 ms, contour 3.5 oct |
| LFO | sine 3 Hz, no depth |
| Routing | X to pitch; Z to resonance 0.1; velocity to cutoff 0.5 (bipolar) |
| Effects | phaser 0.25 Hz, depth 0.4, feedback 0.1, mix 0.12; chorus 0.7 Hz, depth 0.3, mix 0.25; reverb 30 m, 4000 ms, -8 dB |
| Level | -5 dB |

### Cutting Lead

Two detuned saws (+-8 cent) and a centred saw through a bright, open filter (note 92, 1.7 kHz, resonance 0.3), a quick
attack (8 ms) and a vibrato (sine 5.2 Hz, +-0.24 st). Z (pressure) and Y (mod wheel) both brighten the filter (depths 0.6 and
0.5): **the same parameter**, which the voice now adds. X bends. Because the base cutoff is high, Y and Z move the sound mostly in the top octaves (Z: +7.5 dB above 3 kHz, +2 dB above
1 kHz), so Z raises the resonance as well (0.25) and the velocity brightens it (0.3).

| Part | Setting |
|---|---|
| Oscillators | Saw 0.5 (+0.08 st); Saw 0.5 (-0.08 st); Saw 0.4 |
| Amplitude | 8 ms, 60 ms, sustain 0.9, release 150 ms |
| Filter | cutoff note 92 (1661 Hz), resonance 0.3; envelope 5 ms, 200 ms, 0.6, 150 ms, contour 1.2 oct |
| LFO | sine 5.2 Hz, oscillator depth 0.02 |
| Routing | Z to cutoff 0.6; Y to cutoff 0.5; X to pitch; Z to resonance 0.25; velocity to cutoff 0.3 (bipolar) |
| Effects | phaser 0.3 Hz, depth 0.5, feedback 0.2, mix 0.15; chorus 0.5 Hz, depth 0.3, mix 0.25; reverb 14 m, 1400 ms, -16 dB |
| Level | -4 dB |

### Grand Stage

A stage piano: a triangle body with a little detuned saw and a square an octave up for hammer brightness, a 2 ms attack and a
3.5 s decay to a low sustain (0.15), a filter that opens with the velocity (depth 0.6, up to 14 semitones; the only patch with a slot that is not bipolar, as
AbacDsp has it). X bends and Z opens the filter (0.4); no vibrato. Light chorus and phaser, an 18 m reverb with a 2 s decay.

| Part | Setting |
|---|---|
| Oscillators | Triangle 0.65; Saw 0.2 (+0.05 st); Square 0.1 (factor 2) |
| Amplitude | 2 ms, 3500 ms, sustain 0.15, release 250 ms |
| Filter | cutoff note 88 (1319 Hz), resonance 0.05; envelope 2 ms, 2000 ms, 0.3, 250 ms, contour 0.8 oct |
| LFO | sine 4 Hz, no depth |
| Routing | velocity to cutoff 0.6; X to pitch; Z to cutoff 0.4 |
| Effects | phaser 0.25 Hz, depth 0.4, feedback 0.15, mix 0.15; chorus 0.5 Hz, depth 0.4, mix 0.3; reverb 18 m, 2000 ms, -14 dB |
| Level | -3 dB |

### Harp Pluck

A plucked string: a saw, a square (+6 cent) and a triangle an octave up, a 2 ms attack with a 350 ms decay to almost nothing, a
filter envelope with a fast decay (180 ms, contour 2.8 octaves) for the bright start, and a pitch dip of 2 semitones over
25 ms at the start of every note for the snap of a pluck. Z raises the resonance (depth 0.4): the most audible Z of the set (+10 dB
for a held note), where the rest of the sound gets darker (the energy above 1 kHz falls by 12 dB). X bends by 1 semitone in
the patch; with the default Bend range it is 12. The velocity brightens it (0.4).

| Part | Setting |
|---|---|
| Oscillators | Saw 0.5; Square 0.3 (+0.06 st); Triangle 0.2 (factor 2) |
| Amplitude | 2 ms, 350 ms, sustain 0.05, release 150 ms |
| Filter | cutoff note 62 (294 Hz), resonance 0.25; envelope 1 ms, 180 ms, 0, 150 ms, contour 2.8 oct |
| Pitch envelope | decay 25 ms, depth -2 st |
| LFO | sine 4 Hz, no depth |
| Routing | Z to resonance 0.4; X to pitch (1 st); velocity to cutoff 0.4 (bipolar) |
| Effects | phaser 0.3 Hz, depth 0.3, feedback 0.1, mix 0.1; chorus 0.8 Hz, depth 0.25, mix 0.2; reverb 10 m, 900 ms, -18 dB |
| Level | -3 dB |

### Kick Punch

A one-shot kick drum: a triangle an octave down at level 0.9, a pinch of white noise and a triangle an octave up for the click;
the pitch falls by 30 semitones over 55 ms, the filter (92 Hz, contour 2.2 octaves, 45 ms decay) shuts quickly, the amplitude
decays in 180 ms. X bends, Z opens the filter (0.6) and the velocity makes the hit harder and brighter (0.4); no chorus or phaser, a small room (5 m, 300 ms, -30 dB).

| Part | Setting |
|---|---|
| Oscillators | Triangle 0.9 (factor 0.5); White noise 0.15; Triangle 0.2 (factor 2) |
| Amplitude | 1 ms, 180 ms, sustain 0, release 60 ms |
| Filter | cutoff note 42 (92 Hz), resonance 0.15; envelope 1 ms, 45 ms, 0, 45 ms, contour 2.2 oct |
| Pitch envelope | decay 55 ms, depth -30 st |
| LFO | sine 5 Hz, no depth |
| Routing | X to pitch; Z to cutoff 0.6; velocity to cutoff 0.4 (bipolar) |
| Effects | reverb 5 m, 300 ms, -30 dB (chorus and phaser off) |
| Level | -1 dB |

### Retro Poly

Two detuned saws (+-12 cent) with a square sub an octave down, a moderate attack (40 ms) and a high sustain (0.85) for chords,
with a wide chorus (50 % mix) for the ensemble width of an old poly-synth, a light vibrato (+-0.12 st) and a phaser. Y brightens the
filter (depth 0.6, up to 14 semitones); X bends, Z opens it as well (0.5) and the velocity brightens it (0.3). The default patch of
the plugin.

| Part | Setting |
|---|---|
| Oscillators | Saw 0.5 (+0.12 st); Saw 0.5 (-0.12 st); Square 0.3 (factor 0.5) |
| Amplitude | 40 ms, 200 ms, sustain 0.85, release 400 ms |
| Filter | cutoff note 74 (587 Hz), resonance 0.2; envelope 30 ms, 300 ms, 0.55, 400 ms, contour 1.8 oct |
| LFO | sine 4.8 Hz, oscillator depth 0.01 |
| Routing | Y to cutoff 0.6; X to pitch; Z to cutoff 0.5; velocity to cutoff 0.3 (bipolar) |
| Effects | phaser 0.2 Hz, depth 0.5, feedback 0.2, mix 0.18; chorus 0.6 Hz, depth 0.5, mix 0.5; reverb 16 m, 1800 ms, -14 dB |
| Level | -6 dB |

### Section Brass

A horn section swelling in: a saw, a square (+10 cent) and a saw (-15 cent), a slow attack (80 ms) shared by the amplitude and
the filter envelope (contour 2 octaves), a light breath vibrato. Z adds the breath-style brightening of the classic MPE brass
technique (the cutoff, depth 0.7, up to 17 semitones: +3.6 dB, the energy above 1 kHz up by 6 dB), Y adds bite through the
resonance (depth 0.4), X bends and the velocity brightens it (0.4). The patch the author found good for expressiveness (2026-10-17).

| Part | Setting |
|---|---|
| Oscillators | Saw 0.5; Square 0.35 (+0.1 st); Saw 0.35 (-0.15 st) |
| Amplitude | 80 ms, 100 ms, sustain 0.85, release 180 ms |
| Filter | cutoff note 68 (415 Hz), resonance 0.4; envelope 80 ms, 150 ms, 0.6, 180 ms, contour 2 oct |
| LFO | sine 4.5 Hz, oscillator depth 0.015 |
| Routing | Z to cutoff 0.7; Y to resonance 0.4; X to pitch; velocity to cutoff 0.4 (bipolar) |
| Effects | phaser 0.3 Hz, depth 0.35, feedback 0.15, mix 0.1; chorus 0.45 Hz, depth 0.3, mix 0.2; reverb 16 m, 1500 ms, -15 dB |
| Level | -4 dB |

### Warm Bass

A classic subtractive bass: a square with pulse width 0.2, a saw an octave down and a triangle two octaves down, through a
resonant filter (117 Hz, resonance 0.35) with a fast, punchy envelope (contour 2.6 octaves, 90 ms decay to 0.2). A triangle LFO moves the
cutoff a little (+-1.2 semitones at 4 Hz). Y brightens the tone (depth 0.8: up to 19 semitones, the largest effect of the set,
+4.6 dB and 31 dB more above 1 kHz), Z adds growl through the resonance (a square root curve, depth 0.5), X bends by an octave
in the patch (the default Bend range is 12 semitones too); Z also opens the filter (0.35) and the velocity brightens it (0.3).
Kept subtle in the effects: a bass sits close to dry.

| Part | Setting |
|---|---|
| Oscillators | Square 0.7 (pwm 0.2); Saw 0.6 (factor 0.5); Triangle 0.35 (factor 0.25) |
| Amplitude | 2 ms, 40 ms, sustain 0.85, release 120 ms |
| Filter | cutoff note 46 (117 Hz), resonance 0.35; envelope 1 ms, 90 ms, 0.2, 150 ms, contour 2.6 oct |
| LFO | triangle 4 Hz, filter depth 0.1 |
| Routing | Z to resonance 0.5 (square root curve); Y to cutoff 0.8; X to pitch (1 octave); Z to cutoff 0.35; velocity to cutoff 0.3 (bipolar) |
| Effects | phaser 0.2 Hz, depth 0.3, feedback 0.1, mix 0.08; chorus 0.4 Hz, depth 0.15, mix 0.12; reverb 8 m, 600 ms, -24 dB |
| Level | -3 dB |

## What stands out

- **Every patch bends, answers pressure and follows the velocity** (2026-10-18). Before, four patches had no bend slot, six no Z
  routing, and the velocity moved the brightness of Grand Stage only; the level changed by 2 dB.
- **Velocity is bipolar around 64** in nine patches: soft notes are darker and quieter, velocity 64 gives the sound the patch had
  before, hard notes are brighter (the cutoff by +-0.3 to 0.5 of 24 semitones at the extremes).
- **Z has a short useful range at 200 %**: for the cutoff the filter is fully open before full pressure when the base cutoff is
  high; the resonance of Harp Pluck and Crystal Bells is the opposite, a strong effect from small pressures.
- **The pitch bend range is the plugin's** (Bend range 12) for every patch with an X slot, not the 1 or 2 semitones of
  the original patches (set Bend range to 0 for the patch's own).
- **The slots have no memory of their targets' limits**: the sum of Y, Z, velocity and the envelope can push a cutoff beyond what the
  filter's fit covers (8.6 kHz); the cutoff then stops moving.

## Where it is defined

`plugins/morphexsynth/scripts/` (the scripts), `MorphPatch.h` (the fields), the voice
`dsp/Synthesizer/MorphexsynthVoice.h` (what a slot does, `applyTarget()`), the originals
in AbacDsp `examples/morphexsynth/factory-patches/*.json` (Lua scripts; provenance in `docs/references.md`).
