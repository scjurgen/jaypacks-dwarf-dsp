-- Chord Player: every key you play sounds a chord on top of it.
-- Change the settings below and save the file.

import "chords"

local shape = Chords.Major  -- Chords.Major, Chords.Minor, Chords.Dominant7 ... or your own list of semitones
local strumMs = 0           -- delay between the notes of the chord (0: all at once)
local inversion = 0         -- how often the lowest note is moved up an octave

local sounding = {}         -- [channel][note] = the notes that this key switched on

function OnNoteOn(channel, note, velocity)
    local notes = Chords.invert(Chords.notes(note, shape), inversion)
    sounding[channel] = sounding[channel] or {}
    sounding[channel][note] = notes
    for i, chordNote in ipairs(notes) do
        NoteOn(channel, chordNote, velocity, {delayMs = (i - 1) * strumMs})
    end
end

function OnNoteOff(channel, note, velocity)
    local notes = sounding[channel] and sounding[channel][note]
    if notes == nil then
        return
    end
    sounding[channel][note] = nil
    for i, chordNote in ipairs(notes) do
        NoteOff(channel, chordNote, {delayMs = (i - 1) * strumMs})
    end
end

function OnCC(channel, controller, value) Pass() end
function OnPitchBend(channel, value) Pass() end
function OnProgramChange(channel, program) Pass() end
