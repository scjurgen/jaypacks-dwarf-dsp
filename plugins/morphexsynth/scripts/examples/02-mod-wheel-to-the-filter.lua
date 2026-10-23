-- Mod wheel to the filter
-- The mod wheel (the Y source) opens the filter. Shows SetCtrlSlot: a slot reads a source, shapes it with a curve,
-- scales it by the depth and adds it to a target. Start with the wheel down and a dark filter, then push it up. Change
-- the target (CtrlTarget.Pan ...) or the depth.
function OnStart()
    SetVolume(-6)
    SetOscillator(0, { waveform = OscWaveform.Saw, level = 0.6 })
    SetOscillator(1, { waveform = OscWaveform.Saw, level = 0.4, detune = 0.12 })
    SetAmpEnvelope({ attackMs = 5, decayMs = 200, sustainLevel = 0.7, releaseMs = 250 })
    SetFilter({ cutoff = 40, resonance = 0.3 })
    SetCtrlSlot(0, {
        source = CtrlSource.Y,
        curve = CtrlCurve.Linear,
        target = CtrlTarget.FilterCutoff,
        valueType = CtrlValueType.Abs,
        depth = 0.8,
    })
end
