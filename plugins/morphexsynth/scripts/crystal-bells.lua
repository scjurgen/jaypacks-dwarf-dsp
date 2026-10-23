-- Crystal Bells: real bells ring with inharmonic partials, not a harmonic
-- series - hum (an octave below the strike pitch), prime (the strike pitch),
-- and tierce (a minor third above) dominate what's actually heard. The 3
-- oscillators cover those (mellow Triangle, not bright Square/Saw); high
-- filter resonance adds a 4th, self-ringing partial on the strike, and a
-- strong positive filter-envelope contour lets that brightness decay away
-- as the bell mellows, rather than staying bright forever.
-- Factory patch of Morphexsynth, from AbacDsp (examples/morphexsynth/factory-patches/Crystal Bells.json); SetVolume is the
-- level of the patch, a field of that file there.
-- The slots for pitch bend (X), pressure (Z) and velocity are this repository's addition to the AbacDsp patch, so that
-- a keyboard on channel 1 and an MPE controller both play it expressively.
import "constants"

function OnStart()
    SetVolume(-5.0)
    -- hum: an octave below the strike pitch, detuned for a slow natural beat
    SetOscillator(0, { waveform = OscWaveform.Triangle, level = 0.55, detune = 0.04, pitchFactor = 0.5 })
    -- prime: the strike pitch itself
    SetOscillator(1, { waveform = OscWaveform.Triangle, level = 0.6, detune = -0.03, pitchFactor = 1.0 })
    -- tierce: a minor third above prime - the classic "bell" inharmonicity
    SetOscillator(2, { waveform = OscWaveform.Triangle, level = 0.35, pitchFactor = 1.2 })

    SetAmpEnvelope({ attackMs = 2, decayMs = 2200, sustainLevel = 0, releaseMs = 2200 })
    -- Bright at the strike, mellowing as it rings down: a fairly closed base
    -- cutoff, with a strong positive contour supplying the initial clang.
    SetFilterEnvelope({ attackMs = 2, decayMs = 900, sustainLevel = 0, releaseMs = 900, contour = 3.5 })
    -- Resonance held high (not self-oscillating) so the filter itself rings at
    -- its own cutoff on the strike transient - a cheap 4th, pitched partial.
    SetFilter({ cutoff = 62, resonance = 0.72, type = "LP4" })
    SetPitchEnvelope({ attackMs = 0, decayMs = 0, depthSemitones = 0, glideMsPerOctave = 0 })
    -- A struck bell has no ongoing vibrato.
    SetLfo({ waveform = LfoWaveform.Sine, speedHz = 3, filterDepth = 0, oscDepth = 0, keyFollow = 0 })
    SetDistortion(0)

    SetChorus({ rateHz = 0.7, depth = 0.3, mix = 0.25 })
    SetReverb({ sizeMeters = 30, decayMs = 4000, dryDb = 0, mixDb = -8 })
    SetPhaser({ rateHz = 0.25, depth = 0.4, feedback = 0.1, mix = 0.12 })

    -- X (pitch bend): standard +/-2 semitones, for the occasional bell glissando
    SetCtrlSlot(0, { source = CtrlSource.X, curve = CtrlCurve.Linear, target = CtrlTarget.PitchBend,
                     valueType = CtrlValueType.BiPolar, depth = 2 / 12 })

    -- Z (pressure): aftertouch makes the filter ring louder (the filter envelope has opened the cutoff already)
    SetCtrlSlot(1, { source = CtrlSource.Z, curve = CtrlCurve.Linear, target = CtrlTarget.FilterResonance,
                     valueType = CtrlValueType.Abs, depth = 0.1 })

    -- Velocity: harder keys are brighter, soft ones darker (velocity 64 leaves the patch as it is)
    SetCtrlSlot(2, { source = CtrlSource.Velocity, curve = CtrlCurve.Linear, target = CtrlTarget.FilterCutoff,
                     valueType = CtrlValueType.BiPolar, depth = 0.5 })
end
