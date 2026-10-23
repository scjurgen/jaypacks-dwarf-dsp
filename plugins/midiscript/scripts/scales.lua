-- Scales: scale shapes as lists of semitones above the root, and the nearest note of a scale.
-- import "scales"
--
--   Scales.Major, Scales.Minor, ...        the shapes
--   Scales.quantise(note, root, shape)     the note of the scale nearest to `note` (a tie goes down)
--   Scales.contains(note, root, shape)     true if `note` is in the scale
-- root is a pitch class: 0 = C, 1 = C#, ... 11 = B.

Scales = {
    Chromatic = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},
    Major = {0, 2, 4, 5, 7, 9, 11},
    Minor = {0, 2, 3, 5, 7, 8, 10},
    HarmonicMinor = {0, 2, 3, 5, 7, 8, 11},
    Dorian = {0, 2, 3, 5, 7, 9, 10},
    Phrygian = {0, 1, 3, 5, 7, 8, 10},
    Lydian = {0, 2, 4, 6, 7, 9, 11},
    Mixolydian = {0, 2, 4, 5, 7, 9, 10},
    PentatonicMajor = {0, 2, 4, 7, 9},
    PentatonicMinor = {0, 3, 5, 7, 10},
    Blues = {0, 3, 5, 6, 7, 10},
    WholeTone = {0, 2, 4, 6, 8, 10},
}

function Scales.contains(note, root, shape)
    local degree = (note - root) % 12
    for _, step in ipairs(shape) do
        if step == degree then
            return true
        end
    end
    return false
end

function Scales.quantise(note, root, shape)
    for distance = 0, 6 do
        if Scales.contains(note - distance, root, shape) then
            return note - distance
        end
        if Scales.contains(note + distance, root, shape) then
            return note + distance
        end
    end
    return note
end
