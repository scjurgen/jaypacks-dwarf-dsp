-- Slow drift: a slow wander. On every step the filter frequency moves to a note a little away from the last one
-- (a random walk around 800 Hz, four octaves wide, remembering about ten steps), gliding all the way, with a
-- 24 dB lowpass and a gentle wobble on top. Meant for a slow tempo (30 BPM at 1/16).
-- X: drift depth in percent, 0 to 100 (50 = 1.5 semitones of wobble, 100 = 3)
-- Y: resonance in percent, 0 to 100
local centre = 69 + 12 * math.log(800 / 440, 2) -- 800 Hz as a MIDI note
local range = 48                                -- four octaves, in semitones
local memory = 0.905                            -- correlation of consecutive steps
local deviation = 0.2                           -- standard deviation of the walk, as a fraction of the range
local state = 0

local function gauss()
    return math.sqrt(-2 * math.log(1 - math.random())) * math.cos(2 * math.pi * math.random())
end

local function wobble(x)
    SetDrift(x * 0.03, { rate = 0.5 })
end

function OnStart()
    SetFilterType(FilterType.Lp24)
    SetResonance(GetY())
    wobble(GetX())
end

function OnControl(x, y)
    SetResonance(y)
    wobble(x)
end

function OnStep(step)
    state = memory * state + math.sqrt(1 - memory * memory) * gauss()
    local position = clamp(0.5 + deviation * state, 0, 1)
    SetCutoff(centre + (position - 0.5) * range, { glide = 1 })
end
