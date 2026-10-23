-- Toggle trill: a trill between low and high. The steps alternate between the lower and the upper 40 % of three
-- octaves around 1 kHz, a random note inside the part each time, with a band pass (6 dB).
-- X: glide in percent of the step, 1 to 100
-- Y: resonance in percent, 0 to 100
local centre = 69 + 12 * math.log(1000 / 440, 2) -- 1 kHz as a MIDI note
local range = 36                                 -- three octaves, in semitones
local spread = 0.4                               -- the part of the range a step may land in
local high = false

function OnStart()
    SetFilterType(FilterType.Bp6)
    SetResonance(GetY())
end

function OnControl(x, y)
    SetResonance(y)
end

function OnStep(step)
    high = not high
    local offset = math.random() * spread
    local position = high and 1 - offset or offset
    SetCutoff(centre + (position - 0.5) * range, { glide = math.max(0.01, GetX() / 100) })
end
