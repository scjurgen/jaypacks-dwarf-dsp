-- Scale Quantiser: every note is moved to the nearest note of a scale.
-- Change the settings below and save the file.

import "scales"

local root = 0                  -- 0 = C, 1 = C#, ... 11 = B
local shape = Scales.Major      -- Scales.Minor, Scales.PentatonicMinor, Scales.Blues ... or your own list

local played = {}               -- [channel][note] = the note that went out, so the note off matches

function OnNoteOn(channel, note, velocity)
    local out = Scales.quantise(note, root, shape)
    played[channel] = played[channel] or {}
    played[channel][note] = out
    NoteOn(channel, out, velocity)
end

function OnNoteOff(channel, note, velocity)
    local out = played[channel] and played[channel][note]
    if out == nil then
        return
    end
    played[channel][note] = nil
    NoteOff(channel, out)
end

function OnCC(channel, controller, value) Pass() end
function OnPitchBend(channel, value) Pass() end
function OnProgramChange(channel, program) Pass() end
