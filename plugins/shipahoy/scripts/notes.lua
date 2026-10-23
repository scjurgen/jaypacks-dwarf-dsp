-- Notes: the last notes that were played into the plugin, for scripts that build their sequences from them.
-- import "notes"
--
--   function OnNewPitch(note, hz, pitch, periodicity, level) Notes.remember(note) end
--
--   Notes.remember(note)       an accepted note (OnNewPitch); the same note again is remembered again (a new attack)
--   Notes.played()             how many played notes are remembered, 0 to Notes.size
--   Notes.last(i)              the i-th newest played note (1 = newest), nil if there is none
--   Notes.clear()              forget the played notes
--   Notes.window()             the notes to build from, oldest first: always Notes.size of them, the played ones
--                              preceded by the end of Notes.fallback while fewer than Notes.size were played, so a
--                              script sounds from the first step on and turns into the played notes one by one
--   Notes.pitchClasses()       the pitch classes (0 = C ... 11 = B) of the window, each once, ascending
--   Notes.has(pitchClass)      true if the window has a note of this pitch class
--   Notes.intervals(root)      the distances in semitones above `root` (0 to 11) of the pitch classes of the window,
--                              each once, ascending: 0 is the root itself
--   Notes.sequence(length)     a list of `length` notes: the window, repeated
--
-- Notes.size is 5 and Notes.fallback is a minor pentatonic (A3 C4 D4 E4 G4); a script may change both before the
-- first note. A set of five notes is not a key: nothing here detects one. What a script has already sent to the
-- filter keeps its destinations when the window changes (SetCutoff takes absolute notes).

Notes = {
    size = 5,
    fallback = {57, 60, 62, 64, 67},
    list = {},
}

function Notes.remember(note)
    Notes.list[#Notes.list + 1] = note
    while #Notes.list > Notes.size do
        table.remove(Notes.list, 1)
    end
end

function Notes.played()
    return #Notes.list
end

function Notes.last(i)
    return Notes.list[#Notes.list + 1 - i]
end

function Notes.clear()
    Notes.list = {}
end

function Notes.window()
    local window = {}
    local missing = Notes.size - #Notes.list
    for i = #Notes.fallback - missing + 1, #Notes.fallback do
        if i >= 1 then
            window[#window + 1] = Notes.fallback[i]
        end
    end
    for _, note in ipairs(Notes.list) do
        window[#window + 1] = note
    end
    return window
end

function Notes.pitchClasses()
    local seen = {}
    local classes = {}
    for _, note in ipairs(Notes.window()) do
        local class = math.floor(note) % 12
        if not seen[class] then
            seen[class] = true
            classes[#classes + 1] = class
        end
    end
    table.sort(classes)
    return classes
end

function Notes.has(pitchClass)
    for _, class in ipairs(Notes.pitchClasses()) do
        if class == pitchClass then
            return true
        end
    end
    return false
end

function Notes.intervals(root)
    local seen = {}
    local distances = {}
    for _, class in ipairs(Notes.pitchClasses()) do
        local distance = (class - math.floor(root)) % 12
        if not seen[distance] then
            seen[distance] = true
            distances[#distances + 1] = distance
        end
    end
    table.sort(distances)
    return distances
end

function Notes.sequence(length)
    local window = Notes.window()
    local result = {}
    for i = 1, length do
        result[i] = window[(i - 1) % #window + 1]
    end
    return result
end
