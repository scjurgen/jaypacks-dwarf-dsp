-- Section Brass: two detuned saw/square oscillators with a swelling attack shared
-- between the amp and filter envelopes (contour matched to the swell), like a
-- horn section building in. Z (pressure) adds breath-style brightening while
-- held - the classic MPE wind/brass technique; Y adds bite via resonance.
-- Factory patch of Morphexsynth, from AbacDsp (examples/morphexsynth/factory-patches/Section Brass.json); SetVolume is the
-- level of the patch, a field of that file there.
-- The slots for pitch bend (X), pressure (Z) and velocity are this repository's addition to the AbacDsp patch, so that
-- a keyboard on channel 1 and an MPE controller both play it expressively.
import "constants"

function OnStart()
    SetVolume(-4.0)
    SetOscillator(0, { waveform = OscWaveform.Saw, level = 0.5, pitchFactor = 1.0 })
    SetOscillator(1, { waveform = OscWaveform.Square, level = 0.35, detune = 0.1, pitchFactor = 1.0 })
    -- A third saw, detuned the other way, thickens this into a "section" of horns.
    SetOscillator(2, { waveform = OscWaveform.Saw, level = 0.35, detune = -0.15, pitchFactor = 1.0 })

    SetAmpEnvelope({ attackMs = 80, decayMs = 100, sustainLevel = 0.85, releaseMs = 180 })
    SetFilterEnvelope({ attackMs = 80, decayMs = 150, sustainLevel = 0.6, releaseMs = 180, contour = 2.0 })
    SetFilter({ cutoff = 68, resonance = 0.4, type = "LP4" })
    SetPitchEnvelope({ attackMs = 0, decayMs = 0, depthSemitones = 0, glideMsPerOctave = 0 })
    -- A light, breath-like vibrato - subtle, not a wobble.
    SetLfo({ waveform = LfoWaveform.Sine, speedHz = 4.5, filterDepth = 0, oscDepth = 0.015, keyFollow = 0 })
    SetDistortion(0)

    SetReverb({ sizeMeters = 16, decayMs = 1500, dryDb = 0, mixDb = -15 })
    SetChorus({ rateHz = 0.45, depth = 0.3, mix = 0.2 })
    SetPhaser({ rateHz = 0.3, depth = 0.35, feedback = 0.15, mix = 0.1 })

    -- Z (pressure): breath-style brightening while a note is held
    SetCtrlSlot(0, { source = CtrlSource.Z, curve = CtrlCurve.Linear, target = CtrlTarget.FilterCutoff,
                     valueType = CtrlValueType.Abs, depth = 0.7 })
    -- Y (mod wheel / slide): extra bite via resonance
    SetCtrlSlot(1, { source = CtrlSource.Y, curve = CtrlCurve.Linear, target = CtrlTarget.FilterResonance,
                     valueType = CtrlValueType.Abs, depth = 0.4 })
    -- X (pitch bend): standard +/-2 semitones, for falls and scoops
    SetCtrlSlot(2, { source = CtrlSource.X, curve = CtrlCurve.Linear, target = CtrlTarget.PitchBend,
                     valueType = CtrlValueType.BiPolar, depth = 2 / 12 })

    -- Velocity: harder keys are brighter, soft ones darker (velocity 64 leaves the patch as it is)
    SetCtrlSlot(3, { source = CtrlSource.Velocity, curve = CtrlCurve.Linear, target = CtrlTarget.FilterCutoff,
                     valueType = CtrlValueType.BiPolar, depth = 0.4 })
end
