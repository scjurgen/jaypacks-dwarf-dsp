-- Wave morph on a controller
-- Controller 1 (the mod wheel) moves oscillator 0 from a triangle through a square to a saw. Shows SetWaveMorph called
-- from OnCC while notes sound, and the value 0 to 127 scaled to 0 to 1. Change the controller number or the waveforms.
function OnStart()
    SetVolume(-6)
    SetOscillator(0, { waveform = OscWaveform.Triangle, level = 0.7 })
    SetWaveMorph(0, { waveform2 = OscWaveform.Square, waveform3 = OscWaveform.Saw, morph = 0 })
    SetAmpEnvelope({ attackMs = 5, decayMs = 200, sustainLevel = 0.8, releaseMs = 250 })
    SetFilter({ cutoff = 90, resonance = 0.1 })
end

function OnCC(channel, controller, value)
    if controller == 1 then
        SetWaveMorph(0, { waveform2 = OscWaveform.Square, waveform3 = OscWaveform.Saw, morph = value / 127 })
    end
end
