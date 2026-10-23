-- All notes off when the transport stops
-- Notes go through, and when the host's transport stops every sounding note is ended. Shows OnTransportStop and Panic.
-- Panic also drops everything that was waiting, so it is the right answer to a hung note.
function OnNoteOn(channel, note, velocity) Pass() end
function OnNoteOff(channel, note, velocity) Pass() end

function OnTransportStop()
    Panic()
end
