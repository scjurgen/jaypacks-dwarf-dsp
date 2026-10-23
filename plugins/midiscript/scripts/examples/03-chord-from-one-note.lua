-- A chord from one note
-- Every key sounds a minor seventh chord on top of it, strummed 15 ms apart. Shows the library "chords" and the delayMs
-- field. The note off is strummed the same way, so a short key press cannot end a note before it began. Change the
-- shape (Chords.Major, Chords.Sus4 ...) or the strum.
import "chords"

local shape = Chords.Minor7
local strumMs = 15

local sounding = {} -- [channel][note] = the notes that this key switched on

function OnNoteOn(channel, note, velocity)
    local notes = Chords.notes(note, shape)
    sounding[channel] = sounding[channel] or {}
    sounding[channel][note] = notes
    for i, chordNote in ipairs(notes) do
        NoteOn(channel, chordNote, velocity, { delayMs = (i - 1) * strumMs })
    end
end

function OnNoteOff(channel, note, velocity)
    local notes = sounding[channel] and sounding[channel][note]
    if notes then
        sounding[channel][note] = nil
        for i, chordNote in ipairs(notes) do
            NoteOff(channel, chordNote, { delayMs = (i - 1) * strumMs })
        end
    end
end
