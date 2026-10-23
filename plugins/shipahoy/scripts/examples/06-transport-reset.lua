-- Reset on transport start
-- Every start of the host's transport begins the pattern again at its first note, at normal speed, with a slow drift
-- that stops with the transport. Shows OnTransportStart and OnTransportStop. The script keeps its own position in the
-- pattern, so it can restart it whenever it likes.
local notes = { 60, 64, 67, 72 }
local index = 0

function OnTransportStart()
    index = 0
    SpeedRatio(1)
    SetDrift(0.5)
end

function OnTransportStop()
    SetDrift(0)
end

function OnStep(step)
    SetCutoff(notes[index % #notes + 1], { glide = 0.4 })
    index = index + 1
end
