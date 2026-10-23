-- Transpose
-- Every note is moved up by a number of semitones (negative moves it down). The note off has to go to the same note as
-- its note on, even if the setting changed in between, so the script remembers what it sent. Change the shift.
local shift = 7

local sent = {} -- [channel][note] = the note that went out for this key

function OnNoteOn(channel, note, velocity)
    local out = clamp(note + shift, 0, 127)
    sent[channel] = sent[channel] or {}
    sent[channel][note] = out
    NoteOn(channel, out, velocity)
end

function OnNoteOff(channel, note, velocity)
    local out = sent[channel] and sent[channel][note]
    if out then
        sent[channel][note] = nil
        NoteOff(channel, out)
    end
end

function OnCC(channel, controller, value) Pass() end
function OnPitchBend(channel, value) Pass() end
