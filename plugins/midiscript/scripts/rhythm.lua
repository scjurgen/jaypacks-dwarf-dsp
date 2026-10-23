-- Rhythm: helpers for steps on the clock (24 ticks per quarter note).
-- import "rhythm"
--
--   Rhythm.swing(step, amount, stepTicks)   ticks to delay step number `step` (counting from 0): the odd steps are
--                                           pushed back by amount (0 to 0.5) of a step
--   Rhythm.euclid(pulses, steps)            a list of `steps` booleans with `pulses` of them true, spread evenly

Rhythm = {}

function Rhythm.swing(step, amount, stepTicks)
    if step % 2 == 1 then
        return amount * stepTicks
    end
    return 0
end

function Rhythm.euclid(pulses, steps)
    local pattern = {}
    local bucket = 0
    for i = 1, steps do
        bucket = bucket + pulses
        if bucket >= steps then
            bucket = bucket - steps
            pattern[i] = true
        else
            pattern[i] = false
        end
    end
    return pattern
end
