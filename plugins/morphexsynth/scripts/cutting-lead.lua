-- Cutting Lead: two detuned unison saws through a bright, open filter, with a
-- subtle always-on LFO vibrato. Z (pressure) and the mod wheel (Y) both brighten
-- the filter for expressive lead lines; X bends a standard +/-2 semitones.
-- Factory patch of Morphexsynth, from AbacDsp (examples/morphexsynth/factory-patches/Cutting Lead.json); SetVolume is the
-- level of the patch, a field of that file there.
-- The slots for pitch bend (X), pressure (Z) and velocity are this repository's addition to the AbacDsp patch, so that
-- a keyboard on channel 1 and an MPE controller both play it expressively.
import "constants"

function OnStart()
    SetVolume(-4.0)
    SetOscillator(0, { waveform = OscWaveform.Saw, level = 0.5, detune = 0.08, pitchFactor = 1.0 })
    SetOscillator(1, { waveform = OscWaveform.Saw, level = 0.5, detune = -0.08, pitchFactor = 1.0 })
    -- A third, centered saw thickens the unison without pulling it off pitch.
    SetOscillator(2, { waveform = OscWaveform.Saw, level = 0.4, detune = 0, pitchFactor = 1.0 })

    SetAmpEnvelope({ attackMs = 8, decayMs = 60, sustainLevel = 0.9, releaseMs = 150 })
    SetFilterEnvelope({ attackMs = 5, decayMs = 200, sustainLevel = 0.6, releaseMs = 150, contour = 1.2 })
    SetFilter({ cutoff = 92, resonance = 0.3, type = "LP4" })
    SetPitchEnvelope({ attackMs = 0, decayMs = 0, depthSemitones = 0, glideMsPerOctave = 0 })
    -- Subtle vibrato: oscDepth is semitones-ish, so keep this small - anything above
    -- ~0.02 reads as wobbly rather than a gentle, always-on vibrato.
    SetLfo({ waveform = LfoWaveform.Sine, speedHz = 5.2, filterDepth = 0, oscDepth = 0.02, keyFollow = 0 })
    SetDistortion(0)

    SetReverb({ sizeMeters = 14, decayMs = 1400, dryDb = 0, mixDb = -16 })
    SetChorus({ rateHz = 0.5, depth = 0.3, mix = 0.25 })
    SetPhaser({ rateHz = 0.3, depth = 0.5, feedback = 0.2, mix = 0.15 })

    -- Z (pressure): aftertouch brightening while a note is held
    SetCtrlSlot(0, { source = CtrlSource.Z, curve = CtrlCurve.Linear, target = CtrlTarget.FilterCutoff,
                     valueType = CtrlValueType.Abs, depth = 0.6 })
    -- Y (mod wheel / slide): brighten further
    SetCtrlSlot(1, { source = CtrlSource.Y, curve = CtrlCurve.Linear, target = CtrlTarget.FilterCutoff,
                     valueType = CtrlValueType.Abs, depth = 0.5 })
    -- X (pitch bend): standard +/-2 semitones
    SetCtrlSlot(2, { source = CtrlSource.X, curve = CtrlCurve.Linear, target = CtrlTarget.PitchBend,
                     valueType = CtrlValueType.BiPolar, depth = 2 / 12 })

    -- Z (pressure): aftertouch adds resonance
    SetCtrlSlot(3, { source = CtrlSource.Z, curve = CtrlCurve.Linear, target = CtrlTarget.FilterResonance,
                     valueType = CtrlValueType.Abs, depth = 0.25 })

    -- Velocity: harder keys are brighter, soft ones darker (velocity 64 leaves the patch as it is)
    SetCtrlSlot(4, { source = CtrlSource.Velocity, curve = CtrlCurve.Linear, target = CtrlTarget.FilterCutoff,
                     valueType = CtrlValueType.BiPolar, depth = 0.3 })
end
