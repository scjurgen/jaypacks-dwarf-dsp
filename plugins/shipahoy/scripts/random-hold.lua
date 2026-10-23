-- Sample and hold: a random note inside two octaves around middle C on every step, a glide to it.
-- X: glide in percent of the step, 1 to 100
-- Y: resonance in percent, 0 to 100
function OnStart()
    SetFilterType(FilterType.Lp12)
    SetResonance(GetY())
end

function OnControl(x, y)
    SetResonance(y)
end

function OnStep(step)
    SetCutoff(48 + math.random() * 24, { glide = math.max(0.01, GetX() / 100) })
end
