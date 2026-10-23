-- A trill: the steps alternate between the lower and the upper part of two octaves around middle C
-- (each a little more than a third of the range), a glide to a random note inside the part.
-- X: glide in percent of the step, 1 to 100
-- Y: resonance in percent, 0 to 100
local low = 48
local high = 72
local part = 0.4 * (high - low)

function OnStart()
    SetFilterType(FilterType.Lp12)
    SetResonance(GetY())
end

function OnControl(x, y)
    SetResonance(y)
end

function OnStep(step)
    local note
    if step % 2 == 0 then
        note = low + math.random() * part
    else
        note = high - math.random() * part
    end
    SetCutoff(note, { glide = math.max(0.01, GetX() / 100) })
end
