-- Quantise to a scale
-- Every note is moved to the nearest note of D dorian (a tie goes down). Shows the library "scales". The note off goes
-- to the note that the note on went to. Change the root (0 = C ... 11 = B) and the shape (Scales.Blues ...).
import "scales"

local root = 2
local shape = Scales.Dorian

local played = {} -- [channel][note] = the note that went out

function OnNoteOn(channel, note, velocity)
    local out = Scales.quantise(note, root, shape)
    played[channel] = played[channel] or {}
    played[channel][note] = out
    NoteOn(channel, out, velocity)
end

function OnNoteOff(channel, note, velocity)
    local out = played[channel] and played[channel][note]
    if out then
        played[channel][note] = nil
        NoteOff(channel, out)
    end
end
