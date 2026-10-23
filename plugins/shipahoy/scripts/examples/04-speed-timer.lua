-- Speed changes on a timer
-- The sequence runs at normal speed for four seconds, then at double speed for two seconds, and so on. Shows SetTimer,
-- OnTimer and SpeedRatio. A timer set while the speed is 2 follows that speed (see SpeedRatio in API.md).
local notes = { 52, 55, 59, 62 }
local fast = false

function OnStart()
    SetResonance(75)
    SetTimer(0, { ms = 4000 })
end

function OnTimer(id)
    fast = not fast
    SpeedRatio(fast and 2 or 1)
    SetTimer(0, { ms = 4000 })
end

function OnStep(step)
    SetCutoff(notes[step % #notes + 1], { glide = 0.5 })
end
