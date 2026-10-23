# Testing in Reaper on the Mac

The Mac build of a plugin is the same C++ code as the Dwarf build, compiled with Apple clang,
so a desktop host is the fastest place to listen and to try settings. REAPER loads the
jaypacks LV2 bundles on macOS (checked by the author with REAPER 7). The automated tests
(`docs/testing.md`) stay the judge of correctness; Reaper is for listening, for settings the
tests do not cover, and for comparing builds by ear and by null test.

## Install and load

```
tools/jp.py build-mac shipahoy --install    build and copy to ~/Library/Audio/Plug-Ins/LV2
```

- `--install` replaces only bundles whose manifest carries the jaypacks URI; other LV2
  bundles in the folder are left alone.
- Restart REAPER after installing a new build. A running process keeps the binary it loaded
  first, so a new instance in the same session would still run the old code.
- If a plugin does not show up, check that REAPER's LV2 path list (plug-in settings) contains
  `~/Library/Audio/Plug-Ins/LV2`, then re-scan with the cache cleared.
- The plugins appear under their display names (for example "Ship Ahoy", brand "Krachpedal").
  REAPER shows its generic parameter panel; the pedal face (modgui) exists only in mod-ui.
- The test plugins (`JP Signal Gen`, `JP Meter`, `JP Probe`) load as well. Their control
  outputs (the meter's readings) are LV2 output ports; whether and where REAPER displays
  them is not checked.

## What to try

| Goal | How in REAPER |
|---|---|
| Hear a preset | Load the plugin; the defaults are the main preset (for shipahoy "Ship Ahoy"). Presets come from the bundle's `presets.ttl`; check that REAPER lists them in its preset menu |
| Zipper noise | Automate a smoothed control (Mix, Frequency, Resonance) with steps or move it fast while a sustained tone plays; any click is a bug (the device test measures the same with the meter) |
| Bypass and dry | Compare REAPER's FX bypass with Mix -100 (shipahoy): both must sound identical |
| Sample rates | Set the project sample rate to 44.1, 48 and 96 kHz: tuning and time constants follow the rate (the Dwarf runs at 48 kHz) |
| Block sizes | Change the audio device buffer size; the output must not depend on it (the host tests check this exactly) |
| Extreme settings | Every control at its minimum and maximum, resonance at 100 %, silence in and out: no NaN, no runaway level |
| CPU | REAPER's performance meter shows the FX CPU per track; the Dwarf figures are in `README.md` and on each plugin page |

## Comparing builds with a null test

Every plugin instance starts from the same random seed, so an offline render of the same
project is repeatable. To check that a change did not alter the sound (a refactoring, a
moved header), or to hear exactly what it changed:

1. Render the test project with the old build to a WAV file.
2. Install the new build (`tools/jp.py build-mac <name> --install`), restart REAPER, render
   again.
3. Put both renders on two tracks, invert the polarity of one: silence means the builds are
   identical for this project; anything audible is the difference.

Expect small differences between the Mac and the Dwarf: the Gaussian random values and
everything computed from them can differ in the last bits between the two compilers and C
libraries, so compare the device with tolerances (the device tests do), not by null test.

## Differences to the Dwarf

| | Mac in REAPER | Dwarf |
|---|---|---|
| Compiler | Apple clang, release flags | GCC 9.4, `-O3 -ffast-math -fno-finite-math-only` |
| Audio | Project rate, device buffer | 48 kHz, 128 frames, 2 periods |
| UI | Generic parameter panel | modgui pedal face, knobs and footswitch addressing |
| Denormals | Flushed by the adapter during `run()` | the same |

## Outlook

REAPER can be scripted (ReaScript, in Lua or Python), so renders of a set of test projects
could run without a person: load a project, render, compare with a reference render. This is
not set up; the device tests cover the measurable behaviour today. See also
`docs/outlook-lua.md`.
