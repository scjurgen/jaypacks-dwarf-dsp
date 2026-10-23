-- Chords: chord shapes as lists of semitones above the root.
-- import "chords"
--
--   Chords.Major, Chords.Minor, ...        the shapes
--   Chords.notes(root, shape)              the MIDI notes of the chord, those above 127 left out
--   Chords.invert(notes, times)            the chord with the lowest note moved up an octave, `times` times

Chords = {
    Major = {0, 4, 7},
    Minor = {0, 3, 7},
    Diminished = {0, 3, 6},
    Augmented = {0, 4, 8},
    Sus2 = {0, 2, 7},
    Sus4 = {0, 5, 7},
    Major7 = {0, 4, 7, 11},
    Minor7 = {0, 3, 7, 10},
    Dominant7 = {0, 4, 7, 10},
    Major9 = {0, 4, 7, 11, 14},
    Minor9 = {0, 3, 7, 10, 14},
    Power = {0, 7, 12},
}

function Chords.notes(root, shape)
    local notes = {}
    for _, step in ipairs(shape) do
        if root + step <= 127 then
            notes[#notes + 1] = root + step
        end
    end
    return notes
end

function Chords.invert(notes, times)
    local result = {}
    for i, note in ipairs(notes) do
        result[i] = note
    end
    for _ = 1, times do
        table.sort(result)
        local lowest = table.remove(result, 1)
        if lowest ~= nil and lowest + 12 <= 127 then
            result[#result + 1] = lowest + 12
        end
    end
    table.sort(result)
    return result
end
