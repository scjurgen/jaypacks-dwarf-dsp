-- Kick Punch: a low fundamental with a fast pitch drop (the classic kick-drum
-- trick) and a filter that starts bright then slams shut for the click-into-thud
-- character. Percussion, but it answers velocity, pressure and the pitch bend as well.
-- Factory patch of Morphexsynth, from AbacDsp (examples/morphexsynth/factory-patches/Kick Punch.json); SetVolume is the
-- level of the patch, a field of that file there.
-- The slots for pitch bend (X), pressure (Z) and velocity are this repository's addition to the AbacDsp patch, so that
-- a keyboard on channel 1 and an MPE controller both play it expressively.
import "constants"

function OnStart()
    SetVolume(-1.0)
    SetOscillator(0, { waveform = OscWaveform.Triangle, level = 0.9, pitchFactor = 0.5 })
    SetOscillator(1, { waveform = OscWaveform.White, level = 0.15, pitchFactor = 1.0 })
    -- An octave-up click layer for beater attack, on top of the noise burst.
    SetOscillator(2, { waveform = OscWaveform.Triangle, level = 0.2, pitchFactor = 2.0 })

    SetAmpEnvelope({ attackMs = 1, decayMs = 180, sustainLevel = 0, releaseMs = 60 })
    SetFilterEnvelope({ attackMs = 1, decayMs = 45, sustainLevel = 0, releaseMs = 45, contour = 2.2 })
    SetFilter({ cutoff = 42, resonance = 0.15, type = "LP4" })

    -- The pitch drop that gives a kick its thump
    SetPitchEnvelope({ attackMs = 0, decayMs = 55, depthSemitones = -30, glideMsPerOctave = 0 })
    -- A one-shot has no ongoing vibrato.
    SetLfo({ waveform = LfoWaveform.Sine, speedHz = 5, filterDepth = 0, oscDepth = 0, keyFollow = 0 })
    SetDistortion(0)

    -- A kick wants to stay phase-coherent and punchy: chorus/phaser off, just a hint
    -- of room to sit in a mix rather than sound pasted on top.
    SetReverb({ sizeMeters = 5, decayMs = 300, dryDb = 0, mixDb = -30 })
    SetChorus({ rateHz = 0.4, depth = 0, mix = 0 })
    SetPhaser({ rateHz = 0.2, depth = 0, feedback = 0, mix = 0 })

    -- X (pitch bend): standard +/-2 semitones
    SetCtrlSlot(0, { source = CtrlSource.X, curve = CtrlCurve.Linear, target = CtrlTarget.PitchBend,
                     valueType = CtrlValueType.BiPolar, depth = 2 / 12 })

    -- Z (pressure): aftertouch opens the filter
    SetCtrlSlot(1, { source = CtrlSource.Z, curve = CtrlCurve.Linear, target = CtrlTarget.FilterCutoff,
                     valueType = CtrlValueType.Abs, depth = 0.6 })

    -- Velocity: harder keys are brighter, soft ones darker (velocity 64 leaves the patch as it is)
    SetCtrlSlot(2, { source = CtrlSource.Velocity, curve = CtrlCurve.Linear, target = CtrlTarget.FilterCutoff,
                     valueType = CtrlValueType.BiPolar, depth = 0.4 })
end
