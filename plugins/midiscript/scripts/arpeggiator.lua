-- Arpeggiator: the keys you hold are played one after the other in time with the clock.
-- Change the settings below and save the file; copy it to user-1.lua on the device to make it your own.

local mode = "up"            -- "up", "down", "updown" or "random"
local rate = Ticks.Sixteenth -- ticks per step (Ticks.Eighth, Ticks.Sixteenth, Ticks.EighthTriplet ...)
local gate = 0.5             -- how long a note sounds, as a fraction of a step
local swing = 0.0            -- 0 to 0.5: the odd steps are late by this fraction of a step
local octaves = 1            -- how many octaves the pattern climbs

import "rhythm"

local held = {}              -- the held notes, as they were pressed
local channel = 1            -- played on the channel of the last key
local velocity = 100
local position = 0           -- where the pattern is, counted in steps since the first key

local function remove(note)
    for i, heldNote in ipairs(held) do
        if heldNote == note then
            table.remove(held, i)
            return
        end
    end
end

local function pattern()
    local sorted = {}
    for _, note in ipairs(held) do
        sorted[#sorted + 1] = note
    end
    table.sort(sorted)
    local notes = {}
    for octave = 0, octaves - 1 do
        for _, note in ipairs(sorted) do
            if note + 12 * octave <= 127 then
                notes[#notes + 1] = note + 12 * octave
            end
        end
    end
    return notes
end

function OnNoteOn(ch, note, vel)
    if #held == 0 then
        position = 0
    end
    remove(note)
    held[#held + 1] = note
    channel = ch
    velocity = vel
end

function OnNoteOff(ch, note, vel)
    remove(note)
end

function OnTick(tick)
    if tick % rate ~= 0 or #held == 0 then
        return
    end
    local notes = pattern()
    local count = #notes
    local index
    if mode == "down" then
        index = count - (position % count)
    elseif mode == "updown" and count > 1 then
        local cycle = 2 * count - 2
        local at = position % cycle
        index = at < count and at + 1 or cycle - at + 1
    elseif mode == "random" then
        index = math.random(count)
    else
        index = position % count + 1
    end
    local step = tick // rate
    Note(channel, notes[index], velocity, {
        delayTicks = Rhythm.swing(step, swing, rate),
        lengthTicks = rate * gate,
    })
    position = position + 1
end
