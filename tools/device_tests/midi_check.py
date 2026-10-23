#!/usr/bin/env python3
"""Check on the Dwarf that MIDI reaches a plugin unchanged, including all 16 channels.

Needs the probe plugin installed with its MIDI input (tools/jp.py deploy probe). Saves the
loaded pedalboard, empties the graph, adds one probe, connects the pedalboard's MIDI input
(mod-ui's "All MIDI In", the JACK client mod-midi-merger that merges USB and DIN MIDI) to the
probe like a user would, and plays MIDI the way a USB controller delivers it: into the ALSA
sequencer of the device, from where jackd's bridge, the merger and mod-host carry it to the
plugin (device_tests/alsa_midi_send.py runs on the device). The probe counts the messages; this
script reads the counts over the mod-ui websocket, each time on a freshly loaded probe:
  1. one note on per channel 1 to 16: all 16 channels must show up,
  2. an MPE-style burst on member channels 2 to 16 only (note on, per-note pitch bend, channel
     pressure, CC74 on each) and nothing on channel 1: the channel mask must be exactly 0xFFFE
     and every message counted, the last one intact,
  3. a dense burst (500 messages, one per millisecond): none lost,
  4. a program change and a timing clock: counted or filtered is reported, not judged,
  5. a chain: the board's MIDI input to the first probe, its MIDI output to a second probe: the
     second one sees every message the first one saw (all 16 channels, a dense burst of 500),
  6. the host's worker: every note on becomes a job that the first probe's worker thread works and
     whose answer comes back (16 note ons, then 200 one millisecond apart).
Then leaves the probe loaded (with --restore: reloads the previous pedalboard).

Usage:
  midi_check.py [--restore]
"""

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import devicetest  # noqa: E402
import modui  # noqa: E402

URI = devicetest.URI_BASE + "probe"
INSTANCE = "probe"
SETTLE = 0.6
OUTPUTS = ("midiEvents", "midiChannels", "midiStatus", "midiData1", "midiData2")
CHAIN_OUTPUTS = OUTPUTS + ("midiSent", "midiDropped", "workerJobs", "workerAnswers", "workerNote")
SECOND = "probe2"


def fresh_probe(graph):
    """Loads a new probe (its counters start at 0) and wires the board's MIDI input to it.
    mod-host only feeds a plugin's MIDI input from the connections of the graph: events that
    another JACK client writes into the plugin's port directly do not arrive (found with this
    check), so the board is wired as in the web UI."""
    modui.reset()
    modui.add_plugin(INSTANCE, URI)
    modui.connect("midi_merger_out", f"{INSTANCE}/midiIn")
    graph.reader.poll(1.0)


def fresh_chain(graph):
    """As fresh_probe(), plus a second probe that is fed from the first one's MIDI output."""
    fresh_probe(graph)
    modui.add_plugin(SECOND, URI, x=400, y=200)
    modui.connect(f"{INSTANCE}/midiOut", f"{SECOND}/midiIn")
    graph.reader.poll(1.0)


def play_chain(graph, messages):
    """Plays the messages and returns the counters of the first and of the second probe."""
    report = modui.midi_send(messages)
    graph.reader.poll(SETTLE)
    first = {name: graph.out(INSTANCE, name) for name in CHAIN_OUTPUTS}
    second = {name: graph.out(SECOND, name) for name in CHAIN_OUTPUTS}
    return report, first, second


def play(graph, messages):
    """Plays the messages and returns the sender's report and the probe's counters."""
    report = modui.midi_send(messages)
    graph.reader.poll(SETTLE)
    return report, {name: graph.out(INSTANCE, name) for name in OUTPUTS}


def routing_left_alone():
    """True if the sender removed its temporary connection from the capture port to the merger."""
    return not modui.jack_connected("system:midi_capture_1", "mod-midi-merger:in")


def run(restore):
    if not modui.plugin_installed(URI):
        print(f"midi_check: {URI} is not installed (tools/jp.py deploy probe)")
        return 1
    check = devicetest.Check()
    with modui.preserved_pedalboard(restore) as board:
        print(f"midi_check: saved pedalboard {board or '(none)'}")
        graph = devicetest.Graph()
        try:
            fresh_probe(graph)
            ports = modui.jack_midi_inputs("midiIn")
            if len(ports) != 1:
                print(f"midi_check: expected one MIDI input port of the probe, found {ports}")
                return 1
            check.info(f"the probe's JACK port {ports[0]}")
            check.near("events before any MIDI", graph.out(INSTANCE, "midiEvents"), 0, 0)

            # 1. one note on per channel
            messages = [(10 + 5 * channel, [0x90 | channel, 60, 100]) for channel in range(16)]
            report, now = play(graph, messages)
            check.info(f"sender: {report}")
            check.near("events after 16 note ons", now["midiEvents"], 16, 0)
            check.near("channel mask after 16 note ons", now["midiChannels"], 0xFFFF, 0)

            # 2. MPE burst on the member channels only
            fresh_probe(graph)
            messages = []
            for channel in range(1, 16):
                base = 10 + 6 * channel
                messages += [(base, [0x90 | channel, 48 + channel, 90]),
                             (base + 1, [0xE0 | channel, channel * 4, 64 + channel]),
                             (base + 2, [0xD0 | channel, 70 + channel, 0]),
                             (base + 3, [0xB0 | channel, 74, 8 * channel])]
            report, now = play(graph, messages)
            check.info(f"sender: {report}")
            check.near("events after the MPE burst", now["midiEvents"], 60, 0)
            check.near("channel mask after the MPE burst (channels 2 to 16)", now["midiChannels"], 0xFFFE, 0)
            check.near("last message status (CC on channel 16)", now["midiStatus"], 0xBF, 0)
            check.near("last message data 1 (CC74)", now["midiData1"], 74, 0)
            check.near("last message data 2", now["midiData2"], 8 * 15, 0)

            # 3. a dense burst
            fresh_probe(graph)
            messages = [(10 + k, [0xE0 | (1 + k % 15), k % 128, (k * 3) % 128]) for k in range(500)]
            report, now = play(graph, messages)
            check.info(f"sender: {report}")
            check.near("events after 500 messages in 500 ms", now["midiEvents"], 500, 0)
            check.near("last pitch bend data 2", now["midiData2"], (499 * 3) % 128, 0)

            # 4. program change and timing clock: reported only
            fresh_probe(graph)
            report, now = play(graph, [(10, [0xC0 | 4, 7, 0]), (20, [0xF8, 0, 0])])
            check.info(f"program change and timing clock: {now['midiEvents']:.0f} of 2 reached the plugin "
                       f"(last status {now['midiStatus']:.0f})")

            # 5. a chain of two probes through the MIDI output of the first
            fresh_chain(graph)
            messages = [(10 + 5 * channel, [0x90 | channel, 40 + channel, 100]) for channel in range(16)]
            report, first, second = play_chain(graph, messages)
            check.info(f"sender: {report}")
            check.near("first probe: events passed on", first["midiSent"], 16, 0)
            check.near("first probe: events dropped by the host's buffer", first["midiDropped"], 0, 0)
            check.near("second probe: events after 16 note ons", second["midiEvents"], 16, 0)
            check.near("second probe: channel mask", second["midiChannels"], 0xFFFF, 0)
            check.near("second probe: last note", second["midiData1"], 40 + 15, 0)
            # 6. the worker: every note on is a job, every job comes back
            check.near("first probe: worker jobs after 16 note ons", first["workerJobs"], 16, 0)
            check.near("first probe: worker answers after 16 note ons", first["workerAnswers"], 16, 0)
            check.near("first probe: note of the last answer", first["workerNote"], 40 + 15, 0)

            fresh_chain(graph)
            messages = [(10 + k, [0xE0 | (1 + k % 15), k % 128, (k * 3) % 128]) for k in range(500)]
            report, first, second = play_chain(graph, messages)
            check.info(f"sender: {report}")
            check.near("first probe: 500 messages passed on", first["midiSent"], 500, 0)
            check.near("second probe: 500 messages received", second["midiEvents"], 500, 0)
            check.near("second probe: last pitch bend data 2", second["midiData2"], (499 * 3) % 128, 0)

            fresh_chain(graph)
            messages = [(10 + k, [0x90 | (k % 16), 20 + k % 100, 90]) for k in range(200)]
            report, first, second = play_chain(graph, messages)
            check.info(f"sender: {report}")
            check.near("first probe: worker jobs after 200 note ons in 200 ms", first["workerJobs"], 200, 0)
            check.near("first probe: worker answers after 200 note ons in 200 ms", first["workerAnswers"], 200, 0)
            check.near("first probe: note of the last answer", first["workerNote"], 20 + 199 % 100, 0)
            check.near("second probe: 200 note ons received", second["midiEvents"], 200, 0)

            check.record(routing_left_alone(), "the sender removed its temporary connection to the merger")
        finally:
            log = graph.save_log("midi_check.log", graph.reader.log[0][0] if graph.reader.log else 0.0)
            graph.close()
            check.info(f"raw messages: {log}")
    status = check.report("midi_check")
    print("midi_check: " + ("previous pedalboard restored" if restore else "probe left loaded"))
    return status


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--restore", action="store_true", help="reload the previous pedalboard afterwards")
    args = parser.parse_args(argv)
    return run(args.restore)


if __name__ == "__main__":
    sys.exit(main())
