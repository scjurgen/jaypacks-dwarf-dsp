-- Follow what is played
-- The filter steps through the notes played into the plugin. Shows OnNewPitch and the factory library "notes". Until
-- five notes were played the library's fallback scale is used, and the played notes push it out one by one. Play a
-- few notes into the input and listen.
import "notes"

function OnNewPitch(note, hz, pitch, periodicity, level)
    Notes.remember(note)
end

function OnStep(step)
    local window = Notes.window()
    SetCutoff(window[step % #window + 1], { glide = 0.6 })
end
