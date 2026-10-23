#!/usr/bin/env python3
"""Device integration test of shipahoy on the Dwarf (roadmap step 10).

Needs siggen, meter and shipahoy installed (tools/jp.py test-device shipahoy deploys them)
and the Mac test build for the expected filter responses (build-tests, target
shipahoy_expected). Empties the graph and builds
  siggen/out -> shipahoy/in -> shipahoy/out -> meter/signal
  siggen/out -------------------------------> meter/reference
(nothing goes to the audio outputs), then checks with the meter's readings. The plugin has no running state without
a script, so the checks that need a fixed filter write a user script that holds it (frequency, resonance, type) over
ssh and select it (Script = User 1, loaded within the 0.5 s the plugin polls the file):
  - CPU: mean mod-ui CPU with shipahoy running at the "Ship Ahoy" settings (the first script) against the same
    graph before shipahoy was added,
  - mix 0 gives exactly the reference; bypass on gives exactly the reference, off does not,
  - each of the 15 filter types, held at 1 kHz without resonance, matches its transfer function at the eight meter
    bands (gain and coherence),
  - jumps of the mix between 0 and 100 every 50 ms add no step above the dry and the wet
    signal (largest sample step, the zipper metric),
  - every factory script, X and Y at their minimum and maximum, and silence at full resonance give no non-finite and
    no denormal samples,
  - scripts: a user script written over ssh and selected is loaded (script status 1) and a script that moves to A5 at
    every step sounds like the one that holds A5 with nothing late and nothing dropped (the lead of two blocks covers
    the worker); a broken edit leaves it playing (status 3), a removed file gives status 2, selecting the first script
    gives status 1 again,
  - X and Y: the first script's X and Y reach the audio (a different glide and a different resonance sound
    different), and a script reads them with GetX() and GetY() (a script that holds the note GetX() says),
  - drift: with a drift depth (SetDrift) the level of a sine on the slope of a lowpass moves away from the fixed
    filter's, stays finite at depth 12 and rate 20, and depth 0 gives the fixed filter's level back (a sine: noise
    through a narrow resonance varies by 0.5 dB between windows),
  - played notes: siggen is the player; a user script that sends the filter to the note OnNewPitch reports (the
    commands of the hook are applied at the next block) makes a played A5 and then a played A4 sound like the filter
    held at 880 and 440 Hz, and the script stays ok,
  - knob 1 -> mix and footswitch B -> bypass addressed, confirmed by mod-ui's hw_map messages,
  - xruns unchanged while the graph processes (from 1 s after shipahoy joined it to the end;
    an xrun while plugins are added is reported, not failed: it was seen once in about 45
    adds and comes from the graph change, not from processing), no problem lines in the jack2
    journal,
then leaves the board loaded with the addressing (with --restore: reloads the previous
pedalboard). Raw messages go to build-dwarf/scratch/test_shipahoy.log.

Usage:
  test_shipahoy.py [--restore] [--window SECONDS]
"""

import argparse
import json
import math
import re
import subprocess
import sys
import time

import devicetest
from devicetest import FLOOR_DB, REPO_DIR, SILENCE, SINE, URI_BASE, WHITE_NOISE, modui

sys.path.insert(0, str(REPO_DIR / "generator"))
import jpgen  # noqa: E402

PREFIX = "test_shipahoy"
BANDS_HZ = [63, 125, 250, 500, 1000, 2000, 4000, 8000]
CENTRE_HZ = 1000.0
SAMPLE_RATE = 48000.0
EXPECTED_TOOL = REPO_DIR / "build-tests" / "tests" / "host" / "shipahoy_expected"
SHIPAHOY_JSON = REPO_DIR / "plugins" / "shipahoy" / "shipahoy.json"
# tolerances: the Mac measures within 0.077 dB through the .so (step 8); the device run uses
# shorter windows, and coherence drops on steep flanks (0.988 for "notch octaves" at 500 Hz)
GAIN_TOLERANCE_DB = 0.3
MIN_COHERENCE = 0.98
THEORY_FLOOR_DB = -40.0
CPU_CEILING_POINTS = 5.0
# shipahoy's smoothers (20 ms) snap to the target once less than 1e-7 is left (OnePoleSmoother):
# about 17 time constants, 0.34 s, after a full jump; checks that expect bit-identical output
# wait longer than that
SETTLE_EXACT = 1.0
ZIPPER_FACTOR = 1.1
SCRIPT_USER = 9  # the Script port: 0 to 8 the plugin's, 9 = user-1
FACTORY_SCRIPTS = range(9)
TEST_SCRIPT = "/data/jaypacks/scripts/shipahoy/user-1.lua"
RELOAD_WAIT = 1.4  # the plugin looks at the file twice a second


def expected_gains():
    """{type index: (label, [gain dB per band])} from the C++ transfer function."""
    if not EXPECTED_TOOL.exists():
        subprocess.run(["cmake", "--build", str(REPO_DIR / "build-tests"), "--target", "shipahoy_expected"],
                       check=True, capture_output=True)
    text = subprocess.run([str(EXPECTED_TOOL), str(CENTRE_HZ), str(SAMPLE_RATE), *map(str, BANDS_HZ)],
                          check=True, capture_output=True, text=True).stdout
    return {entry["index"]: (entry["label"], entry["gainsDb"]) for entry in json.loads(text)["types"]}


def type_names():
    """The names of the FilterType constants, in menu order (plugins/shipahoy/ShipAhoyScript.h)."""
    text = (REPO_DIR / "plugins" / "shipahoy" / "ShipAhoyScript.h").read_text(encoding="ascii")
    block = text[text.index("c_filterTypeEntries{{"):]
    return re.findall(r'\{"(\w+)", \d+\}', block.split("}};")[0] + "}")


def fixed_script(hz, resonance, type_name, extra=""):
    """A user script that holds a filter: the frequency in Hz, the resonance in percent, the type by name."""
    return (f"function OnStart()\n    SetFilterType(FilterType.{type_name})\n    SetResonance({resonance})\n"
            f"    SetCutoff(69 + 12 * math.log({hz} / 440, 2), {{ glide = 0 }})\n{extra}end\n")


def hold(graph, text, wait=RELOAD_WAIT):
    """Writes the user script, selects it and waits until the plugin has loaded it."""
    write_remote(TEST_SCRIPT, text)
    graph.set("shipahoy", {"script": SCRIPT_USER})
    graph.reader.poll(wait)


def shipahoy_controls():
    desc = jpgen.validate(jpgen.load(SHIPAHOY_JSON))
    return {port["symbol"]: jpgen.control_range(port) for port in desc["ports-control"]}, \
        {port["symbol"]: port["default"] for port in desc["ports-control"]}


def no_bad_samples(check, graph, what):
    check.near(f"{what}: non-finite samples", graph.out("meter", "nonFinite"), 0.0, 0.0)
    check.near(f"{what}: denormal samples", graph.out("meter", "denormals"), 0.0, 0.0)


def check_mix_and_bypass(graph, check, window):
    graph.set("siggen", {"waveform": WHITE_NOISE, "level": -6, "on": 1})
    graph.set("meter", {"window": window})
    graph.set("shipahoy", {"mix": 0})
    graph.reader.poll(SETTLE_EXACT)
    graph.measure(window)
    check.near("mix 0: diff peak dB (output = reference)", graph.out("meter", "diffPeak"), FLOOR_DB, 0.0)
    hold(graph, fixed_script(CENTRE_HZ, 0, "Lp24"))
    graph.set("shipahoy", {"mix": 100})
    graph.bypass("shipahoy", True)
    graph.reader.poll(SETTLE_EXACT)
    graph.measure(window)
    check.near("bypass on: diff peak dB (output = reference)", graph.out("meter", "diffPeak"), FLOOR_DB, 0.0)
    graph.bypass("shipahoy", False)
    graph.reader.poll(0.3)
    graph.measure(window)
    check.above("bypass off, lp 24 dB: diff peak dB", graph.out("meter", "diffPeak"), -20.0)


def check_filter_types(graph, check, expected, window):
    graph.set("siggen", {"waveform": WHITE_NOISE, "level": -12})
    graph.set("meter", {f"band{k + 1}Hz": hz for k, hz in enumerate(BANDS_HZ)})
    graph.set("meter", {"window": window})
    graph.set("shipahoy", {"mix": 100})
    names = type_names()
    worst = 0.0
    for index, (label, theory) in sorted(expected.items()):
        hold(graph, fixed_script(CENTRE_HZ, 0, names[index]))
        graph.reader.poll(0.3)
        graph.measure(window)
        for k, hz in enumerate(BANDS_HZ):
            if theory[k] <= THEORY_FLOOR_DB:
                continue
            gain = graph.out("meter", f"band{k + 1}Gain")
            coherence = graph.out("meter", f"band{k + 1}Coherence")
            deviation = None if gain is None else gain - theory[k]
            if deviation is not None:
                worst = max(worst, abs(deviation))
            check.near(f"{label} at {hz} Hz: gain - theory dB", deviation, 0.0, GAIN_TOLERANCE_DB)
            check.above(f"{label} at {hz} Hz: coherence", coherence, MIN_COHERENCE)
        no_bad_samples(check, graph, label)
    check.info(f"filter types: worst deviation from the transfer function {worst:.3f} dB")


def check_mix_jumps(graph, check):
    window = 2.0
    graph.set("siggen", {"waveform": SINE, "frequency": 220, "level": -12})
    hold(graph, fixed_script(CENTRE_HZ, 0, "Lp12"))
    graph.set("meter", {"window": window, "toneHz": 220})
    steps = {}
    for mix in (0, 100):
        graph.set("shipahoy", {"mix": mix})
        graph.reader.poll(0.3)
        graph.measure(window)
        steps[mix] = graph.out("meter", "maxStep")
    measure_id = graph.start_measure()
    mix = 0
    jumps = 0
    end = time.monotonic() + window + 5.0
    while graph.out("meter", "resultId") != measure_id and time.monotonic() < end:
        mix = 100 - mix
        graph.set("shipahoy", {"mix": mix})
        jumps += 1
        graph.reader.poll(0.05)
    graph.wait_result(measure_id, window)
    jumping = graph.out("meter", "maxStep")
    limit = max(steps[0], steps[100]) + 20.0 * math.log10(ZIPPER_FACTOR)
    check.info(f"largest sample step: dry {steps[0]:.3f} dB, wet {steps[100]:.3f} dB, mix jumping "
               f"({jumps} jumps sent during the window) {jumping} dB")
    check.below("mix jumping every 50 ms: largest sample step dB", jumping, limit)
    no_bad_samples(check, graph, "mix jumps")


def write_remote(path, text):
    folder = path.rsplit("/", 1)[0]
    subprocess.run(["ssh", *modui.SSH_OPTS, f"root@{modui.HOST}", f"mkdir -p {folder} && cat > {path}"], input=text,
                   text=True, check=True, timeout=30)


def read_remote(path):
    try:
        return modui.ssh(f"cat {path} 2>/dev/null; true")
    except modui.DeviceError:
        return ""


def check_script(graph, check, defaults):
    saved = read_remote(TEST_SCRIPT)
    window = 2.0
    try:
        graph.set("siggen", {"waveform": WHITE_NOISE, "level": -12})
        graph.set("meter", {"window": window})
        graph.set("shipahoy", dict(defaults, mix=100))
        hold(graph, fixed_script(880, 60, "Lp12"))
        graph.reader.poll(SETTLE_EXACT)
        graph.measure(window)
        reference = graph.out("meter", "rms")

        stepping = ("function OnStart()\n    SetFilterType(FilterType.Lp12)\n    SetResonance(60)\nend\n"
                    "function OnStep(step)\n    SetCutoff(81, { glide = 0 })\nend\n")
        hold(graph, stepping)
        check.near("script status of a user script", graph.out("shipahoy", "scriptStatus"), 1.0, 0.0)
        graph.reader.poll(SETTLE_EXACT)
        graph.measure(window)
        stepped = graph.out("meter", "rms")
        check.near("a script moving to A5 on every step sounds like the held A5: rms dB", stepped, reference, 0.3)
        no_bad_samples(check, graph, "script")
        # the lead of the clock (two blocks) covers the worker on the Dwarf: nothing was applied after its sample
        check.near("script: commands applied late", graph.out("shipahoy", "late"), 0.0, 0.0)
        check.near("script: events and commands dropped", graph.out("shipahoy", "dropped"), 0.0, 0.0)

        write_remote(TEST_SCRIPT, "function OnStep(step  -- oops")
        graph.reader.poll(RELOAD_WAIT)
        check.near("script status of a broken file (3: compile error)", graph.out("shipahoy", "scriptStatus"), 3.0, 0.0)
        graph.measure(window)
        # noise through a resonance varies by up to 0.5 dB between two windows, so the level of the script as it played
        check.near("the old script keeps playing: rms dB", graph.out("meter", "rms"), stepped, 0.6)

        modui.ssh(f"rm -f {TEST_SCRIPT}")
        graph.reader.poll(RELOAD_WAIT)
        check.near("script status when the file is removed (2: file missing)", graph.out("shipahoy", "scriptStatus"), 2.0, 0.0)
        graph.set("shipahoy", {"script": 0})
        graph.reader.poll(SETTLE_EXACT)
        check.near("script status with the first script (1: ok)", graph.out("shipahoy", "scriptStatus"), 1.0, 0.0)
        graph.set("siggen", {"waveform": SILENCE})
    finally:
        if saved:
            write_remote(TEST_SCRIPT, saved)
        else:
            modui.ssh(f"rm -f {TEST_SCRIPT}; true")
        graph.set("shipahoy", {"script": 0})


def check_x_and_y(graph, check, defaults):
    saved = read_remote(TEST_SCRIPT)
    window = 2.0
    try:
        graph.set("siggen", {"waveform": WHITE_NOISE, "level": -12})
        graph.set("meter", {"window": window})
        # a script that holds the note X says (0 to 100 is the MIDI note) and the resonance Y says
        reading = ("function OnStart()\n    SetFilterType(FilterType.Lp12)\n    SetCutoff(GetX(), { glide = 0 })\n"
                   "    SetResonance(GetY())\nend\n"
                   "function OnControl(x, y)\n    SetCutoff(x, { glide = 0 })\n    SetResonance(y)\nend\n")
        graph.set("shipahoy", dict(defaults, mix=100, x=81, y=60))
        hold(graph, reading)
        graph.reader.poll(SETTLE_EXACT)
        graph.measure(window)
        reached = graph.out("meter", "rms")
        hold(graph, fixed_script(880, 60, "Lp12"))
        graph.reader.poll(SETTLE_EXACT)
        graph.measure(window)
        check.near("GetX() and GetY() give the knobs: rms dB against the held A5", reached, graph.out("meter", "rms"), 0.3)
        hold(graph, reading)
        graph.set("shipahoy", {"x": 60})  # OnControl moves the filter to note 60 within one block
        graph.reader.poll(SETTLE_EXACT)
        graph.measure(window)
        moved = graph.out("meter", "rms")
        check.record(reached is not None and moved is not None and abs(moved - reached) > 0.5,
                     f"OnControl follows a move of X: rms {moved} dB against {reached} dB before")
        check.near("script status with OnControl", graph.out("shipahoy", "scriptStatus"), 1.0, 0.0)

        # the first script: X is the glide and Y the resonance
        graph.set("shipahoy", dict(defaults, mix=100, script=0, tempo=240))
        graph.reader.poll(SETTLE_EXACT)
        levels = {}
        for x, y in ((5, 85), (5, 10), (100, 85)):
            graph.set("shipahoy", {"x": x, "y": y})
            graph.reader.poll(0.5)
            graph.measure(window)
            levels[(x, y)] = graph.out("meter", "rms")
        check.record(all(v is not None for v in levels.values()) and len({round(v, 1) for v in levels.values()}) == 3,
                     f"X and Y reach the first script: rms {levels}")
        graph.set("siggen", {"waveform": SILENCE})
    finally:
        if saved:
            write_remote(TEST_SCRIPT, saved)
        else:
            modui.ssh(f"rm -f {TEST_SCRIPT}; true")
        graph.set("shipahoy", {"script": 0})


def check_drift(graph, check, defaults):
    graph.set("siggen", {"waveform": SINE, "frequency": 1500, "level": -12})
    graph.set("meter", {"window": 2.0})
    graph.set("shipahoy", dict(defaults, mix=100))

    def drifting(depth, rate):
        return f"    SetDrift({depth}, {{ rate = {rate} }})\n"

    hold(graph, fixed_script(1000, 0, "Lp12", drifting(0, 1)))
    graph.reader.poll(SETTLE_EXACT)
    graph.measure(2.0)
    reference = graph.out("meter", "rms")
    hold(graph, fixed_script(1000, 0, "Lp12", drifting(6, 5)), RELOAD_WAIT + 0.5)
    graph.measure(2.0)
    moving = graph.out("meter", "rms")
    check.record(abs(moving - reference) > 0.05,
                 f"drift depth 6 at 5 Hz moves the level: rms {moving} dB against {reference} dB without")
    no_bad_samples(check, graph, "drift depth 6")
    hold(graph, fixed_script(1000, 0, "Lp12", drifting(12, 20)), RELOAD_WAIT + 0.5)
    graph.measure(2.0)
    no_bad_samples(check, graph, "drift depth 12 at 20 Hz")
    hold(graph, fixed_script(1000, 0, "Lp12", drifting(0, 1)), RELOAD_WAIT + 0.5)
    graph.measure(2.0)
    check.near("depth 0 gives the fixed filter back: rms dB", graph.out("meter", "rms"), reference, 0.05)
    graph.set("siggen", {"waveform": SILENCE})


PLAYED_SETUP = "    SetFilterType(FilterType.Lp12)\n    SetResonance(0)\n"
PLAYED_SCRIPT = ("function OnStart()\n" + PLAYED_SETUP + "end\n"
                 "function OnNewPitch(note, hz, pitch, periodicity, level)\n"
                 "    SetCutoff(pitch, { glide = 0 })\n"
                 "end\n")


def check_played_notes(graph, check, defaults):
    saved = read_remote(TEST_SCRIPT)
    window = 2.0
    try:
        graph.set("siggen", {"waveform": SINE, "level": -12})
        graph.set("meter", {"window": window})
        graph.set("shipahoy", dict(defaults, mix=100))
        reference = {}
        for tone, filter_hz in ((880, 880), (440, 440), (440, 880)):
            graph.set("siggen", {"frequency": tone})
            hold(graph, fixed_script(filter_hz, 0, "Lp12"))
            graph.reader.poll(SETTLE_EXACT)
            graph.measure(window)
            reference[(tone, filter_hz)] = graph.out("meter", "rms")
        right, wrong = reference[(440, 440)], reference[(440, 880)]
        check.record(right is not None and wrong is not None and abs(right - wrong) > 1.0,
                     f"an A4 sounds different through a filter at 440 Hz ({right} dB) than at 880 Hz ({wrong} dB)")

        graph.set("siggen", {"frequency": 880})
        hold(graph, PLAYED_SCRIPT, RELOAD_WAIT + 0.5)
        check.near("script status of a script with OnNewPitch", graph.out("shipahoy", "scriptStatus"), 1.0, 0.0)
        graph.measure(window)
        check.near("a played A5 moves the filter to 880 Hz: rms dB", graph.out("meter", "rms"), reference[(880, 880)], 0.3)
        no_bad_samples(check, graph, "played A5")
        graph.set("siggen", {"frequency": 440})
        graph.reader.poll(0.5)
        graph.measure(window)
        check.near("then a played A4 moves it to 440 Hz: rms dB", graph.out("meter", "rms"), reference[(440, 440)], 0.3)
        check.near("script status after the notes", graph.out("shipahoy", "scriptStatus"), 1.0, 0.0)
        graph.set("siggen", {"waveform": SILENCE})
        graph.reader.poll(0.5)
        check.near("script status after silence", graph.out("shipahoy", "scriptStatus"), 1.0, 0.0)
    finally:
        if saved:
            write_remote(TEST_SCRIPT, saved)
        else:
            modui.ssh(f"rm -f {TEST_SCRIPT}; true")
        graph.set("shipahoy", {"script": 0})
        graph.set("siggen", {"frequency": 1000})


def check_hops_and_extremes(graph, check, ranges, defaults, window):
    graph.set("siggen", {"waveform": WHITE_NOISE, "level": -12})
    graph.set("meter", {"window": window})
    graph.set("shipahoy", dict(defaults, tempo=240))
    for script in FACTORY_SCRIPTS:
        graph.set("shipahoy", {"script": script})
        graph.reader.poll(0.8)
        graph.measure(window)
        no_bad_samples(check, graph, f"factory script {script}")
        check.below(f"factory script {script}: peak dB", graph.out("meter", "peak"), 20.0)
        check.near(f"factory script {script}: script status", graph.out("shipahoy", "scriptStatus"), 1.0, 0.0)
        check.info(f"factory script {script}: rms {graph.out('meter', 'rms')} dB, peak {graph.out('meter', 'peak')} dB")
    graph.set("shipahoy", {"script": 0})
    for side, pick in (("minimum", 0), ("maximum", 1)):
        graph.set("shipahoy", {symbol: limits[pick] for symbol, limits in ranges.items()
                               if symbol not in ("script", "sync", "transport", "hostTempo")})
        graph.reader.poll(0.3)
        graph.measure(window)
        no_bad_samples(check, graph, f"every control at its {side}")
        check.info(f"every control at its {side}: peak {graph.out('meter', 'peak')} dB")
    graph.set("shipahoy", defaults)
    graph.set("shipahoy", {"y": 100})
    graph.set("siggen", {"waveform": SILENCE})
    graph.reader.poll(0.5)
    graph.measure(window)
    no_bad_samples(check, graph, "silence at Y = 100 (resonance 100)")
    graph.set("shipahoy", defaults)


def check_cpu(graph, check, baseline_window, defaults):
    """Mean CPU before and after shipahoy joins the graph (siggen and meter run in both).
    Returns the time from which the graph only processes (no more plugins added)."""
    graph.set("siggen", {"waveform": WHITE_NOISE, "level": -12})
    start = time.monotonic()
    graph.reader.poll(baseline_window)
    before, _ = devicetest.stats_from_log(graph.reader.log, since=start)
    modui.add_plugin("shipahoy", URI_BASE + "shipahoy", x=350, y=200)
    modui.connect("siggen/out", "shipahoy/in")
    modui.connect("shipahoy/out", "meter/signal")
    graph.set("shipahoy", defaults)
    graph.reader.poll(1.0)
    start = time.monotonic()
    graph.reader.poll(baseline_window)
    after, _ = devicetest.stats_from_log(graph.reader.log, since=start)
    if not before or not after:
        check.record(False, "no stats messages for the CPU comparison")
        return start
    mean_before = sum(before) / len(before)
    mean_after = sum(after) / len(after)
    check.info(f"cpu without shipahoy {mean_before:.1f} %, with shipahoy (Ship Ahoy settings) {mean_after:.1f} %")
    check.below("cpu added by shipahoy (percentage points)", mean_after - mean_before, CPU_CEILING_POINTS)
    return start


def check_addressing(graph, check):
    modui.address("/graph/shipahoy/mix", "/hmi/knob1", "Mix", 0.0, 100.0, 100.0, 201)
    modui.address("/graph/shipahoy/:bypass", "/hmi/footswitch1", "Ship Ahoy", 0.0, 1.0, 0.0, 33)
    fresh = modui.WebSocket()  # a new client gets the whole state, hw_map messages included
    try:
        maps = {modui.parse_hw_map(message) for message in fresh.drain(2.0)}
    finally:
        fresh.close()
    check.record(("shipahoy", "mix", "/hmi/knob1") in maps, "hw_map: knob 1 -> shipahoy mix")
    check.record(("shipahoy", ":bypass", "/hmi/footswitch1") in maps, "hw_map: footswitch B -> shipahoy bypass")


def run(restore, window):
    missing = devicetest.missing_plugins(["siggen", "meter", "shipahoy"])
    if missing:
        print(f"{PREFIX}: not installed: {missing} (tools/jp.py test-device shipahoy deploys them)")
        return 1
    expected = expected_gains()
    ranges, defaults = shipahoy_controls()
    check = devicetest.Check()
    with modui.preserved_pedalboard(restore) as board:
        print(f"{PREFIX}: pedalboard before the test: {board or '(none)'}")
        cursor = devicetest.journal_cursor()
        modui.reset()
        modui.add_plugin("siggen", URI_BASE + "siggen", x=50, y=200)
        modui.add_plugin("meter", URI_BASE + "meter", x=650, y=200)
        modui.connect("siggen/out", "meter/reference")
        graph = devicetest.Graph()
        start = time.monotonic()
        try:
            graph.reader.poll(1.0)
            print(f"{PREFIX}: cpu ...", flush=True)
            processing = check_cpu(graph, check, 5.0, defaults)
            steps = [
                ("mix and bypass", lambda: check_mix_and_bypass(graph, check, 1.0)),
                ("filter types", lambda: check_filter_types(graph, check, expected, window)),
                ("mix jumps", lambda: check_mix_jumps(graph, check)),
                ("factory scripts and extremes", lambda: check_hops_and_extremes(graph, check, ranges, defaults, 2.0)),
                ("script", lambda: check_script(graph, check, defaults)),
                ("x and y", lambda: check_x_and_y(graph, check, defaults)),
                ("drift", lambda: check_drift(graph, check, defaults)),
                ("played notes", lambda: check_played_notes(graph, check, defaults)),
                ("addressing", lambda: check_addressing(graph, check)),
            ]
            for name, step in steps:
                print(f"{PREFIX}: {name} ...", flush=True)
                try:
                    step()
                except modui.DeviceError as error:
                    check.record(False, f"{name}: {error}")
            graph.reader.poll(1.0)
        finally:
            log = graph.save_log(f"{PREFIX}.log", start)
            graph.close()
        _, xruns = devicetest.stats_from_log(graph.reader.log)
        _, processing_xruns = devicetest.stats_from_log(graph.reader.log, since=processing)
        if xruns and processing_xruns:
            check.info(f"xruns over the whole run {xruns[0]} -> {xruns[-1]} (graph changes included)")
            check.record(processing_xruns[-1] == processing_xruns[0],
                         f"xruns while processing {processing_xruns[0]} -> {processing_xruns[-1]} (expected unchanged)")
        else:
            check.record(False, "no stats messages: xruns unknown")
        lines = devicetest.journal_lines(cursor)
        problems = devicetest.journal_problems(lines)
        check.record(not problems, f"jack2 journal: {len(problems)} problem lines among {len(lines)} during the run")
        for line in problems[:10]:
            check.info(f"journal: {line}")
    check.info(f"raw messages: {log}")
    check.info("previous pedalboard restored" if restore else "test board left loaded (knob 1 = mix, footswitch B = bypass)")
    return check.report(PREFIX)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--restore", action="store_true", help="reload the previous pedalboard afterwards")
    parser.add_argument("--window", type=float, default=8.0, help="meter window per filter type in seconds")
    args = parser.parse_args(argv)
    return run(args.restore, args.window)


if __name__ == "__main__":
    sys.exit(main())
