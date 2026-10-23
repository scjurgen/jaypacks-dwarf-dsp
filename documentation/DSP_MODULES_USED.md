# DSP modules used by the documentation programs

Which project headers each explore program in `documentation/` includes (directly), so a
change to a header shows which charts to regenerate. Idea from AbacDsp
`documentation/DSP_MODULES_USED.md`. All targets are built by
`dev-scripts/dev-docs-plots.sh` (target `docs_explore`).

| Header | Used by | CMake target |
|---|---|---|
| `dsp/Filters/OnePoleLowpass.h` | `Filters/PoleMixing/PoleMixingExplore.cpp`, `Analysis/AnalysisExplore.cpp` | `PoleMixingExplore`, `AnalysisExplore` |
| `dsp/Analysis/GoertzelBand.h`, `ToneFit.h`, `ZeroCrossingFrequency.h` | `Analysis/AnalysisExplore.cpp` | `AnalysisExplore` |
| `dsp/Generators/OrnsteinUhlenbeck.h` | `OrnsteinUhlenbeck/OrnsteinUhlenbeckExplore.cpp` | `OrnsteinUhlenbeckExplore` |
| `dsp/Numbers/PanLaw.h` | `ShipAhoy/ShipAhoyExplore.cpp` | `ShipAhoyExplore` |
| `dsp/Filters/PoleMixingFilter.h` | `Filters/PoleMixing/PoleMixingExplore.cpp`, `ShipAhoy/ShipAhoyExplore.cpp` | `PoleMixingExplore`, `ShipAhoyExplore` |
| `dsp/Numbers/Random.h` | `OrnsteinUhlenbeck/OrnsteinUhlenbeckExplore.cpp`, `ShipAhoy/ShipAhoyExplore.cpp`, `Analysis/AnalysisExplore.cpp` | `OrnsteinUhlenbeckExplore`, `ShipAhoyExplore`, `AnalysisExplore` |
| `plugins/shipahoy/ShipAhoyEngine.h`, `tests/unit/ShipAhoyClockedEngine.h` (the engine, and the reference hopper with the hop clock that the plugin had in its knob mode before the scripts took over; its algorithms live on in the factory scripts) | `OrnsteinUhlenbeckExplore.cpp`, `PoleMixingExplore.cpp`, `ShipAhoyExplore.cpp` | `OrnsteinUhlenbeckExplore`, `PoleMixingExplore`, `ShipAhoyExplore` |
| `dsp/Reverbs/FdnTank.h` | `Eigenghost/EigenghostExplore.cpp` | `EigenghostExplore` |
| `plugins/eigenghost/EigenghostPedal.h`, `dsp/BlockProcessors/BlockProcVibrato.h`, `dsp/Analysis/ZeroCrossingFrequency.h` | `Eigenghost/EigenghostExplore.cpp` | `EigenghostExplore` |
| `dsp/Filters/FenderToneStack.h`, `VolumeBright.h`, `BiquadCascade.h`, `TwinCabinet.h` (with `Biquad.h`, `BilinearTransform.h`), `dsp/NonLinear/AtanDrive.h` | `JaysTwin/JaysTwinExplore.cpp` | `JaysTwinExplore` |
| `dsp/Wavetables/WaveTableBank.h`, `WaveTableOscillator.h` | `Wavetables/WavetablesExplore.cpp` | `WavetablesExplore` |
| `tests/unit/TestHelpers.h` | `Filters/PoleMixing/PoleMixingExplore.cpp`, `Eigenghost/EigenghostExplore.cpp`, `Wavetables/WavetablesExplore.cpp` | `PoleMixingExplore`, `EigenghostExplore`, `WavetablesExplore` |

`dsp/Analysis/SignalStats.h` has no chart (its results are exact sums, covered by
`tests/unit/Analysis/SignalAnalysis_test.cpp`). `dsp/Parameters/ParameterSmoothing.h` has no chart of its own yet; it is exercised through
`ShipAhoyEngine.h` (smoothed controls) and its unit tests. `dsp/Generators/CutoffTrajectory.h` (the glide) is included by
`ShipAhoyEngine.h`, so the `ShipAhoy/` glide chart (`sa_glide.png`) covers it; its exact endpoints are in
`tests/unit/Generators/CutoffTrajectory_test.cpp`.
