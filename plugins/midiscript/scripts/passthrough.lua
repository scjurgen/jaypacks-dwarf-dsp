-- Passthrough: every message goes out unchanged (one latency later). The starting point for a script of your own.

function OnNoteOn(channel, note, velocity) Pass() end
function OnNoteOff(channel, note, velocity) Pass() end
function OnCC(channel, controller, value) Pass() end
function OnPitchBend(channel, value) Pass() end
function OnProgramChange(channel, program) Pass() end
function OnOther(status, data1, data2) Pass() end
