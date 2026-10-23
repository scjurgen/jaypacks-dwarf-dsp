-- Pressure to resonance
-- Pressing a key harder (the Z source, channel or MPE pressure) raises the resonance, and the velocity of the key
-- brightens the note. Shows two slots with different sources and value types: velocity 64 leaves the cutoff where it
-- is (BiPolar), pressure starts at nothing (Abs). Change the depth to hear less or more.
function OnStart()
    SetVolume(-6)
    SetOscillator(0, { waveform = OscWaveform.Square, level = 0.6, pwm = 0.2 })
    SetAmpEnvelope({ attackMs = 3, decayMs = 150, sustainLevel = 0.8, releaseMs = 150 })
    SetFilter({ cutoff = 60, resonance = 0.2 })
    SetCtrlSlot(0, {
        source = CtrlSource.Z,
        curve = CtrlCurve.SquareRoot,
        target = CtrlTarget.FilterResonance,
        valueType = CtrlValueType.Abs,
        depth = 0.6,
    })
    SetCtrlSlot(1, {
        source = CtrlSource.Velocity,
        curve = CtrlCurve.Linear,
        target = CtrlTarget.FilterCutoff,
        valueType = CtrlValueType.BiPolar,
        depth = 0.3,
    })
end
