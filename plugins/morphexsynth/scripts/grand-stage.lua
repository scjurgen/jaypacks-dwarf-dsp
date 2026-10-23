-- Grand Stage: a triangle body with a touch of detuned saw edge for hammer
-- brightness, a fast attack and a long natural decay rather than a held plateau.
-- Velocity brightens the tone (harder hit = brighter), pressure opens the filter and the
-- wheel bends; there is no vibrato, which does not suit this voicing.
-- Factory patch of Morphexsynth, from AbacDsp (examples/morphexsynth/factory-patches/Grand Stage.json); SetVolume is the
-- level of the patch, a field of that file there.
-- The slots for pitch bend (X), pressure (Z) and velocity are this repository's addition to the AbacDsp patch, so that
-- a keyboard on channel 1 and an MPE controller both play it expressively.
import "constants"

function OnStart()
    SetVolume(-3.0)
    SetOscillator(0, { waveform = OscWaveform.Triangle, level = 0.65, pitchFactor = 1.0 })
    SetOscillator(1, { waveform = OscWaveform.Saw, level = 0.2, detune = 0.05, pitchFactor = 1.0 })
    -- A quiet octave-up layer, for extra harmonic complexity in the hammer strike.
    SetOscillator(2, { waveform = OscWaveform.Square, level = 0.1, pitchFactor = 2.0 })

    SetAmpEnvelope({ attackMs = 2, decayMs = 3500, sustainLevel = 0.15, releaseMs = 250 })
    SetFilterEnvelope({ attackMs = 2, decayMs = 2000, sustainLevel = 0.3, releaseMs = 250, contour = 0.8 })
    SetFilter({ cutoff = 88, resonance = 0.05, type = "LP4" })
    SetPitchEnvelope({ attackMs = 0, decayMs = 0, depthSemitones = 0, glideMsPerOctave = 0 })
    -- No vibrato - matches this voicing's own "deliberately static" design.
    SetLfo({ waveform = LfoWaveform.Sine, speedHz = 4, filterDepth = 0, oscDepth = 0, keyFollow = 0 })
    SetDistortion(0)

    -- A stage-piano's classic pedal chain: gentle room, a touch of tremolo-ish
    -- chorus and phaser for that electric-piano character.
    SetReverb({ sizeMeters = 18, decayMs = 2000, dryDb = 0, mixDb = -14 })
    SetChorus({ rateHz = 0.5, depth = 0.4, mix = 0.3 })
    SetPhaser({ rateHz = 0.25, depth = 0.4, feedback = 0.15, mix = 0.15 })

    -- Velocity: harder hits are brighter
    SetCtrlSlot(0, { source = CtrlSource.Velocity, curve = CtrlCurve.Linear, target = CtrlTarget.FilterCutoff,
                     valueType = CtrlValueType.Abs, depth = 0.6 })

    -- X (pitch bend): standard +/-2 semitones
    SetCtrlSlot(1, { source = CtrlSource.X, curve = CtrlCurve.Linear, target = CtrlTarget.PitchBend,
                     valueType = CtrlValueType.BiPolar, depth = 2 / 12 })

    -- Z (pressure): aftertouch opens the filter
    SetCtrlSlot(2, { source = CtrlSource.Z, curve = CtrlCurve.Linear, target = CtrlTarget.FilterCutoff,
                     valueType = CtrlValueType.Abs, depth = 0.4 })
end
