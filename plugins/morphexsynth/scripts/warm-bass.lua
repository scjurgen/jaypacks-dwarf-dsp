-- Warm Bass: classic subtractive bass - square + sub-octave saw through a resonant
-- filter with a fast, punchy envelope sweep. Z (MPE pressure) adds growl via
-- resonance; the mod wheel (Y) brightens the tone; X bends a full octave for slides.
-- Factory patch of Morphexsynth, from AbacDsp (examples/morphexsynth/factory-patches/Warm Bass.json); SetVolume is the
-- level of the patch, a field of that file there.
-- The slots for pitch bend (X), pressure (Z) and velocity are this repository's addition to the AbacDsp patch, so that
-- a keyboard on channel 1 and an MPE controller both play it expressively.
import "constants"

function OnStart()
    SetVolume(-3.0)
    SetOscillator(0, { waveform = OscWaveform.Square, level = 0.7, pwm = 0.2, pitchFactor = 1.0 })
    SetOscillator(1, { waveform = OscWaveform.Saw, level = 0.6, pitchFactor = 0.5 })
    -- A sub an octave below the saw, for low-end weight.
    SetOscillator(2, { waveform = OscWaveform.Triangle, level = 0.35, pitchFactor = 0.25 })

    SetAmpEnvelope({ attackMs = 2, decayMs = 40, sustainLevel = 0.85, releaseMs = 120 })
    SetFilterEnvelope({ attackMs = 1, decayMs = 90, sustainLevel = 0.2, releaseMs = 150, contour = 2.6 })
    SetFilter({ cutoff = 46, resonance = 0.35, type = "LP4" })
    SetPitchEnvelope({ attackMs = 0, decayMs = 0, depthSemitones = 0, glideMsPerOctave = 0 })
    -- A hint of filter wobble, not vibrato - subtle enough to add motion, not wander pitch.
    SetLfo({ waveform = LfoWaveform.Triangle, speedHz = 4, filterDepth = 0.1, oscDepth = 0, keyFollow = 0 })
    SetDistortion(0)

    -- Kept subtle throughout: a bass sits tightest close to dry.
    SetReverb({ sizeMeters = 8, decayMs = 600, dryDb = 0, mixDb = -24 })
    SetChorus({ rateHz = 0.4, depth = 0.15, mix = 0.12 })
    SetPhaser({ rateHz = 0.2, depth = 0.3, feedback = 0.1, mix = 0.08 })

    -- Z (pressure): push harder for more resonance/growl
    SetCtrlSlot(0, { source = CtrlSource.Z, curve = CtrlCurve.SquareRoot, target = CtrlTarget.FilterResonance,
                     valueType = CtrlValueType.Abs, depth = 0.5 })
    -- Y (mod wheel / slide): brighten the tone
    SetCtrlSlot(1, { source = CtrlSource.Y, curve = CtrlCurve.Linear, target = CtrlTarget.FilterCutoff,
                     valueType = CtrlValueType.Abs, depth = 0.8 })
    -- X (pitch bend): a full octave, for classic bass slides
    SetCtrlSlot(2, { source = CtrlSource.X, curve = CtrlCurve.Linear, target = CtrlTarget.PitchBend,
                     valueType = CtrlValueType.BiPolar, depth = 1.0 })

    -- Z (pressure): aftertouch opens the filter
    SetCtrlSlot(3, { source = CtrlSource.Z, curve = CtrlCurve.Linear, target = CtrlTarget.FilterCutoff,
                     valueType = CtrlValueType.Abs, depth = 0.35 })

    -- Velocity: harder keys are brighter, soft ones darker (velocity 64 leaves the patch as it is)
    SetCtrlSlot(4, { source = CtrlSource.Velocity, curve = CtrlCurve.Linear, target = CtrlTarget.FilterCutoff,
                     valueType = CtrlValueType.BiPolar, depth = 0.3 })
end
