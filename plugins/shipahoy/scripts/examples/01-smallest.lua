-- The smallest script
-- One note per step: OnStep and SetCutoff. Change the note (0 to 127, 69 = A4 = 440 Hz) and hear the filter ring at
-- that pitch. Without a script the plugin has no way to move the frequency, so a call to SetCutoff is the minimum.
function OnStep(step)
    SetCutoff(57)
end
