-- Retro Poly: two detuned saws plus a square sub, moderate attack and full
-- sustain for chords, with the master chorus giving the classic poly-synth
-- ensemble width. The mod wheel (Y) brightens the filter; X bends a standard
-- +/-2 semitones.
-- Factory patch of Morphexsynth, from AbacDsp (examples/morphexsynth/factory-patches/Retro Poly.json); SetVolume is the
-- level of the patch, a field of that file there.
-- The slots for pitch bend (X), pressure (Z) and velocity are this repository's addition to the AbacDsp patch, so that
-- a keyboard on channel 1 and an MPE controller both play it expressively.
import "constants"

function OnStart()
    SetVolume(-6.0)
    SetOscillator(0, { waveform = OscWaveform.Saw, level = 0.5, detune = 0.12, pitchFactor = 1.0 })
    SetOscillator(1, { waveform = OscWaveform.Saw, level = 0.5, detune = -0.12, pitchFactor = 1.0 })
    SetOscillator(2, { waveform = OscWaveform.Square, level = 0.3, pitchFactor = 0.5 })

    SetAmpEnvelope({ attackMs = 40, decayMs = 200, sustainLevel = 0.85, releaseMs = 400 })
    SetFilterEnvelope({ attackMs = 30, decayMs = 300, sustainLevel = 0.55, releaseMs = 400, contour = 1.8 })
    SetFilter({ cutoff = 74, resonance = 0.2, type = "LP4" })
    SetPitchEnvelope({ attackMs = 0, decayMs = 0, depthSemitones = 0, glideMsPerOctave = 0 })
    SetDistortion(0)

    -- oscDepth is semitones-ish; 0.04 wobbled the ensemble far more than a subtle
    -- always-on drift should.
    SetLfo({ waveform = LfoWaveform.Sine, speedHz = 4.8, filterDepth = 0, oscDepth = 0.01, keyFollow = 0 })

    SetChorus({ rateHz = 0.6, depth = 0.5, mix = 0.5 })
    SetReverb({ sizeMeters = 16, decayMs = 1800, dryDb = 0, mixDb = -14 })
    SetPhaser({ rateHz = 0.2, depth = 0.5, feedback = 0.2, mix = 0.18 })

    -- Y (mod wheel / slide): brighten the filter
    SetCtrlSlot(0, { source = CtrlSource.Y, curve = CtrlCurve.Linear, target = CtrlTarget.FilterCutoff,
                     valueType = CtrlValueType.Abs, depth = 0.6 })
    -- X (pitch bend): standard +/-2 semitones
    SetCtrlSlot(1, { source = CtrlSource.X, curve = CtrlCurve.Linear, target = CtrlTarget.PitchBend,
                     valueType = CtrlValueType.BiPolar, depth = 2 / 12 })

    -- Z (pressure): aftertouch opens the filter
    SetCtrlSlot(2, { source = CtrlSource.Z, curve = CtrlCurve.Linear, target = CtrlTarget.FilterCutoff,
                     valueType = CtrlValueType.Abs, depth = 0.5 })

    -- Velocity: harder keys are brighter, soft ones darker (velocity 64 leaves the patch as it is)
    SetCtrlSlot(3, { source = CtrlSource.Velocity, curve = CtrlCurve.Linear, target = CtrlTarget.FilterCutoff,
                     valueType = CtrlValueType.BiPolar, depth = 0.3 })
end
