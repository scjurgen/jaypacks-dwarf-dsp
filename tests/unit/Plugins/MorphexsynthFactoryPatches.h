#pragma once

#include <array>
#include <cstddef>

#include "plugins/morphexsynth/MorphPatch.h"
#include "plugins/morphexsynth/MorphexScriptFiles.h"

namespace jp
{

namespace factory
{

constexpr OscillatorPatch osc(const OscWaveform waveform, const float level, const float detune = 0.0f,
                              const float pitchFactor = 1.0f, const float pwm = 0.0f)
{
    return OscillatorPatch{waveform, level, detune, pwm, pitchFactor};
}

constexpr SlotPatch slot(const CtrlDimension source, const CtrlCurve curve, const CtrlTarget target,
                         const CtrlValueType type, const float depth)
{
    return SlotPatch{source, curve, target, type, depth};
}

}

/// The ten factory patches of AbacDsp's morphexsynth (examples/morphexsynth/factory-patches/) as values (with this
/// repository's slots for the pitch bend, the pressure and the velocity added to each), translated
/// from the scripts by hand (the commands of OnStart() with their values, the omitted fields at the defaults of the
/// command). The plugin makes its patches from the script files (plugins/morphexsynth/scripts/); these values are what
/// a test compares them with. Order as the files; the index is the value of the Patch port.

// clang-format off
inline constexpr std::array<MorphPatch, c_factoryPatchCount> c_factoryPatches{{
    {"4 Ever Mini", -2.401f,
     {{factory::osc(OscWaveform::Square, 0.6786f, 0.0f, 0.5f, 0.3f),
       factory::osc(OscWaveform::SharkFin, 0.7833f, -0.1724f, 0.5f),
       factory::osc(OscWaveform::White, 0.3108f)}},
     {2.0f, 9.89f, 1.0f, 9.89f},
     {41.39f, 374.1f, 0.2814f, 374.1f, 0.0f},
     {},
     {LfoType::Triangle, 1.461f, 0.0f, 0.06829f, 0.0f},
     {54.49f, 0.4807f, 3},
     0,
     {{factory::slot(CtrlDimension::EnvelopeFilter, CtrlCurve::Linear, CtrlTarget::FilterCutoff, CtrlValueType::Abs, 0.4058f),
       factory::slot(CtrlDimension::X, CtrlCurve::Linear, CtrlTarget::PitchBend, CtrlValueType::BiPolar, 2.0f / 12.0f),
       factory::slot(CtrlDimension::Z, CtrlCurve::Linear, CtrlTarget::FilterCutoff, CtrlValueType::Abs, 0.4f),
       factory::slot(CtrlDimension::Velocity, CtrlCurve::Linear, CtrlTarget::FilterCutoff, CtrlValueType::BiPolar, 0.3f)}},
     {}, {}, {}},

    {"Airy Pad", -6.0f,
     {{factory::osc(OscWaveform::Saw, 0.5f, 0.15f),
       factory::osc(OscWaveform::Saw, 0.5f, -0.15f),
       factory::osc(OscWaveform::Triangle, 0.3f, 0.0f, 2.0f)}},
     {800.0f, 400.0f, 0.8f, 1200.0f},
     {1500.0f, 500.0f, 0.7f, 1000.0f, 1.6f},
     {0.0f, 0.0f, 0.0f, 0.0f},
     {LfoType::Triangle, 0.18f, 0.125f, 0.0f, 0.0f},
     {66.0f, 0.15f, 3},
     0,
     {{factory::slot(CtrlDimension::Y, CtrlCurve::Linear, CtrlTarget::FilterCutoff, CtrlValueType::Abs, 0.7f),
       factory::slot(CtrlDimension::X, CtrlCurve::Linear, CtrlTarget::PitchBend, CtrlValueType::BiPolar, 2.0f / 12.0f),
       factory::slot(CtrlDimension::Z, CtrlCurve::Linear, CtrlTarget::FilterCutoff, CtrlValueType::Abs, 0.4f),
       factory::slot(CtrlDimension::Velocity, CtrlCurve::Linear, CtrlTarget::FilterCutoff, CtrlValueType::BiPolar, 0.25f)}},
     {0.15f, 0.6f, 0.15f, 0.2f}, {0.5f, 0.6f, 0.6f}, {24.0f, 3200.0f, 0.0f, -10.0f}},

    {"Crystal Bells", -5.0f,
     {{factory::osc(OscWaveform::Triangle, 0.55f, 0.04f, 0.5f),
       factory::osc(OscWaveform::Triangle, 0.6f, -0.03f, 1.0f),
       factory::osc(OscWaveform::Triangle, 0.35f, 0.0f, 1.2f)}},
     {2.0f, 2200.0f, 0.0f, 2200.0f},
     {2.0f, 900.0f, 0.0f, 900.0f, 3.5f},
     {0.0f, 0.0f, 0.0f, 0.0f},
     {LfoType::Sine, 3.0f, 0.0f, 0.0f, 0.0f},
     {62.0f, 0.72f, 3},
     0,
     {{factory::slot(CtrlDimension::X, CtrlCurve::Linear, CtrlTarget::PitchBend, CtrlValueType::BiPolar, 2.0f / 12.0f),
       factory::slot(CtrlDimension::Z, CtrlCurve::Linear, CtrlTarget::FilterResonance, CtrlValueType::Abs, 0.1f),
       factory::slot(CtrlDimension::Velocity, CtrlCurve::Linear, CtrlTarget::FilterCutoff, CtrlValueType::BiPolar, 0.5f)}},
     {0.25f, 0.4f, 0.1f, 0.12f}, {0.7f, 0.3f, 0.25f}, {30.0f, 4000.0f, 0.0f, -8.0f}},

    {"Cutting Lead", -4.0f,
     {{factory::osc(OscWaveform::Saw, 0.5f, 0.08f),
       factory::osc(OscWaveform::Saw, 0.5f, -0.08f),
       factory::osc(OscWaveform::Saw, 0.4f, 0.0f)}},
     {8.0f, 60.0f, 0.9f, 150.0f},
     {5.0f, 200.0f, 0.6f, 150.0f, 1.2f},
     {0.0f, 0.0f, 0.0f, 0.0f},
     {LfoType::Sine, 5.2f, 0.0f, 0.02f, 0.0f},
     {92.0f, 0.3f, 3},
     0,
     {{factory::slot(CtrlDimension::Z, CtrlCurve::Linear, CtrlTarget::FilterCutoff, CtrlValueType::Abs, 0.6f),
       factory::slot(CtrlDimension::Y, CtrlCurve::Linear, CtrlTarget::FilterCutoff, CtrlValueType::Abs, 0.5f),
       factory::slot(CtrlDimension::X, CtrlCurve::Linear, CtrlTarget::PitchBend, CtrlValueType::BiPolar, 2.0f / 12.0f),
       factory::slot(CtrlDimension::Z, CtrlCurve::Linear, CtrlTarget::FilterResonance, CtrlValueType::Abs, 0.25f),
       factory::slot(CtrlDimension::Velocity, CtrlCurve::Linear, CtrlTarget::FilterCutoff, CtrlValueType::BiPolar, 0.3f)}},
     {0.3f, 0.5f, 0.2f, 0.15f}, {0.5f, 0.3f, 0.25f}, {14.0f, 1400.0f, 0.0f, -16.0f}},

    {"Grand Stage", -3.0f,
     {{factory::osc(OscWaveform::Triangle, 0.65f),
       factory::osc(OscWaveform::Saw, 0.2f, 0.05f),
       factory::osc(OscWaveform::Square, 0.1f, 0.0f, 2.0f)}},
     {2.0f, 3500.0f, 0.15f, 250.0f},
     {2.0f, 2000.0f, 0.3f, 250.0f, 0.8f},
     {0.0f, 0.0f, 0.0f, 0.0f},
     {LfoType::Sine, 4.0f, 0.0f, 0.0f, 0.0f},
     {88.0f, 0.05f, 3},
     0,
     {{factory::slot(CtrlDimension::Velocity, CtrlCurve::Linear, CtrlTarget::FilterCutoff, CtrlValueType::Abs, 0.6f),
       factory::slot(CtrlDimension::X, CtrlCurve::Linear, CtrlTarget::PitchBend, CtrlValueType::BiPolar, 2.0f / 12.0f),
       factory::slot(CtrlDimension::Z, CtrlCurve::Linear, CtrlTarget::FilterCutoff, CtrlValueType::Abs, 0.4f)}},
     {0.25f, 0.4f, 0.15f, 0.15f}, {0.5f, 0.4f, 0.3f}, {18.0f, 2000.0f, 0.0f, -14.0f}},

    {"Harp Pluck", -3.0f,
     {{factory::osc(OscWaveform::Saw, 0.5f),
       factory::osc(OscWaveform::Square, 0.3f, 0.06f),
       factory::osc(OscWaveform::Triangle, 0.2f, 0.0f, 2.0f)}},
     {2.0f, 350.0f, 0.05f, 150.0f},
     {1.0f, 180.0f, 0.0f, 150.0f, 2.8f},
     {0.0f, 25.0f, -2.0f, 0.0f},
     {LfoType::Sine, 4.0f, 0.0f, 0.0f, 0.0f},
     {62.0f, 0.25f, 3},
     0,
     {{factory::slot(CtrlDimension::Z, CtrlCurve::Linear, CtrlTarget::FilterResonance, CtrlValueType::Abs, 0.4f),
       factory::slot(CtrlDimension::X, CtrlCurve::Linear, CtrlTarget::PitchBend, CtrlValueType::BiPolar, 1.0f / 12.0f),
       factory::slot(CtrlDimension::Velocity, CtrlCurve::Linear, CtrlTarget::FilterCutoff, CtrlValueType::BiPolar, 0.4f)}},
     {0.3f, 0.3f, 0.1f, 0.1f}, {0.8f, 0.25f, 0.2f}, {10.0f, 900.0f, 0.0f, -18.0f}},

    {"Kick Punch", -1.0f,
     {{factory::osc(OscWaveform::Triangle, 0.9f, 0.0f, 0.5f),
       factory::osc(OscWaveform::White, 0.15f),
       factory::osc(OscWaveform::Triangle, 0.2f, 0.0f, 2.0f)}},
     {1.0f, 180.0f, 0.0f, 60.0f},
     {1.0f, 45.0f, 0.0f, 45.0f, 2.2f},
     {0.0f, 55.0f, -30.0f, 0.0f},
     {LfoType::Sine, 5.0f, 0.0f, 0.0f, 0.0f},
     {42.0f, 0.15f, 3},
     0,
     {{factory::slot(CtrlDimension::X, CtrlCurve::Linear, CtrlTarget::PitchBend, CtrlValueType::BiPolar, 2.0f / 12.0f),
       factory::slot(CtrlDimension::Z, CtrlCurve::Linear, CtrlTarget::FilterCutoff, CtrlValueType::Abs, 0.6f),
       factory::slot(CtrlDimension::Velocity, CtrlCurve::Linear, CtrlTarget::FilterCutoff, CtrlValueType::BiPolar, 0.4f)}},
     {0.2f, 0.0f, 0.0f, 0.0f}, {0.4f, 0.0f, 0.0f}, {5.0f, 300.0f, 0.0f, -30.0f}},

    {"Retro Poly", -6.0f,
     {{factory::osc(OscWaveform::Saw, 0.5f, 0.12f),
       factory::osc(OscWaveform::Saw, 0.5f, -0.12f),
       factory::osc(OscWaveform::Square, 0.3f, 0.0f, 0.5f)}},
     {40.0f, 200.0f, 0.85f, 400.0f},
     {30.0f, 300.0f, 0.55f, 400.0f, 1.8f},
     {0.0f, 0.0f, 0.0f, 0.0f},
     {LfoType::Sine, 4.8f, 0.0f, 0.01f, 0.0f},
     {74.0f, 0.2f, 3},
     0,
     {{factory::slot(CtrlDimension::Y, CtrlCurve::Linear, CtrlTarget::FilterCutoff, CtrlValueType::Abs, 0.6f),
       factory::slot(CtrlDimension::X, CtrlCurve::Linear, CtrlTarget::PitchBend, CtrlValueType::BiPolar, 2.0f / 12.0f),
       factory::slot(CtrlDimension::Z, CtrlCurve::Linear, CtrlTarget::FilterCutoff, CtrlValueType::Abs, 0.5f),
       factory::slot(CtrlDimension::Velocity, CtrlCurve::Linear, CtrlTarget::FilterCutoff, CtrlValueType::BiPolar, 0.3f)}},
     {0.2f, 0.5f, 0.2f, 0.18f}, {0.6f, 0.5f, 0.5f}, {16.0f, 1800.0f, 0.0f, -14.0f}},

    {"Section Brass", -4.0f,
     {{factory::osc(OscWaveform::Saw, 0.5f),
       factory::osc(OscWaveform::Square, 0.35f, 0.1f),
       factory::osc(OscWaveform::Saw, 0.35f, -0.15f)}},
     {80.0f, 100.0f, 0.85f, 180.0f},
     {80.0f, 150.0f, 0.6f, 180.0f, 2.0f},
     {0.0f, 0.0f, 0.0f, 0.0f},
     {LfoType::Sine, 4.5f, 0.0f, 0.015f, 0.0f},
     {68.0f, 0.4f, 3},
     0,
     {{factory::slot(CtrlDimension::Z, CtrlCurve::Linear, CtrlTarget::FilterCutoff, CtrlValueType::Abs, 0.7f),
       factory::slot(CtrlDimension::Y, CtrlCurve::Linear, CtrlTarget::FilterResonance, CtrlValueType::Abs, 0.4f),
       factory::slot(CtrlDimension::X, CtrlCurve::Linear, CtrlTarget::PitchBend, CtrlValueType::BiPolar, 2.0f / 12.0f),
       factory::slot(CtrlDimension::Velocity, CtrlCurve::Linear, CtrlTarget::FilterCutoff, CtrlValueType::BiPolar, 0.4f)}},
     {0.3f, 0.35f, 0.15f, 0.1f}, {0.45f, 0.3f, 0.2f}, {16.0f, 1500.0f, 0.0f, -15.0f}},

    {"Warm Bass", -3.0f,
     {{factory::osc(OscWaveform::Square, 0.7f, 0.0f, 1.0f, 0.2f),
       factory::osc(OscWaveform::Saw, 0.6f, 0.0f, 0.5f),
       factory::osc(OscWaveform::Triangle, 0.35f, 0.0f, 0.25f)}},
     {2.0f, 40.0f, 0.85f, 120.0f},
     {1.0f, 90.0f, 0.2f, 150.0f, 2.6f},
     {0.0f, 0.0f, 0.0f, 0.0f},
     {LfoType::Triangle, 4.0f, 0.1f, 0.0f, 0.0f},
     {46.0f, 0.35f, 3},
     0,
     {{factory::slot(CtrlDimension::Z, CtrlCurve::SquareRoot, CtrlTarget::FilterResonance, CtrlValueType::Abs, 0.5f),
       factory::slot(CtrlDimension::Y, CtrlCurve::Linear, CtrlTarget::FilterCutoff, CtrlValueType::Abs, 0.8f),
       factory::slot(CtrlDimension::X, CtrlCurve::Linear, CtrlTarget::PitchBend, CtrlValueType::BiPolar, 1.0f),
       factory::slot(CtrlDimension::Z, CtrlCurve::Linear, CtrlTarget::FilterCutoff, CtrlValueType::Abs, 0.35f),
       factory::slot(CtrlDimension::Velocity, CtrlCurve::Linear, CtrlTarget::FilterCutoff, CtrlValueType::BiPolar, 0.3f)}},
     {0.2f, 0.3f, 0.1f, 0.08f}, {0.4f, 0.15f, 0.12f}, {8.0f, 600.0f, 0.0f, -24.0f}},
}};
// clang-format on

}
