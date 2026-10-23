-- Echoes what is played: at the first step after a note arrived the filter glides to that very note and stays there until the next note. It starts on the first note of the fallback in notes.lua.
-- X: glide in percent of the step, 1 to 100 (20 = a fifth of the step)
-- Y: resonance in percent, 0 to 100
import "notes"

function OnStart()
    SetFilterType(FilterType.Lp12)
    SetResonance(GetY())
end

function OnControl(x, y)
    SetResonance(y)
end

local target = Notes.fallback[1]
local fresh = true

function OnNewPitch(note, hz, pitch, periodicity, level)
    Notes.remember(note)
    target = pitch
    fresh = true
end

function OnStep(step)
    if fresh then
        SetCutoff(target, { glide = math.max(0.01, GetX() / 100) })
        fresh = false
    end
end
