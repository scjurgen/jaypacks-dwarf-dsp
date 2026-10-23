-- A timed arpeggio
-- The held keys are played one after the other on a repeating timer, in an euclidean rhythm of 5 hits in 8 steps. Shows
-- SetTimer (repeating, in ticks), OnTimer, StopTimer and the library "rhythm". The timer runs while a key is held.
-- It plays on channel 1 at velocity 100; take both from the hook arguments to follow the player.
import "rhythm"

local pattern = Rhythm.euclid(5, 8)
local held = {} -- the held notes, as they were pressed
local position = 0

local function remove(note)
    for i, heldNote in ipairs(held) do
        if heldNote == note then
            table.remove(held, i)
        end
    end
end

function OnNoteOn(channel, note, velocity)
    if #held == 0 then
        position = 0
        SetTimer(0, { ticks = Ticks.Eighth, repeating = true })
    end
    remove(note)
    held[#held + 1] = note
end

function OnNoteOff(channel, note, velocity)
    remove(note)
    if #held == 0 then
        StopTimer(0)
    end
end

function OnTimer(id)
    if #held > 0 and pattern[position % #pattern + 1] then
        Note(1, held[position % #held + 1], 100, { lengthTicks = Ticks.Sixteenth })
    end
    position = position + 1
end
