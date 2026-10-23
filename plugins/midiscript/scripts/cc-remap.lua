-- CC Remap: controllers are turned into other controllers, everything else goes through.
-- Change the tables below and save the file.

local remap = {
    [1] = 74,    -- mod wheel becomes CC 74 (brightness)
    [11] = 7,    -- expression becomes volume
}
local scale = {
    [11] = 1.0,  -- optional factor for the value of a controller (by its number before the remap)
}

function OnCC(channel, controller, value)
    local target = remap[controller]
    if target == nil then
        Pass()
        return
    end
    local out = math.floor(value * (scale[controller] or 1.0) + 0.5)
    CC(channel, target, clamp(out, 0, 127))
end

function OnNoteOn(channel, note, velocity) Pass() end
function OnNoteOff(channel, note, velocity) Pass() end
function OnPitchBend(channel, value) Pass() end
function OnProgramChange(channel, program) Pass() end
function OnOther(status, data1, data2) Pass() end
