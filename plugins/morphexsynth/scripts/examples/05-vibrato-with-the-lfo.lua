-- Vibrato with the LFO
-- A slow sine LFO moves the pitch of all oscillators by a quarter of a semitone each way, 5.5 times a second. Shows
-- SetLfo: oscDepth is pitch in semitones, filterDepth moves the cutoff instead. Change speedHz and oscDepth, or move
-- the depth to filterDepth for a wobble of the tone instead of the pitch.
function OnStart()
    SetVolume(-6)
    SetOscillator(0, { waveform = OscWaveform.Triangle, level = 0.7 })
    SetAmpEnvelope({ attackMs = 30, decayMs = 200, sustainLevel = 0.8, releaseMs = 300 })
    SetFilter({ cutoff = 90, resonance = 0.1 })
    SetLfo({ waveform = LfoWaveform.Sine, speedHz = 5.5, oscDepth = 0.25, filterDepth = 0 })
end
