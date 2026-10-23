-- A list of notes with a glide
-- One note per step, taken in turn from a table. Shows step counting from 0 and the glide field (0 jumps, 1 arrives
-- as the step ends). Change the notes, or the glide to 0.5 to arrive half way through each step and then hold.
local notes = { 57, 60, 64, 67, 64, 60 }

function OnStart()
    SetFilterType(FilterType.Lp12)
    SetResonance(80)
end

function OnStep(step)
    -- step starts at 0, Lua lists at 1
    SetCutoff(notes[step % #notes + 1], { glide = 0.7 })
end
