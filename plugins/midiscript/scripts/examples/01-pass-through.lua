-- Pass everything through
-- Every message goes on unchanged. Shows that a message only goes on when its hook calls Pass(): a message without a
-- hook (for example MIDI clock) is dropped. Start from this one when a script should change only some messages, and
-- remove the Pass() of the kind you want to handle yourself.
function OnNoteOn(channel, note, velocity) Pass() end
function OnNoteOff(channel, note, velocity) Pass() end
function OnCC(channel, controller, value) Pass() end
function OnPitchBend(channel, value) Pass() end
function OnProgramChange(channel, program) Pass() end
function OnOther(status, data1, data2) Pass() end
