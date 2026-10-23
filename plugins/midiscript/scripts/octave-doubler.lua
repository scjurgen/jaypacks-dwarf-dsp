-- Octave Doubler: every note is played together with the same note some octaves higher or lower.
-- Change the settings below and save the file.

local octaveShift = 1       -- 1: an octave up, -1: an octave down, 2: two up ...
local level = 0.7           -- velocity of the added note as a fraction of the played one

local added = {}            -- [channel][note] = the note that was added, or nil

function OnNoteOn(channel, note, velocity)
    Pass()
    local other = note + 12 * octaveShift
    added[channel] = added[channel] or {}
    added[channel][note] = nil
    if other >= 0 and other <= 127 and other ~= note then
        added[channel][note] = other
        NoteOn(channel, other, math.max(1, math.floor(velocity * level + 0.5)))
    end
end

function OnNoteOff(channel, note, velocity)
    Pass()
    local other = added[channel] and added[channel][note]
    if other ~= nil then
        added[channel][note] = nil
        NoteOff(channel, other)
    end
end

function OnCC(channel, controller, value) Pass() end
function OnPitchBend(channel, value) Pass() end
function OnProgramChange(channel, program) Pass() end
