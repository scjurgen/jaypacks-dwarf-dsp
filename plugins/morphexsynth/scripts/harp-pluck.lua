-- Harp Pluck: slightly detuned saw and square, a fast filter-envelope decay for
-- the bright plucked transient, and a small, fast pitch dip on attack for a
-- realistic string-pluck snap. Light chorus adds shimmer; Z lets a sustained
-- pluck grow more resonant.
-- Factory patch of Morphexsynth, from AbacDsp (examples/morphexsynth/factory-patches/Harp Pluck.json); SetVolume is the
-- level of the patch, a field of that file there.
-- The slots for pitch bend (X), pressure (Z) and velocity are this repository's addition to the AbacDsp patch, so that
-- a keyboard on channel 1 and an MPE controller both play it expressively.
import "constants"

function OnStart()
    SetVolume(-3.0)
    SetOscillator(0, { waveform = OscWaveform.Saw, level = 0.5, pitchFactor = 1.0 })
    SetOscillator(1, { waveform = OscWaveform.Square, level = 0.3, detune = 0.06, pitchFactor = 1.0 })
    -- An octave-up triangle for a touch of pluck sparkle.
    SetOscillator(2, { waveform = OscWaveform.Triangle, level = 0.2, pitchFactor = 2.0 })

    SetAmpEnvelope({ attackMs = 2, decayMs = 350, sustainLevel = 0.05, releaseMs = 150 })
    SetFilterEnvelope({ attackMs = 1, decayMs = 180, sustainLevel = 0, releaseMs = 150, contour = 2.8 })
    SetFilter({ cutoff = 62, resonance = 0.25, type = "LP4" })

    -- A small, fast downward pitch snap on attack, like a plucked string
    SetPitchEnvelope({ attackMs = 0, decayMs = 25, depthSemitones = -2, glideMsPerOctave = 0 })
    -- A plucked string has no ongoing vibrato.
    SetLfo({ waveform = LfoWaveform.Sine, speedHz = 4, filterDepth = 0, oscDepth = 0, keyFollow = 0 })
    SetDistortion(0)

    SetChorus({ rateHz = 0.8, depth = 0.25, mix = 0.2 })
    SetReverb({ sizeMeters = 10, decayMs = 900, dryDb = 0, mixDb = -18 })
    SetPhaser({ rateHz = 0.3, depth = 0.3, feedback = 0.1, mix = 0.1 })

    -- Z (pressure): more resonance the harder a held pluck is pressed
    SetCtrlSlot(0, { source = CtrlSource.Z, curve = CtrlCurve.Linear, target = CtrlTarget.FilterResonance,
                     valueType = CtrlValueType.Abs, depth = 0.4 })
    -- X (pitch bend): modest +/-1 semitone
    SetCtrlSlot(1, { source = CtrlSource.X, curve = CtrlCurve.Linear, target = CtrlTarget.PitchBend,
                     valueType = CtrlValueType.BiPolar, depth = 1 / 12 })

    -- Velocity: harder keys are brighter, soft ones darker (velocity 64 leaves the patch as it is)
    SetCtrlSlot(2, { source = CtrlSource.Velocity, curve = CtrlCurve.Linear, target = CtrlTarget.FilterCutoff,
                     valueType = CtrlValueType.BiPolar, depth = 0.4 })
end
