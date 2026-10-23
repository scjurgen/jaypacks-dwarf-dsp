-- Answers what is played: the newest note starts a phrase one octave above it that climbs through the pitch classes
-- of the last five notes (see notes.lua) and comes back down, one per step. A new note
-- starts the phrase again from its own pitch at the next step.
-- X: glide in percent of the step, 1 to 100 (50 = half the step)
-- Y: resonance in percent, 0 to 100
import "notes"

function OnStart()
    SetFilterType(FilterType.Lp12)
    SetResonance(GetY())
end

function OnControl(x, y)
    SetResonance(y)
end

local position = 0

function OnNewPitch(note, hz, pitch, periodicity, level)
    Notes.remember(note)
    position = 0
end

function OnStep(step)
    local root = Notes.last(1) or Notes.fallback[1]
    local distances = Notes.intervals(root)
    local count = #distances
    local period = math.max(1, 2 * count - 2)
    local k = position % period
    local index = k < count and k + 1 or period - k + 1
    SetCutoff(root + 12 + distances[index], { glide = math.max(0.01, GetX() / 100) })
    position = position + 1
end
