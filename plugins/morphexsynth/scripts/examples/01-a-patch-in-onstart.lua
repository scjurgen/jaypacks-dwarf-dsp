-- A patch in OnStart
-- The smallest useful patch: a saw through a lowpass filter with an amplitude envelope. Shows that a patch is a list of
-- Set... commands in OnStart, and that what a script leaves out starts from a blank patch. Change the waveform
-- (OscWaveform.Square ...), the cutoff (a note number) or the release.
function OnStart()
    SetVolume(-6)
    SetOscillator(0, { waveform = OscWaveform.Saw, level = 0.6 })
    SetAmpEnvelope({ attackMs = 5, decayMs = 200, sustainLevel = 0.7, releaseMs = 250 })
    SetFilter({ cutoff = 80, resonance = 0.2, type = "LP4" })
end
