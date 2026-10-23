#!/usr/bin/env python3
"""Device integration test of midiscript on the Dwarf (Lua scripting step 9).

Needs midiscript, probe and morphexsynth installed (tools/jp.py test-device midiscript deploys midiscript). Empties the
graph and builds
  mod-ui's MIDI input (mod-midi-merger) -> midiscript/midiIn, midiscript/midiOut -> probe/midiIn
and plays MIDI the way a USB controller does (ALSA sequencer, tools/device_tests/alsa_midi_send.py). The probe counts
what comes out of the plugin and its output values are read over the mod-ui websocket while the sender runs, so every
message has the time at which the host reported it (about 12 ms of jitter, no bias). Checks:
  - the bundle ships the six scripts, the three libraries and API.md,
  - Passthrough: one note on per channel 1 to 16, a dense burst of 500 messages, nothing late and nothing dropped (with
    the default latency of 12 ms), notes that are on end with their note off,
  - the latency the Dwarf needs: bursts at latencies of 0 to 12 ms and the number of messages that came late (reported),
  - the Arpeggiator on the board's tempo (the websocket's transport-bpm 120, 90 and 60): the pitch sequence 60, 64, 67
    exactly, the mean interval between steps within 2 % of a sixteenth note, every note on has its note off, nothing
    late, nothing dropped, sounding 0 afterwards,
  - Sync = Transport: no clock while the transport is stopped, a clock while it rolls (transport-rolling), none after it
    stops,
  - scripts: a user script written over ssh is loaded, an edited file is reloaded within 1.2 s and the notes the old
    script left on end with their note offs, a broken file is rejected (status 3, the log says so) while the previous
    script keeps playing, a missing file reports status 2, the Panic control ends all notes and sends All Notes Off on
    all channels (user slot 8 only; a script that is there is saved and restored),
  - CPU: mean mod-ui CPU with the plugin idle and with the arpeggio running against the same graph without it,
  - xruns unchanged while the graph processes, no problem lines in the jack2 journal,
then builds the board for playing (MIDI in -> midiscript -> morphexsynth -> outputs, knob 1 = Script, knob 2 =
Latency; with --restore: reloads the previous pedalboard). Raw messages go to build-dwarf/scratch/test_midiscript.log.

Usage:
  test_midiscript.py [--restore]
"""

import argparse
import statistics
import subprocess
import sys
import threading
import time

import devicetest
from devicetest import URI_BASE, modui

PREFIX = "test_midiscript"
PLUGIN = "midiscript"
PROBE = "probe"
SYNTH = "morphexsynth"
BUNDLE_FOLDER = f"/root/.lv2/jaypacks-{PLUGIN}.lv2"
USER_FOLDER = f"/data/jaypacks/scripts/{PLUGIN}"
TEST_SLOT = 8
TEST_SCRIPT = f"{USER_FOLDER}/user-{TEST_SLOT}.lua"
TEST_LOG = f"{USER_FOLDER}/user-{TEST_SLOT}.log"
RELOAD_WAIT = 1.2
SCRIPT_ARPEGGIATOR = 1
SCRIPT_USER = 5 + TEST_SLOT  # the entry of the Script menu
NOTE_ON, NOTE_OFF, CONTROL_CHANGE, PITCH_BEND = 0x90, 0x80, 0xB0, 0xE0
CPU_WINDOW = 5.0
CPU_ARP_CEILING_POINTS = 15.0
INTERVAL_TOLERANCE = 0.02
CHORD = (60, 64, 67)
HOLD_ECHO = "function OnNoteOn(channel, note, velocity) NoteOn(channel, note, velocity) end\n"
SILENT = "function OnNoteOn(channel, note, velocity) end\n"


def wire(graph):
    """A fresh pair of plugins (counters at 0): the board's MIDI -> midiscript -> probe."""
    modui.reset()
    modui.add_plugin(PLUGIN, URI_BASE + PLUGIN, x=100, y=200)
    modui.add_plugin(PROBE, URI_BASE + PROBE, x=450, y=200)
    modui.connect("midi_merger_out", f"{PLUGIN}/midiIn")
    modui.connect(f"{PLUGIN}/midiOut", f"{PROBE}/midiIn")
    graph.reader.poll(1.5)


def board_tempo(graph, bpm, rolling=None):
    graph.ws.send(f"transport-bpm {bpm}")
    if rolling is not None:
        graph.ws.send(f"transport-rolling {1 if rolling else 0}")
    graph.reader.poll(0.6)


def write_remote(path, text):
    folder = path.rsplit("/", 1)[0]
    subprocess.run(["ssh", *modui.SSH_OPTS, f"root@{modui.HOST}", f"mkdir -p {folder} && cat > {path}"], input=text,
                   text=True, check=True, timeout=30)


def read_remote(path):
    try:
        return modui.ssh(f"cat {path} 2>/dev/null; true")
    except modui.DeviceError:
        return ""


class Run:
    """What the probe reported while a MIDI schedule was played: (seconds, status, data1, data2) per message that the
    host showed (messages that arrive within one update of the host show only the last one: only the counters are exact
    then), and the probe's counters at the end."""

    def __init__(self, events, total, sender):
        self.events = events
        self.total = total
        self.sender = sender

    def of(self, status):
        return [event for event in self.events if event[1] & 0xF0 == status]

    def times(self, status):
        return [event[0] for event in self.of(status)]


def play(graph, messages, seconds, settle=1.0):
    """Plays the schedule (ALSA sender on a thread of its own) while the websocket is read, for `seconds` after it
    started and `settle` more; returns a Run."""
    outcome = {}

    def send():
        try:
            outcome["report"] = modui.midi_send(messages)
        except modui.DeviceError as error:
            outcome["report"] = f"failed: {error}"

    first = len(graph.reader.log)
    state = {name: graph.out(PROBE, name) or 0.0 for name in ("midiStatus", "midiData1", "midiData2", "midiEvents")}
    before = state["midiEvents"]
    thread = threading.Thread(target=send)
    start = time.monotonic()
    thread.start()
    graph.reader.poll(seconds)
    while thread.is_alive():  # the ssh start-up of the sender takes a second or more
        graph.reader.poll(0.1)
    thread.join()
    graph.reader.poll(settle)
    events = []
    for arrival, message in graph.reader.log[first:]:
        parts = message.split()
        if len(parts) < 4 or parts[0] != "output_set" or parts[1] != f"/graph/{PROBE}" or parts[2] not in state:
            continue
        value = float(parts[3])
        if parts[2] == "midiEvents":
            if value > state["midiEvents"]:
                events.append((arrival - start, int(state["midiStatus"]), int(state["midiData1"]),
                               int(state["midiData2"])))
            state["midiEvents"] = value
        else:
            state[parts[2]] = value
    total = (graph.out(PROBE, "midiEvents") or 0.0) - before
    return Run(events, total, outcome.get("report"))


def status(graph):
    return {name: graph.out(PLUGIN, name) for name in ("scriptStatus", "sounding", "late", "dropped")}


def hold(notes, milliseconds, channel=1, velocity=100):
    """Note ons at the start, the note offs after `milliseconds`."""
    on = [(20 + 5 * i, [NOTE_ON | (channel - 1), note, velocity]) for i, note in enumerate(notes)]
    off = [(milliseconds + 5 * i, [NOTE_OFF | (channel - 1), note, 64]) for i, note in enumerate(notes)]
    return on + off


def check_bundle(check):
    listing = modui.ssh(f"ls {BUNDLE_FOLDER}/scripts 2>&1; true").split()
    names = ["passthrough", "arpeggiator", "chord-player", "scale-quantiser", "octave-doubler", "cc-remap", "scales",
             "chords", "rhythm"]
    check.record(all(f"{name}.lua" in listing for name in names), "the bundle ships the six scripts and the libraries")
    check.record("API.md" in listing and "constants.lua" in listing, "the bundle ships API.md and constants.lua")
    title = modui.ssh(f"head -1 {BUNDLE_FOLDER}/scripts/API.md 2>&1; true").strip()
    check.record(title == "# MIDI Script scripting API", f"API.md starts with its title ({title!r})")


def check_passthrough(graph, check):
    wire(graph)
    check.near("script status of the default script", graph.out(PLUGIN, "scriptStatus"), 1.0, 0.0)
    messages = [(10 + 5 * c, [NOTE_ON | c, 40 + c, 100]) for c in range(16)]
    messages += [(200 + 5 * c, [NOTE_OFF | c, 40 + c, 64]) for c in range(16)]
    run = play(graph, messages, 0.5)
    check.info(f"sender: {run.sender}")
    check.near("passthrough: 16 note ons and 16 note offs on 16 channels reached the probe", run.total, 32, 0)
    check.near("passthrough: channel mask", graph.out(PROBE, "midiChannels"), 0xFFFF, 0)
    now = status(graph)
    check.near("passthrough: notes sounding afterwards", now["sounding"], 0, 0)
    check.near("passthrough: late", now["late"], 0, 0)
    check.near("passthrough: dropped", now["dropped"], 0, 0)

    wire(graph)
    messages = [(10 + k, [PITCH_BEND | (1 + k % 15), k % 128, (k * 3) % 128]) for k in range(500)]
    run = play(graph, messages, 0.7)
    check.info(f"sender: {run.sender}")
    check.near("passthrough: 500 messages in 500 ms reached the probe", run.total, 500, 0)
    now = status(graph)
    check.near("passthrough: 500 messages, late", now["late"], 0, 0)
    check.near("passthrough: 500 messages, dropped", now["dropped"], 0, 0)
    check.near("passthrough: last pitch bend data 2", graph.out(PROBE, "midiData2"), (499 * 3) % 128, 0)


def check_latency(graph, check):
    """How late do messages come at a latency: the worker's round trip on the Dwarf. Reported, only the default is judged."""
    for latency in (0, 2, 4, 6, 8, 12):
        wire(graph)
        graph.set(PLUGIN, {"latency": latency})
        graph.reader.poll(0.3)
        messages = [(10 + 4 * k, [NOTE_ON | (k % 16), 30 + k % 90, 90]) for k in range(200)]
        messages += [(900 + 4 * k, [NOTE_OFF | (k % 16), 30 + k % 90, 64]) for k in range(200)]
        run = play(graph, messages, 1.8, settle=0.5)
        now = status(graph)
        check.info(f"latency {latency:2d} ms: {run.total:.0f} of 400 messages, late {now['late']:.0f}, "
                   f"dropped {now['dropped']:.0f}, sounding {now['sounding']:.0f}")
        if latency == 12:
            check.near("latency 12 ms (the default): late", now["late"], 0, 0)
            check.near("latency 12 ms: messages out", run.total, 400, 0)


def step_intervals(times):
    return [b - a for a, b in zip(times, times[1:])]


def check_arpeggio(graph, check):
    for bpm in (120, 90, 60):
        wire(graph)
        board_tempo(graph, bpm, rolling=False)
        graph.set(PLUGIN, {"script": SCRIPT_ARPEGGIATOR})
        graph.reader.poll(1.0)
        check.near(f"arpeggiator at {bpm} BPM: script status", graph.out(PLUGIN, "scriptStatus"), 1.0, 0.0)
        run = play(graph, hold(CHORD, 6000), 6.2, settle=1.0)
        ons = run.of(NOTE_ON)
        offs = run.of(NOTE_OFF)
        step = 60.0 / bpm / 4.0
        check.above(f"arpeggiator at {bpm} BPM: steps played", len(ons), 5.0 / step - 3)
        pitches = [event[2] for event in ons]
        check.record(pitches == [CHORD[i % 3] for i in range(len(pitches))] and len(pitches) > 6,
                     f"arpeggiator at {bpm} BPM: the pitches go {CHORD} round and round ({pitches[:7]} ...)")
        intervals = step_intervals([event[0] for event in ons])
        if intervals:
            mean = sum(intervals) / len(intervals)
            check.near(f"arpeggiator at {bpm} BPM: mean time between steps (ms)", mean * 1000.0, step * 1000.0,
                       step * 1000.0 * INTERVAL_TOLERANCE)
            check.info(f"arpeggiator at {bpm} BPM: steps {mean * 1000.0:.1f} ms apart (median "
                       f"{statistics.median(intervals) * 1000.0:.1f}, the host reports in steps of about 12 ms)")
        check.near(f"arpeggiator at {bpm} BPM: every note on has its note off", len(ons), len(offs), 1)
        check.near(f"arpeggiator at {bpm} BPM: probe counted what the log showed", run.total, len(ons) + len(offs), 1)
        now = status(graph)
        check.near(f"arpeggiator at {bpm} BPM: sounding afterwards", now["sounding"], 0, 0)
        check.near(f"arpeggiator at {bpm} BPM: late", now["late"], 0, 0)
        check.near(f"arpeggiator at {bpm} BPM: dropped", now["dropped"], 0, 0)
    board_tempo(graph, 120)


def check_transport(graph, check):
    wire(graph)
    board_tempo(graph, 120, rolling=False)
    graph.set(PLUGIN, {"script": SCRIPT_ARPEGGIATOR, "sync": 1})
    graph.reader.poll(1.0)
    messages = hold(CHORD, 9000)
    outcome = {}
    thread = threading.Thread(target=lambda: outcome.update(report=modui.midi_send(messages)))
    thread.start()
    start = time.monotonic()
    graph.reader.poll(3.5)  # the sender needs a second or more to start; its keys are down for about 2 s of this
    stopped = graph.out(PROBE, "midiEvents") or 0.0
    board_tempo(graph, 120, rolling=True)
    mark_roll = time.monotonic() - start
    graph.reader.poll(2.5)
    rolling = (graph.out(PROBE, "midiEvents") or 0.0) - stopped
    board_tempo(graph, 120, rolling=False)
    mark_stop = time.monotonic() - start
    graph.reader.poll(0.5)
    after_stop = graph.out(PROBE, "midiEvents") or 0.0
    graph.reader.poll(2.0)
    later = (graph.out(PROBE, "midiEvents") or 0.0) - after_stop
    thread.join()
    graph.reader.poll(1.0)
    check.near("sync to the transport: messages while it is stopped (keys held)", stopped, 0, 0)
    check.above("sync to the transport: messages in 2.5 s while it rolls", rolling, 12)
    check.near("sync to the transport: messages in the 2 s after it stopped (0.5 s grace)", later, 0, 0)
    check.info(f"transport rolled at {mark_roll:.1f} s and stopped at {mark_stop:.1f} s after the start of the sender; "
               f"{rolling:.0f} messages in 2.5 s of rolling")
    now = status(graph)
    check.near("sync to the transport: sounding afterwards", now["sounding"], 0, 0)
    graph.set(PLUGIN, {"sync": 0})


def played(graph, messages, seconds=0.5):
    """The probe's message count for a schedule of MIDI."""
    return play(graph, messages, seconds).total


def check_user_scripts(graph, check):
    saved = read_remote(TEST_SCRIPT)
    saved_log = read_remote(TEST_LOG)
    notes = [(20, [NOTE_ON, 60, 100]), (25, [NOTE_ON | 1, 64, 100]), (30, [NOTE_ON | 2, 67, 100])]
    try:
        wire(graph)
        write_remote(TEST_SCRIPT, HOLD_ECHO)
        graph.set(PLUGIN, {"script": SCRIPT_USER})
        graph.reader.poll(RELOAD_WAIT)
        check.near("script status of a user script", graph.out(PLUGIN, "scriptStatus"), 1.0, 0.0)
        check.near("a user script plays: note ons at the probe", played(graph, notes), 3, 0)
        check.near("notes left on by the script", graph.out(PLUGIN, "sounding"), 3, 0)
        before = graph.out(PROBE, "midiEvents") or 0.0

        write_remote(TEST_SCRIPT, SILENT)
        graph.reader.poll(RELOAD_WAIT + 0.5)
        check.near("an edited file is reloaded: script status", graph.out(PLUGIN, "scriptStatus"), 1.0, 0.0)
        check.near("a reload ends the notes the old script left on", graph.out(PLUGIN, "sounding"), 0, 0)
        check.near("a reload sends their note offs", (graph.out(PROBE, "midiEvents") or 0.0) - before, 3, 0)

        # a file that does not load changes nothing: the script that was running keeps playing
        write_remote(TEST_SCRIPT, HOLD_ECHO)
        graph.reader.poll(RELOAD_WAIT)
        write_remote(TEST_SCRIPT, "function OnNoteOn( oops\n")
        graph.reader.poll(RELOAD_WAIT)
        check.near("script status of a broken file (3: compile error)", graph.out(PLUGIN, "scriptStatus"), 3.0, 0.0)
        log = read_remote(TEST_LOG)
        check.record("compile error" in log and f"user-{TEST_SLOT}:" in log,
                     "the log file next to the script names the error and the line (user-8:<line>:)")
        check.near("the previous script keeps playing while the file is broken: a note on at the probe",
                   played(graph, [(20, [NOTE_ON, 72, 100])]), 1, 0)
        check.near("... and nothing was ended", graph.out(PLUGIN, "sounding"), 1, 0)

        # a panic ends everything that sounds and tells all channels
        write_remote(TEST_SCRIPT, HOLD_ECHO)
        graph.reader.poll(RELOAD_WAIT)
        check.near("script status after the file is fixed", graph.out(PLUGIN, "scriptStatus"), 1.0, 0.0)
        played(graph, notes)
        check.near("notes sounding before the panic", graph.out(PLUGIN, "sounding"), 3, 0)
        before = graph.out(PROBE, "midiEvents") or 0.0
        graph.set(PLUGIN, {"panic": 1})
        graph.reader.poll(0.6)
        graph.set(PLUGIN, {"panic": 0})
        check.near("panic: sounding afterwards", graph.out(PLUGIN, "sounding"), 0, 0)
        check.near("panic: 3 note offs and All Notes Off on 16 channels reached the probe",
                   (graph.out(PROBE, "midiEvents") or 0.0) - before, 19, 0)

        # a hook that fails: the notes end, that call says nothing, the script goes on
        write_remote(TEST_SCRIPT, "local n = 0\nfunction OnNoteOn(channel, note, velocity)\n    n = n + 1\n"
                                  "    NoteOn(channel, note, velocity)\n    if n == 2 then error('boom') end\nend\n")
        graph.reader.poll(RELOAD_WAIT)
        before = graph.out(PROBE, "midiEvents") or 0.0
        played(graph, [(20, [NOTE_ON, 60, 100]), (400, [NOTE_ON, 61, 100])], 0.8)
        check.near("a failing hook: script status (4: runtime error)", graph.out(PLUGIN, "scriptStatus"), 4.0, 0.0)
        check.near("a failing hook: the note that was on has ended", graph.out(PLUGIN, "sounding"), 0, 0)
        check.near("a failing hook: note on, its note off, nothing from the failed call",
                   (graph.out(PROBE, "midiEvents") or 0.0) - before, 2, 0)
        check.record("a hook failed" in read_remote(TEST_LOG), "the log says that a hook failed")

        modui.ssh(f"rm -f {TEST_SCRIPT}")
        graph.reader.poll(RELOAD_WAIT)
        check.near("script status when the file is removed (2: file missing)", graph.out(PLUGIN, "scriptStatus"), 2.0, 0.0)
        graph.set(PLUGIN, {"script": 0})
        graph.reader.poll(0.8)
        check.near("script status back with Passthrough", graph.out(PLUGIN, "scriptStatus"), 1.0, 0.0)
    finally:
        if saved:
            write_remote(TEST_SCRIPT, saved)
            if saved_log:
                write_remote(TEST_LOG, saved_log)
        else:
            modui.ssh(f"rm -f {TEST_SCRIPT} {TEST_LOG}; true")


def mean_cpu(graph, seconds):
    start = time.monotonic()
    graph.reader.poll(seconds)
    cpu, _ = devicetest.stats_from_log(graph.reader.log, since=start)
    return sum(cpu) / len(cpu) if cpu else None


def check_cpu(graph, check):
    modui.reset()
    modui.add_plugin(PROBE, URI_BASE + PROBE, x=450, y=200)
    graph.reader.poll(1.5)
    base = mean_cpu(graph, CPU_WINDOW)
    wire(graph)
    board_tempo(graph, 120, rolling=False)
    idle = mean_cpu(graph, CPU_WINDOW)
    graph.set(PLUGIN, {"script": SCRIPT_ARPEGGIATOR})
    graph.reader.poll(1.0)
    outcome = {}
    thread = threading.Thread(target=lambda: outcome.update(report=modui.midi_send(hold(CHORD, 9000))))
    thread.start()
    graph.reader.poll(2.0)
    busy = mean_cpu(graph, CPU_WINDOW)
    thread.join()
    if base is None or idle is None or busy is None:
        check.record(False, "no stats messages for the CPU comparison")
        return
    check.info(f"cpu with the probe alone {base:.1f} %, with the idle plugin {idle:.1f} %, with an arpeggio of "
               f"8 notes a second {busy:.1f} %")
    check.below("cpu added by the idle plugin (percentage points)", idle - base, CPU_ARP_CEILING_POINTS)
    check.below("cpu added by the running arpeggio (percentage points)", busy - base, CPU_ARP_CEILING_POINTS)


def board_for_playing(check):
    modui.reset()
    modui.add_plugin(PLUGIN, URI_BASE + PLUGIN, x=100, y=200)
    modui.add_plugin(SYNTH, URI_BASE + SYNTH, x=450, y=200)
    modui.connect("midi_merger_out", f"{PLUGIN}/midiIn")
    modui.connect(f"{PLUGIN}/midiOut", f"{SYNTH}/midiIn")
    modui.connect(f"{SYNTH}/outL", "playback_1")
    modui.connect(f"{SYNTH}/outR", "playback_2")
    modui.address(f"/graph/{PLUGIN}/script", "/hmi/knob1", "Script", 0.0, 13.0, 0.0, 14)
    modui.address(f"/graph/{PLUGIN}/latency", "/hmi/knob2", "Latency", 0.0, 50.0, 12.0, 51)
    fresh = modui.WebSocket()
    try:
        maps = {modui.parse_hw_map(message) for message in fresh.drain(2.0)}
    finally:
        fresh.close()
    check.record((PLUGIN, "script", "/hmi/knob1") in maps, "hw_map: knob 1 -> script")
    check.record((PLUGIN, "latency", "/hmi/knob2") in maps, "hw_map: knob 2 -> latency")


def run(restore):
    missing = devicetest.missing_plugins([PLUGIN, PROBE, SYNTH])
    if missing:
        print(f"{PREFIX}: not installed: {missing} (tools/jp.py test-device {PLUGIN} deploys {PLUGIN})")
        return 1
    check = devicetest.Check()
    with modui.preserved_pedalboard(restore) as board:
        print(f"{PREFIX}: pedalboard before the test: {board or '(none)'}")
        cursor = devicetest.journal_cursor()
        modui.reset()
        graph = devicetest.Graph()
        start = time.monotonic()
        processing = start
        step_starts = []
        try:
            graph.reader.poll(1.0)
            check_bundle(check)
            processing = time.monotonic()
            steps = [
                ("passthrough", lambda: check_passthrough(graph, check)),
                ("latency", lambda: check_latency(graph, check)),
                ("arpeggio", lambda: check_arpeggio(graph, check)),
                ("transport", lambda: check_transport(graph, check)),
                ("user scripts", lambda: check_user_scripts(graph, check)),
                ("cpu", lambda: check_cpu(graph, check)),
            ]
            for name, step in steps:
                print(f"{PREFIX}: {name} ...", flush=True)
                step_starts.append((time.monotonic(), name))
                try:
                    step()
                except modui.DeviceError as error:
                    check.record(False, f"{name}: {error}")
            graph.reader.poll(1.0)
        finally:
            try:
                board_tempo(graph, 120, rolling=False)
            except Exception:  # noqa: BLE001 - the tempo is only put back
                pass
            log = graph.save_log(f"{PREFIX}.log", start)
            graph.close()
        _, xruns = devicetest.stats_from_log(graph.reader.log)
        _, processing_xruns = devicetest.stats_from_log(graph.reader.log, since=processing)
        if xruns and processing_xruns:
            check.info(f"xruns over the whole run {xruns[0]} -> {xruns[-1]} (graph changes included)")
            previous = None
            for arrival, message in graph.reader.log:
                parts = message.split()
                if parts and parts[0] == "stats" and len(parts) >= 3:
                    if previous is not None and int(float(parts[2])) != previous:
                        step = [name for began, name in step_starts if began <= arrival]
                        check.info(f"xruns {previous} -> {int(float(parts[2]))} reported at {arrival - start:.1f} s, "
                                   f"during step '{step[-1] if step else 'bundle'}'")
                    previous = int(float(parts[2]))
            check.record(processing_xruns[-1] == processing_xruns[0],
                         f"xruns while processing {processing_xruns[0]} -> {processing_xruns[-1]} (expected unchanged)")
        else:
            check.record(False, "no stats messages: xruns unknown")
        lines = devicetest.journal_lines(cursor)
        problems = devicetest.journal_problems(lines)
        check.record(not problems, f"jack2 journal: {len(problems)} problem lines among {len(lines)} during the run")
        for line in problems[:10]:
            check.info(f"journal: {line}")
        try:
            board_for_playing(check)
        except modui.DeviceError as error:
            check.record(False, f"board for playing: {error}")
    check.info(f"raw messages: {log}")
    check.info("previous pedalboard restored" if restore
               else "board left loaded for playing (MIDI in -> midiscript -> morphexsynth -> outputs, knob 1 = Script, "
                    "knob 2 = Latency)")
    return check.report(PREFIX)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--restore", action="store_true", help="reload the previous pedalboard afterwards")
    args = parser.parse_args(argv)
    return run(args.restore)


if __name__ == "__main__":
    sys.exit(main())
