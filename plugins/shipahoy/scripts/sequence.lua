-- A fixed sequence of eight notes, one per step.
-- The step length is the Step setting, the speed the Tempo knob (or the host's tempo).
-- X: glide in percent of the step, 1 to 100 (70 reaches each note shortly before the next step)
-- Y: resonance in percent, 0 to 100
local notes = { 57, 60, 64, 67, 64, 60, 62, 59 }

function OnStart()
    SetFilterType(FilterType.Lp12)
    SetResonance(GetY())
end

function OnControl(x, y)
    SetResonance(y)
end

function OnStep(step)
    SetCutoff(notes[step % #notes + 1], { glide = math.max(0.01, GetX() / 100) })
end
