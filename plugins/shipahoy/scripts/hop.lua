-- Hop: the classic Ship Ahoy. On every step the filter frequency jumps to a random note inside three octaves around
-- 1 kHz and glides there; white random (every note of the range is equally likely).
-- X: glide in percent of the step, 1 to 100 (5 = a short glide, 100 = glides all the way to the next step)
-- Y: resonance in percent, 0 to 100 (85 = the classic, above 95 it rings hard)
local centre = 69 + 12 * math.log(1000 / 440, 2) -- 1 kHz as a MIDI note
local range = 36                                 -- three octaves, in semitones

function OnStart()
    SetFilterType(FilterType.Lp12)
    SetResonance(GetY())
end

function OnControl(x, y)
    SetResonance(y)
end

function OnStep(step)
    SetCutoff(centre + (math.random() - 0.5) * range, { glide = math.max(0.01, GetX() / 100) })
end
