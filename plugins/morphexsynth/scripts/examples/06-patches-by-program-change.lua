-- Patches by program change
-- Program change 0 gives a bright saw, 1 a dark square with a long release. Shows OnProgramChange and that the
-- commands work from a hook too, not only in OnStart. Add a table entry for each patch.
local patches = {
    [0] = function()
        SetOscillator(0, { waveform = OscWaveform.Saw, level = 0.6 })
        SetAmpEnvelope({ attackMs = 3, decayMs = 150, sustainLevel = 0.7, releaseMs = 120 })
        SetFilter({ cutoff = 90, resonance = 0.2 })
    end,
    [1] = function()
        SetOscillator(0, { waveform = OscWaveform.Square, level = 0.6, pwm = 0.3 })
        SetAmpEnvelope({ attackMs = 40, decayMs = 400, sustainLevel = 0.6, releaseMs = 900 })
        SetFilter({ cutoff = 50, resonance = 0.3 })
    end,
}

function OnStart()
    SetVolume(-6)
    patches[0]()
end

function OnProgramChange(channel, program)
    local patch = patches[program]
    if patch then
        patch()
    end
end
