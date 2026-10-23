-- Follows what is played: the filter steps through the last five notes played into the plugin, oldest to newest,
-- one per step. Until five notes were played the sequence begins with the end of the
-- fallback scale of notes.lua, and the played notes push it out one by one.
-- X: glide in percent of the step, 1 to 100 (70 is a flowing line)
-- Y: resonance in percent, 0 to 100
import "notes"

function OnStart()
    SetFilterType(FilterType.Lp12)
    SetResonance(GetY())
end

function OnControl(x, y)
    SetResonance(y)
end

function OnNewPitch(note, hz, pitch, periodicity, level)
    Notes.remember(note)
end

function OnStep(step)
    local window = Notes.window()
    SetCutoff(window[step % #window + 1], { glide = math.max(0.01, GetX() / 100) })
end
