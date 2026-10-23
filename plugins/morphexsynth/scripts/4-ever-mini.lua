-- Factory patch of Morphexsynth, from AbacDsp (examples/morphexsynth/factory-patches/4 Ever Mini.json); SetVolume is the
-- level of the patch, a field of that file there.
-- The slots for pitch bend (X), pressure (Z) and velocity are this repository's addition to the AbacDsp patch, so that
-- a keyboard on channel 1 and an MPE controller both play it expressively.
import "constants"

function OnStart()
    SetVolume(-2.401)
    SetOscillator(0, { waveform = 3, level = 0.6786, pitchFactor = 0.5, pwm = 0.3 })
    SetOscillator(1, { waveform = 1, level = 0.7833, detune = -0.1724, pitchFactor = 0.5 })
    SetOscillator(2, { waveform = 4, level = 0.3108 })  -- noise

    SetAmpEnvelope({ attackMs = 2, decayMs = 9.89, sustainLevel = 1, releaseMs = 9.89 })
    SetFilterEnvelope({ attackMs = 41.39, decayMs = 374.1, sustainLevel = 0.2814, releaseMs = 374.1 })
    SetFilter({ cutoff = 54.49, resonance = 0.4807, type = "LP4" })

    -- Filter envelope has no cutoff path of its own yet (engine gap, see
    -- port-minimoog-presets.md Gap 2) - routed through the MPE matrix instead.
    SetCtrlSlot(0, { source = 5, curve = 2, target = 3, valueType = 0, depth = 0.4058 })

    -- osc3 is Minimoog's free-running pitch-wobble modulator (oscMod=1,
    -- filtModEnable=0), approximated via the LFO's oscDepth.
    SetLfo({ waveform = 1, speedHz = 1.461, oscDepth = 0.06829, keyFollow = 0 })

    -- X (pitch bend): standard +/-2 semitones
    SetCtrlSlot(1, { source = CtrlSource.X, curve = CtrlCurve.Linear, target = CtrlTarget.PitchBend,
                     valueType = CtrlValueType.BiPolar, depth = 2 / 12 })

    -- Z (pressure): aftertouch opens the filter
    SetCtrlSlot(2, { source = CtrlSource.Z, curve = CtrlCurve.Linear, target = CtrlTarget.FilterCutoff,
                     valueType = CtrlValueType.Abs, depth = 0.4 })

    -- Velocity: harder keys are brighter, soft ones darker (velocity 64 leaves the patch as it is)
    SetCtrlSlot(3, { source = CtrlSource.Velocity, curve = CtrlCurve.Linear, target = CtrlTarget.FilterCutoff,
                     valueType = CtrlValueType.BiPolar, depth = 0.3 })
end
