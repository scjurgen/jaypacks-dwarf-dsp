-- Airy Pad: three detuned saws with a slow attack/release and a slow filter sweep,
-- widened by the master chorus and set in space by the master reverb. The mod
-- wheel (Y) opens the filter for a gentle brightening swell.
-- Factory patch of Morphexsynth, from AbacDsp (examples/morphexsynth/factory-patches/Airy Pad.json); SetVolume is the
-- level of the patch, a field of that file there.
-- The slots for pitch bend (X), pressure (Z) and velocity are this repository's addition to the AbacDsp patch, so that
-- a keyboard on channel 1 and an MPE controller both play it expressively.
import "constants"

function OnStart()
    SetVolume(-6.0)
    SetOscillator(0, { waveform = OscWaveform.Saw, level = 0.5, detune = 0.15, pitchFactor = 1.0 })
    SetOscillator(1, { waveform = OscWaveform.Saw, level = 0.5, detune = -0.15, pitchFactor = 1.0 })
    SetOscillator(2, { waveform = OscWaveform.Triangle, level = 0.3, pitchFactor = 2.0 })

    SetAmpEnvelope({ attackMs = 800, decayMs = 400, sustainLevel = 0.8, releaseMs = 1200 })
    SetFilterEnvelope({ attackMs = 1500, decayMs = 500, sustainLevel = 0.7, releaseMs = 1000, contour = 1.6 })
    SetFilter({ cutoff = 66, resonance = 0.15, type = "LP4" })
    SetPitchEnvelope({ attackMs = 0, decayMs = 0, depthSemitones = 0, glideMsPerOctave = 0 })
    SetDistortion(0)

    -- filterDepth is semitones-ish; 0.5 swung the cutoff far too widely for a slow
    -- pad wash, so this is trimmed to a gentle drift.
    SetLfo({ waveform = LfoWaveform.Triangle, speedHz = 0.18, filterDepth = 0.125, oscDepth = 0, keyFollow = 0 })

    SetChorus({ rateHz = 0.5, depth = 0.6, mix = 0.6 })
    SetReverb({ sizeMeters = 24, decayMs = 3200, dryDb = 0, mixDb = -10 })
    SetPhaser({ rateHz = 0.15, depth = 0.6, feedback = 0.15, mix = 0.2 })

    -- Y (mod wheel / slide): slow additional brightening
    SetCtrlSlot(0, { source = CtrlSource.Y, curve = CtrlCurve.Linear, target = CtrlTarget.FilterCutoff,
                     valueType = CtrlValueType.Abs, depth = 0.7 })

    -- X (pitch bend): standard +/-2 semitones
    SetCtrlSlot(1, { source = CtrlSource.X, curve = CtrlCurve.Linear, target = CtrlTarget.PitchBend,
                     valueType = CtrlValueType.BiPolar, depth = 2 / 12 })

    -- Z (pressure): aftertouch opens the filter
    SetCtrlSlot(2, { source = CtrlSource.Z, curve = CtrlCurve.Linear, target = CtrlTarget.FilterCutoff,
                     valueType = CtrlValueType.Abs, depth = 0.4 })

    -- Velocity: harder keys are brighter, soft ones darker (velocity 64 leaves the patch as it is)
    SetCtrlSlot(3, { source = CtrlSource.Velocity, curve = CtrlCurve.Linear, target = CtrlTarget.FilterCutoff,
                     valueType = CtrlValueType.BiPolar, depth = 0.25 })
end
