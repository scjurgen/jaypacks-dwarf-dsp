-- Random hold
-- A random note of a scale on every step. Shows math.random and picking from a list, and X and Y as knobs: X is the
-- glide in percent of the step, Y the resonance. Change the scale and the root.
local scale = { 0, 2, 3, 5, 7, 8, 10 } -- natural minor, in semitones above the root
local root = 45                         -- A2

function OnStart()
    SetFilterType(FilterType.Lp24)
end

function OnControl(x, y)
    SetResonance(y)
end

function OnStep(step)
    local degree = math.random(#scale)
    local octave = math.random(0, 2)
    SetCutoff(root + scale[degree] + 12 * octave, { glide = math.max(0.01, GetX() / 100) })
end
