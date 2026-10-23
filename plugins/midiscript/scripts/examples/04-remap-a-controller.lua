-- Remap a controller
-- The mod wheel (CC 1) is sent as CC 74 (brightness), halved; all other controllers go through. Shows CC and Pass in
-- one hook. Change the numbers, or the factor, to move any controller to another one.
local from, to, factor = 1, 74, 0.5

function OnCC(channel, controller, value)
    if controller == from then
        CC(channel, to, math.floor(value * factor + 0.5))
    else
        Pass()
    end
end
