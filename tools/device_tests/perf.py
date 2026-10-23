#!/usr/bin/env python3
"""Performance of jaypacks plugins on the MOD Dwarf: CPU and memory, written to
plugins/<name>/performance.json, and the tables in README.md and docs/plugins/<name>.md
rewritten from them (tools/performance.py). Run by tools/jp.py perf, which deploys first.

The device's hardware and firmware are the same on every Dwarf, so one measurement holds for
all of them until the plugin's DSP changes.

Method, per plugin, repeated for a number of cycles:
  - the graph holds only the source: siggen with white noise at -12 dBFS, connected to every
    audio input of the plugin (siggen itself is measured on an empty graph);
  - CPU without the plugin: mean of mod-ui's "stats" messages (jackd's DSP load in percent,
    about one per second) over one window;
  - the plugin is added and connected (a plugin with a MIDI input also to mod-ui's MIDI input);
  - CPU with the plugin, one window per setting: the defaults, then every preset that differs
    from them; for a plugin with a MIDI input the defaults without a note, then the scenarios of
    MIDI_SCENARIOS (a number of notes held on the channel 1 with the settings of a patch, played
    the way a USB controller does and released after the window);
  - the plugin is removed (except after the last cycle: the graph stays loaded for the web UI,
    --restore reloads the previous pedalboard instead).
The CPU cost of a setting is the mean with the plugin minus the mean without it in the same
cycle, in percentage points; the JSON keeps the median and the range over the cycles. The
figure varies by a few tenths between windows, so a cheap plugin can read slightly below 0.
Xruns are counted only inside the windows (not during graph changes). Memory is what the
plugin allocates, counted on the Mac by its memory probe (tools/performance.py probe_memory):
jackd's growth on the device shows a few kB for every plugin, because its allocator serves
a new plugin from memory freed earlier in the process (docs/device.md).

Usage:
  perf.py <plugin>... [--cycles N] [--window SECONDS] [--restore]
"""

import argparse
import datetime
import json
import statistics
import subprocess
import sys
import time

import devicetest
from devicetest import REPO_DIR, URI_BASE, WHITE_NOISE, modui

sys.path.insert(0, str(REPO_DIR / "tools"))
import performance  # noqa: E402

PREFIX = "perf"
SOURCE = "source"
SETTLE = 1.5
NOTE_ON, CONTROL_CHANGE = 0x90, 0xB0
# per plugin with a MIDI input: (label, settings, number of notes held); the notes are 36, 40, 44 ...
# on channel 1. The patches are ones that hold their level (sustain 0.8 or more), so that the load
# stays the same during the window
MIDI_SCENARIOS = {
    "morphexsynth": [
        ("1 note, Retro Poly", {"patch": 7}, 1),
        ("4 notes, Retro Poly", {"patch": 7}, 4),
        ("12 notes, Retro Poly", {"patch": 7}, 12),
        ("12 notes, Cutting Lead", {"patch": 3}, 12),
        ("12 notes, Airy Pad", {"patch": 1}, 12),
        ("12 notes, Section Brass", {"patch": 8}, 12),
        ("12 notes, Warm Bass", {"patch": 9}, 12),
    ],
    # the plugin's output goes nowhere here; the notes are held keys: the arpeggiator plays them as 16th notes at the
    # board's tempo, the chord player turns each into a chord, the quantiser and the doubler pass them
    "midiscript": [
        ("Arpeggiator, 3 keys held", {"script": 1}, 3),
        ("Arpeggiator, 8 keys held", {"script": 1}, 8),
        ("Chord Player, 8 keys", {"script": 2}, 8),
        ("Scale Quantiser, 8 keys", {"script": 3}, 8),
        ("Octave Doubler, 8 keys", {"script": 4}, 8),
    ],
}


def plugin_spec(name):
    return json.loads((REPO_DIR / "plugins" / name / f"{name}.json").read_text())


def settings_list(name, spec):
    """[(label, {symbol: value}, notes)]: the defaults, then each preset that differs from them; for a
    plugin with a MIDI input the defaults without a note ("idle"), then its MIDI_SCENARIOS."""
    defaults = {port["symbol"]: port["default"] for port in spec.get("ports-control", [])}
    if spec.get("midi-in"):
        result = [("idle (no note)", defaults, 0)]
        for label, values, notes in MIDI_SCENARIOS.get(name, []):
            result.append((label, dict(defaults, **values), notes))
        return result
    result = [("defaults", defaults, 0)]
    for preset in spec.get("presets", []):
        values = dict(defaults, **preset["values"])
        if values != defaults:
            result.append((f"preset {preset['name']}", values, 0))
    return result


def play_notes(count):
    """Strikes count notes on channel 1 the way a USB controller does."""
    modui.midi_send([(20 + 5 * i, [NOTE_ON, 36 + 4 * i, 100]) for i in range(count)])


def release_notes():
    modui.midi_send([(5, [CONTROL_CHANGE, 123, 0])])


def device_info():
    firmware = modui.ssh("head -n 1 /etc/mod-release/release").strip()
    command = modui.ssh("tr '\\0' ' ' < /proc/$(pidof jackd)/cmdline").split()
    rate = command[command.index("-r") + 1] if "-r" in command else "?"
    period = command[command.index("-p") + 1] if "-p" in command else "?"
    return {"firmware": f"MOD OS {firmware}", "jack": f"{rate} Hz, {period} frames"}


def binary_kb(name):
    size = modui.ssh(f"stat -c %s /root/.lv2/jaypacks-{name}.lv2/jaypacks-{name}.so").strip()
    return round(int(size) / 1024)


def repo_commit():
    commit = subprocess.run(["git", "-C", str(REPO_DIR), "rev-parse", "--short", "HEAD"],
                            capture_output=True, text=True).stdout.strip()
    dirty = subprocess.run(["git", "-C", str(REPO_DIR), "status", "--porcelain", "--untracked-files=no"],
                           capture_output=True, text=True).stdout.strip()
    return commit + ("+changes" if dirty else "")


class Window:
    """Mean CPU and xrun change of the stats messages during one window."""

    def __init__(self, graph, seconds):
        start = time.monotonic()
        graph.reader.poll(seconds)
        cpu, xruns = devicetest.stats_from_log(graph.reader.log, since=start)
        if not cpu:
            raise modui.DeviceError("no stats messages from mod-ui")
        self.cpu = sum(cpu) / len(cpu)
        self.xruns = xruns[-1] - xruns[0] if xruns else 0


def add_under_test(name, spec):
    modui.add_plugin(name, URI_BASE + name, x=350, y=200)
    if name != "siggen":
        for port in spec.get("audio-ports-in", []):
            modui.connect(f"{SOURCE}/out", f"{name}/{port['symbol']}")
    if spec.get("midi-in"):
        modui.connect("midi_merger_out", f"{name}/midiIn")


def measure(name, cycles, window, restore):
    spec = plugin_spec(name)
    settings = settings_list(name, spec)
    has_source = name != "siggen" and bool(spec.get("audio-ports-in"))
    cpu = {label: [] for label, _, _ in settings}
    xruns = 0
    with modui.preserved_pedalboard(restore):
        modui.reset()
        if has_source:
            modui.add_plugin(SOURCE, URI_BASE + "siggen", x=50, y=200)
        graph = devicetest.Graph()
        try:
            if has_source:
                graph.set(SOURCE, {"waveform": WHITE_NOISE, "level": -12})
            graph.reader.poll(SETTLE)
            for cycle in range(cycles):
                print(f"{PREFIX}: {name} cycle {cycle + 1} of {cycles}", flush=True)
                base = Window(graph, window)
                xruns += base.xruns
                add_under_test(name, spec)
                graph.reader.poll(SETTLE)
                for label, values, notes in settings:
                    graph.set(name, values)
                    graph.reader.poll(SETTLE)
                    if notes:
                        play_notes(notes)
                        graph.reader.poll(SETTLE)
                    loaded = Window(graph, window)
                    xruns += loaded.xruns
                    cpu[label].append(loaded.cpu - base.cpu)
                    if notes:
                        release_notes()
                        graph.reader.poll(SETTLE + 1.5)
                if cycle + 1 < cycles:
                    modui.remove_plugin(name)
                    graph.reader.poll(SETTLE)
        finally:
            graph.close()
    return {
        "plugin": name,
        "version": f"{spec['major']}.{spec['minor']}.{spec['micro']}",
        "date": datetime.date.today().isoformat(),
        "commit": repo_commit(),
        "device": device_info(),
        "source": "siggen white noise at -12 dBFS" if has_source else
                  "MIDI notes from the ALSA sequencer (see the settings)" if spec.get("midi-in") else
                  "none (no audio input)",
        "cycles": cycles,
        "window_s": window,
        "cpu_points": [
            {"settings": label, "median": round(statistics.median(values), 2), "min": round(min(values), 2),
             "max": round(max(values), 2)}
            for label, values in cpu.items()
        ],
        "memory_kb": dict(performance.probe_memory(name), binary=binary_kb(name)),
        "xruns": xruns,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("plugins", nargs="+")
    parser.add_argument("--cycles", type=int, default=3)
    parser.add_argument("--window", type=float, default=6.0, help="seconds per CPU window")
    parser.add_argument("--restore", action="store_true", help="reload the previous pedalboard afterwards")
    args = parser.parse_args()
    needed = sorted(set(args.plugins) | {"siggen"})
    missing = devicetest.missing_plugins(needed)
    if missing:
        print(f"{PREFIX}: not installed: {missing} (tools/jp.py perf deploys them)")
        return 1
    for name in args.plugins:
        data = measure(name, args.cycles, args.window, args.restore)
        path = REPO_DIR / "plugins" / name / "performance.json"
        path.write_text(json.dumps(data, indent=2) + "\n")
        print(f"{PREFIX}: wrote {path.relative_to(REPO_DIR)}")
        print(performance.plugin_table(data))
    for path in performance.update_docs():
        print(f"{PREFIX}: updated {path.relative_to(REPO_DIR)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
