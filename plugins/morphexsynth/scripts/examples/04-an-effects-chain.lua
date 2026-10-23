-- An effects chain
-- A patch with the four effects of the effects bus: distortion, chorus, phaser and reverb. Shows that the effects are
-- set like the rest of the patch, and that a mix of 0 (or -100 dB for the reverb) switches one off. Set the effects you
-- do not want to their off values to hear each one on its own.
function OnStart()
    SetVolume(-9)
    SetOscillator(0, { waveform = OscWaveform.Saw, level = 0.6 })
    SetOscillator(1, { waveform = OscWaveform.Saw, level = 0.4, detune = 0.1 })
    SetAmpEnvelope({ attackMs = 10, decayMs = 300, sustainLevel = 0.6, releaseMs = 400 })
    SetFilter({ cutoff = 70, resonance = 0.15 })
    SetDistortion(2)
    SetChorus({ rateHz = 0.5, depth = 0.4, mix = 0.4 })
    SetPhaser({ rateHz = 0.2, depth = 0.5, feedback = 0.3, mix = 0.3 })
    SetReverb({ sizeMeters = 15, decayMs = 2000, dryDb = 0, mixDb = -12 })
end
